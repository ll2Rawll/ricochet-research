#include "exit_handler.h"
#include "vmcs_defs.h"
#include "vmx.h"
#include "../hv.h"
#include "../ept/ept.h"
#include "../common/memory.h"
#include "../hv_hook/hv_hook.h"    // HV_HYPERCALL_MAGIC_LEAF, HV_SYNTHETIC_EXIT_PAUSE

//
// ─── Win11 24H2 / 25H2 Intel Timing Fix ──────────────────────────────────────
//
// When running as a UEFI-injected hypervisor *inside* Hyper-V's exit chain
// (hyper-reV / hyperjacking style), returning to the guest directly after
// handling your own exit (EPT violation, CPUID hook) bypasses hvix64.dll's
// epilogue.  Hyper-V's STIMER vCPU scheduler tracks "how many exits has each
// core processed?" and if a core appears to vanish it fires the watchdog:
//
//     CLOCK_WATCHDOG_TIMEOUT   (0x101)
//     SYNTHETIC_WATCHDOG_TIMEOUT (0x1CA)
//
// The fix (discovered by Hattie8491 / Ngala in forum logs):
//   After handling your exit, write a *benign* fake exit reason into the VMCS
//   (Hyper-V's internal PAUSE synthetic code 0x7B) and then call hvix64's
//   real handler.  hvix64 sees a trivial PAUSE, runs its full epilogue
//   (clock-balancing, scheduler accounting, idle wakeup), and the watchdog
//   stays quiet.
//
// In a STANDALONE kernel-mode hypervisor (you ARE the hypervisor, not nested
// inside hvix64) this exact trick doesn't apply — you're already the exit
// handler.  What DOES apply is:
//   • Don't intercept STIMER MSRs (MSR bitmap all-zero = full passthrough).
//   • Handle EPT violations fast (< a few µs).
//   • Pass all unhandled exits back to the guest with VMRESUME — do NOT loop.
//
// The HV_HYPERJACK_MODE #define below gates the fake-PAUSE injection for the
// case where this code is compiled into a hyper-reV-style UEFI payload and
// calls an existing original_handler pointer.
//

// #define HV_HYPERJACK_MODE   // uncomment if building as hvix64 parasite

#ifdef HV_HYPERJACK_MODE
static UINT64(*g_OriginalHandler)(UINT64, UINT64, UINT64, UINT64) = nullptr;

static void FakePauseAndCallOriginal(UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4)
{
    //
    // Overwrite exit reason with Hyper-V's synthetic PAUSE code 0x7B.
    // hvix64 will treat this as a trivial guest PAUSE instruction,
    // run its full epilogue (STIMER accounting, vCPU scheduler),
    // then VMRESUME.  This keeps the watchdog alive.
    //
    __vmx_vmwrite(VMCS_RO_EXIT_REASON, HV_SYNTHETIC_EXIT_PAUSE);
    __vmx_vmwrite(VMCS_RO_INSN_LENGTH, 2);   // PAUSE is F3 90, 2 bytes

    if (g_OriginalHandler)
        g_OriginalHandler(a1, a2, a3, a4);
}
#endif // HV_HYPERJACK_MODE

// ─────────────────────────────────────────────────────────────────────────────

static UINT64 VmcsRead(UINT64 Field)
{
    UINT64 v = 0;
    __vmx_vmread(Field, &v);
    return v;
}

static void VmcsWrite(UINT64 Field, UINT64 Value)
{
    __vmx_vmwrite(Field, Value);
}

static void AdvanceGuestRip()
{
    const UINT64 rip     = VmcsRead(VMCS_GUEST_RIP);
    const UINT64 insnLen = VmcsRead(VMCS_RO_INSN_LENGTH);
    VmcsWrite(VMCS_GUEST_RIP, rip + insnLen);
}

// ─── CPUID emulation ─────────────────────────────────────────────────────────
//
// Pass through all leaves unchanged, EXCEPT:
//   • Leaf 1 ECX bit 5 (VMX): clear so guests can't detect nested VMX.
//   • Leaf 0x40000000: return our own HV identity instead of advertising
//     Hyper-V (avoids Windows enabling enlightenments that assume Hyper-V).
//
static void HandleCpuid(GpRegs* Regs)
{
    const UINT32 leaf    = (UINT32)Regs->Rax;
    const UINT32 subleaf = (UINT32)Regs->Rcx;

    int regs[4] = {};
    __cpuidex(regs, (int)leaf, (int)subleaf);

    if (leaf == 1) {
        regs[2] &= ~(1 << 5);  // clear VMX bit (ECX[5])
    } else if (leaf == 0x40000000) {
        // Advertise as unknown hypervisor to suppress Windows enlightenments
        regs[0] = 0x40000001;
        regs[1] = 'VHPK'; // "KPHVerifiedHV" or whatever you like
        regs[2] = 0;
        regs[3] = 0;
    }

    Regs->Rax = (UINT64)(UINT32)regs[0];
    Regs->Rbx = (UINT64)(UINT32)regs[1];
    Regs->Rcx = (UINT64)(UINT32)regs[2];
    Regs->Rdx = (UINT64)(UINT32)regs[3];

    AdvanceGuestRip();
}

// ─── EPT violation handler ───────────────────────────────────────────────────
//
// On an EPT violation the exit qualification tells us whether this is
// a read/write fault or an execute fault.  For our shadow-page hooks:
//
//   Execute fault (bit 2 set):
//     Guest is trying to RUN a page that we've marked --x (shadow pfn).
//     → We set the PTE back to rw- with original pfn so reads/writes work.
//
//   Read/Write fault (bits 0/1):
//     Guest is trying to READ or WRITE a page we temporarily made non-rw.
//     → Restore rw- to original pfn, clear execute so next exec refaults.
//
// This toggle keeps the shadow visible only during execution.
//
static void HandleEptViolation()
{
    const UINT64 qual = VmcsRead(VMCS_RO_EXIT_QUAL);
    const UINT64 gpa  = VmcsRead(VMCS_RO_GUEST_PHYS_ADDR);

    const bool isExec  = (qual & (1ull << 2)) != 0;
    const bool isWrite = (qual & (1ull << 1)) != 0;
    const bool isRead  = (qual & (1ull << 0)) != 0;
    UNREFERENCED_PARAMETER(isWrite);
    UNREFERENCED_PARAMETER(isRead);

    void* eptRoot = g_Hv.SlatRootVa;
    if (!eptRoot) return;

    // Walk to the PTE for this GPA
    EptEntry* pte = Ept::GetOrSplitPte(eptRoot, gpa & ~0xFFFull, false);
    if (!pte) return;

    if (isExec) {
        // We're in shadow-execute mode.  Switch back to original PFN, rw-, no exec.
        const UINT64 origPfn = (gpa >> 12); // identity map → GPA pfn == HPA pfn
        pte->Read    = 1;
        pte->Write   = 1;
        pte->Execute = 0;
        pte->Pfn     = origPfn;
    } else {
        // Read/write mode. Keep original PFN readable; drop execute if hooked.
        // (shadow pfn would have been restored on next exec exit)
        pte->Read    = 1;
        pte->Write   = 1;
        pte->Execute = 0;
    }

    // Flush the EPT TLB for this EPTP
    Vmx::InvEpt(g_Hv.SlatRootPhys | ((UINT64)EPT_PAGEWALK_4 << 3) | EPT_MEMORY_TYPE_WB);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main dispatch — called from VmxHostEntryPoint (vmx_asm.asm)
// Returns 0 → VMRESUME, 1 → shutdown
// ─────────────────────────────────────────────────────────────────────────────
extern "C" UINT64 VmxExitDispatch(GpRegs* Regs)
{
    UINT64 exitReason = VmcsRead(VMCS_RO_EXIT_REASON) & 0xFFFF;

    switch (exitReason)
    {
    case VMX_EXIT_CPUID:
        HandleCpuid(Regs);
        return 0;

    case VMX_EXIT_VMCALL:
        //
        // Hypercall interface: rcx = magic | operation code
        // For a minimal implementation we simply advance RIP and ignore.
        // Extend here to dispatch to read/write/translate operations.
        //
        Regs->Rax = (UINT64)-1;   // STATUS_NOT_IMPLEMENTED
        AdvanceGuestRip();
        return 0;

    case VMX_EXIT_EPT_VIOLATION:
        HandleEptViolation();
        return 0;

    case VMX_EXIT_EPT_MISCONFIG:
        // Should not happen with a correct EPT setup. Inject #GP if it does.
        HV_LOG("EPT misconfiguration! GPA=0x%llX", VmcsRead(VMCS_RO_GUEST_PHYS_ADDR));
        return 0;

    case VMX_EXIT_HLT:
        // Let the guest halt (pass-through: inject HLT into guest execution).
        AdvanceGuestRip();
        return 0;

    case VMX_EXIT_XSETBV:
        // Pass through XSETBV
        _xsetbv((UINT32)Regs->Rcx, (Regs->Rdx << 32) | (Regs->Rax & 0xFFFFFFFF));
        AdvanceGuestRip();
        return 0;

    case VMX_EXIT_RDMSR:
    {
        UINT64 val = __readmsr((UINT32)Regs->Rcx);
        Regs->Rax = val & 0xFFFFFFFF;
        Regs->Rdx = val >> 32;
        AdvanceGuestRip();
        return 0;
    }

    case VMX_EXIT_WRMSR:
    {
        UINT64 val = (Regs->Rdx << 32) | (Regs->Rax & 0xFFFFFFFF);
        __writemsr((UINT32)Regs->Rcx, val);
        AdvanceGuestRip();
        return 0;
    }

    case VMX_EXIT_CR_ACCESS:
    {
        // Handle MOV to CR3 transparently
        const UINT64 qual = VmcsRead(VMCS_RO_EXIT_QUAL);
        const UINT8  crNum = (UINT8)(qual & 0xF);
        const UINT8  type  = (UINT8)((qual >> 4) & 0x3);
        if (crNum == 3 && type == 0 /* MOV to CR3 */) {
            VmcsWrite(VMCS_GUEST_CR3, Regs->Rax);
        }
        AdvanceGuestRip();
        return 0;
    }

    case VMX_EXIT_INVD:
        __wbinvd();
        AdvanceGuestRip();
        return 0;

    default:
        // Unhandled exit – pass back to guest harmlessly.
        // NOTE: In a full implementation you'd log these.
        break;
    }

    return 0;   // VMRESUME
}

// ─────────────────────────────────────────────────────────────────────────────
// VmxHyperjackDispatch
//
// Called from VmxHyperjackEntry (hv_hook_asm.asm) on every VM exit that hvix64
// now sees AFTER our 14-byte hook redirected its entry point to us.
//
// We are running in VMX HOST mode — VMREAD/VMWRITE go directly to the current
// VMCS with no further exits.  Hyper-V's scheduler is still watching, so:
//   • Handle only the exits we care about.
//   • After handling, overwrite EXIT_REASON with 0x7B (HV_SYNTHETIC_EXIT_PAUSE)
//     and INSN_LENGTH with 2 so hvix64's epilogue thinks the guest paused.
//     Without this, Win11 24H2+ fires CLOCK_WATCHDOG_TIMEOUT (0x101).
//   • Return — the ASM stub jumps to the trampoline, which executes the saved
//     original 14 bytes then continues inside hvix64 normally (→ VMRESUME).
// ─────────────────────────────────────────────────────────────────────────────
extern "C" void VmxHyperjackDispatch(GpRegs* HostRegs)
{
    const UINT64 exitReason = VmcsRead(VMCS_RO_EXIT_REASON) & 0xFFFF;
    bool handled = false;

    switch (exitReason)
    {
    // ── EPT violation ─────────────────────────────────────────────────────────
    // Forward to the same handler as standalone mode. In hyperjack mode,
    // g_Hv.SlatRootVa must point to a valid EPT structure (set during init).
    case VMX_EXIT_EPT_VIOLATION:
        if (g_Hv.SlatRootVa) {
            HandleEptViolation();
            handled = true;
        }
        break;

    // ── CPUID hypercall interface ─────────────────────────────────────────────
    // Any code (user or kernel) that executes CPUID with leaf HV_HYPERCALL_MAGIC_LEAF
    // will be intercepted here.  RCX (in HostRegs) carries the sub-operation.
    case VMX_EXIT_CPUID:
        if ((UINT32)HostRegs->Rax == HV_HYPERCALL_MAGIC_LEAF) {
            // Advance RIP (CPUID is 2 bytes)
            const UINT64 rip     = VmcsRead(VMCS_GUEST_RIP);
            const UINT64 insnLen = VmcsRead(VMCS_RO_INSN_LENGTH);
            VmcsWrite(VMCS_GUEST_RIP, rip + insnLen);

            // Minimal response: signal "OK" in EAX, zero the rest.
            HostRegs->Rax = 0x484Bu;   // 'HK'
            HostRegs->Rbx = 0;
            HostRegs->Rcx = 0;
            HostRegs->Rdx = 0;
            handled = true;
        }
        break;

    default:
        break;
    }

    if (handled) {
        //
        // ★ Win11 STIMER / Watchdog fix ★
        //
        // Hyper-V's vCPU scheduler counts exits.  If we swallow an exit and
        // return directly to the guest, the scheduler's count diverges from
        // reality and the STIMER watchdog fires a BSOD.
        //
        // Solution: before handing back to hvix64 (via the trampoline), fake
        // the exit as a guest PAUSE instruction (Hyper-V internal code 0x7B).
        // hvix64 handles PAUSE trivially — just bumps the counter and resumes.
        //
        VmcsWrite(VMCS_RO_EXIT_REASON, HV_SYNTHETIC_EXIT_PAUSE);
        VmcsWrite(VMCS_RO_INSN_LENGTH, 2);   // PAUSE = F3 90, 2 bytes
    }
    // If !handled: exit reason is unchanged, hvix64 handles it normally.
}

#include "exit_handler.h"
#include "vmcb_defs.h"
#include "svm.h"
#include "../hv.h"
#include "../npt/npt.h"
#include "../common/decode.h"
#include "../common/memory.h"

//
// ═══════════════════════════════════════════════════════════════════════════════
//  Win11 24H2 / 25H2 AMD Platform Fixes — READ THIS
// ═══════════════════════════════════════════════════════════════════════════════
//
//  FIX #1 — STIMER / Clock Watchdog (CLOCK_WATCHDOG_TIMEOUT 0x101)
//  ───────────────────────────────────────────────────────────────
//  Same root cause as Intel but AMD-flavoured.  In the hyper-reV hyperjacking
//  scenario you intercept an NPF, swap the nested CR3 (NCr3) to the hook NCR,
//  and return to guest — bypassing hvax64's epilogue.  Hyper-V's STIMER
//  scheduler sees the vCPU miss its accounting window and fires the watchdog.
//
//  The fix (Hattie8491 / Ngala, UnknownCheats forum):
//    After handling your NPF, BEFORE calling the real hvax64 handler,
//    set VMCB.Control.ExitCode = SVM_EXIT_PAUSE (0x77).
//    hvax64 sees a trivial PAUSE exit, runs its full epilogue (clock balance,
//    idle scheduler), returns to guest.  Watchdog stays alive.
//
//  In a STANDALONE kernel-mode hypervisor (this project when not in hyperjack
//  mode) the STIMER fix takes a different form:
//    • MSR permission map = all-zeros (STIMER MSRs pass through untouched).
//    • Don't spend > ~5 µs in the exit handler.
//    • Don't intercept PAUSE — let the guest spin freely.
//
//  FIX #2 — AMD NRIP Missing on #NPF (BSOD / hang)
//  ──────────────────────────────────────────────────
//  On Intel, hardware fills VMCS.VMEXIT_INSTRUCTION_LENGTH on every synchronous
//  exit (CPUID, VMMCALL, etc.), giving you NEXT_RIP = RIP + length.
//
//  On AMD, hardware fills VMCB.Control.NextRip ONLY for synchronous, non-fault
//  exits (CPUID, VMMCALL, MSR, …).  For #NPF (a *fault*, not a synchronous
//  intercept), NextRip is LEFT AT ZERO.
//
//  Why this matters:
//    When you fake a PAUSE exit (Fix #1) to route through hvax64, hvax64 will
//    try to advance RIP by reading VMCB.Control.NextRip.  If NextRip == 0,
//    hvax64 sets guest RIP to 0 → guest triple-faults → BSOD or freeze.
//
//    Additionally, if YOU want to advance past the faulting instruction (e.g.
//    to emulate or skip it), you need the instruction length.
//
//  The fix (Ngala, UnknownCheats forum):
//    Read the faulting instruction bytes.  AMD conveniently copies up to 15
//    bytes into VMCB.Control.InsnBytes (at offset 0x0D1) with InsnLen (0x0D0)
//    indicating how many bytes are valid.  If InsnLen > 0 use them directly.
//    If InsnLen == 0 (some CPUs / some exits don't fill this), fall back to
//    mapping the guest virtual address (VMCB.Save.Rip) through the guest's
//    page table and decoding manually with our x86-64 length decoder.
//
//    Once you have the length:
//        vmcb->Control.NextRip = vmcb->Save.Rip + decoded_len;
//
// ═══════════════════════════════════════════════════════════════════════════════

// ── Hyperjack mode gate ───────────────────────────────────────────────────────
// Uncomment when compiling as a hyper-reV style UEFI/hvax64 parasite.
// In standalone mode leave it defined-out.
//
// #define HV_HYPERJACK_MODE

#ifdef HV_HYPERJACK_MODE
static UINT64(*g_SvmOriginalHandler)(UINT64, UINT64, UINT64, UINT64) = nullptr;
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  Retrieve the current vCPU's VMCB virtual address.
//  The C launcher stored it in g_Hv.Vcpus[].VmcbVa indexed by processor.
// ─────────────────────────────────────────────────────────────────────────────
static Vmcb* GetCurrentVmcb()
{
    const UINT32 idx = KeGetCurrentProcessorNumberEx(nullptr);
    if (idx >= g_Hv.CpuCount) return nullptr;
    return static_cast<Vmcb*>(g_Hv.Vcpus[idx].VmcbVa);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fix #2:  Compute (or recover) the length of the faulting instruction.
//
//  Strategy:
//   1. If VMCB.InsnLen > 0, the CPU already decoded it for us → use it.
//   2. Otherwise, map guest RIP through the guest page table, read up to 15
//      bytes, and run our software decoder.
// ─────────────────────────────────────────────────────────────────────────────
static UINT32 RecoverInsnLength(Vmcb* vmcb)
{
    // Case 1: CPU filled InsnLen (happens for most intercepts except #NPF)
    if (vmcb->Control.InsnLen > 0)
        return vmcb->Control.InsnLen;

    // Case 2: Must decode manually.
    //
    // Walk guest CR3 → GPA → map to host VA → read bytes → decode.
    //
    const UINT64 guestRip = vmcb->Save.Rip;
    const UINT64 guestCr3 = vmcb->Save.Cr3;

    // Translate GVA → GPA using the *guest* (non-nested) page table
    const UINT64 gpa = Memory::WalkPageTable(guestCr3, guestRip);
    if (!gpa) return 1;    // fallback: assume 1-byte (worst case)

    // Map the GPA into host VA space for reading
    constexpr SIZE_T ReadSize = 15;
    void* hostVa = Memory::MapPhysical(gpa, ReadSize);
    if (!hostVa) return 1;

    const UINT32 len = Decode::InsnLength(static_cast<const UINT8*>(hostVa), (UINT32)ReadSize);
    Memory::UnmapPhysical(hostVa, ReadSize);

    return (len > 0) ? len : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Advance guest RIP past the current instruction.
//  On AMD this is done by writing VMCB.Save.Rip = VMCB.Control.NextRip.
//  If NextRip is 0 (e.g. after an #NPF), we compute it via RecoverInsnLength.
// ─────────────────────────────────────────────────────────────────────────────
static void AdvanceGuestRip(Vmcb* vmcb)
{
    if (vmcb->Control.NextRip != 0) {
        vmcb->Save.Rip = vmcb->Control.NextRip;
    } else {
        //
        // ★ Win11 AMD Fix #2 in action ★
        // NextRip is 0 (hardware didn't fill it — typical for #NPF).
        // Decode the instruction length and compute it manually.
        //
        const UINT32 len = RecoverInsnLength(vmcb);
        vmcb->Control.NextRip = vmcb->Save.Rip + len;
        vmcb->Save.Rip        = vmcb->Control.NextRip;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  CPUID emulation (same policy as Intel side)
// ─────────────────────────────────────────────────────────────────────────────
static void HandleCpuid(Vmcb* vmcb, GpRegs* Regs)
{
    const UINT32 leaf    = (UINT32)vmcb->Save.Rax;
    const UINT32 subleaf = (UINT32)Regs->Rcx;

    int regs[4] = {};
    __cpuidex(regs, (int)leaf, (int)subleaf);

    if (leaf == 1) {
        regs[2] &= ~(1 << 2);   // clear SVM bit so guest doesn't detect us
    } else if (leaf == 0x40000000) {
        regs[0] = 0x40000001;
        regs[1] = regs[2] = regs[3] = 0;
    }

    vmcb->Save.Rax = (UINT64)(UINT32)regs[0];
    Regs->Rbx      = (UINT64)(UINT32)regs[1];
    Regs->Rcx      = (UINT64)(UINT32)regs[2];
    Regs->Rdx      = (UINT64)(UINT32)regs[3];

    AdvanceGuestRip(vmcb);
}

// ─────────────────────────────────────────────────────────────────────────────
//  #NPF (Nested Page Fault) handler — the AMD SLAT hook toggle
//
//  NPF exit info:
//    ExitInfo1 = NPF error code (same layout as #PF error code + AMD extras)
//    ExitInfo2 = faulting guest physical address
//
//  Dual-NCR model (from hyper-reV):
//    HvNCR:   execute-only pages are NX=1 → causes #NPF on execution
//    HookNCR: shadow page for hooked GPA has NX=0; all others NX=1
//
//  On NPF due to EXECUTE fault from HvNCR:
//    → Switch NCr3 to HookNCR so guest executes shadow page
//  On NPF due to EXECUTE fault from HookNCR (non-hooked page):
//    → Switch back to HvNCR (guest left the hooked region)
//
//  After toggling NCr3 we DO NOT advance RIP — the guest re-executes the same
//  instruction under the new NCr3 mapping.
// ─────────────────────────────────────────────────────────────────────────────
static void HandleNpf(Vmcb* vmcb)
{
    const UINT64 npfInfo1 = vmcb->Control.ExitInfo1;
    const UINT64 faultGpa = vmcb->Control.ExitInfo2;

    // Only care about execute faults (bit 4 of error code)
    const bool isExecute = (npfInfo1 & (1ull << 4)) != 0;
    if (!isExecute) {
        // Read/write fault — can happen during hook setup or for other reasons.
        // Just let it through by keeping HvNCR active (no NCr3 change needed).
        return;
    }

    const UINT64 hvNcrPhys   = g_Hv.SlatRootPhys;
    const UINT64 hookNcrPhys = g_Hv.HookNcrPhys;

    if (vmcb->Control.NCr3 == hvNcrPhys) {
        // Execution hit a hooked (NX=1) page in HvNCR → switch to HookNCR
        vmcb->Control.NCr3 = hookNcrPhys;
    } else {
        // Execution hit an unhooked (NX=1) page in HookNCR → switch back to HvNCR
        vmcb->Control.NCr3 = hvNcrPhys;
    }

    // Flush TLB entries for this ASID so the NCr3 change takes effect
    vmcb->Control.TlbCtrl = TlbControl::FlushGuestTlb;

    UNREFERENCED_PARAMETER(faultGpa);

#ifdef HV_HYPERJACK_MODE
    //
    // ★ Win11 AMD Fix #1 in action (hyperjack mode only) ★
    //
    // We've done our job (toggled NCr3).  Now we MUST call the real hvax64
    // handler to keep the STIMER watchdog alive.  But hvax64 will read
    // VMCB.Control.ExitCode and try to use VMCB.Control.NextRip.
    //
    // Step A: Fix NextRip so hvax64 doesn't corrupt guest RIP.
    //   Hardware left NextRip=0 on #NPF.  Fill it now.
    //
    const UINT32 len = RecoverInsnLength(vmcb);
    vmcb->Control.NextRip = vmcb->Save.Rip + len;

    //
    // Step B: Fake the exit code to PAUSE (0x77).
    //   hvax64 sees a trivial PAUSE exit, runs its full epilogue:
    //     - STIMER accounting (HvRequestIdle tracking)
    //     - vCPU scheduler check
    //     - clock balance
    //   Then hvax64 writes VMCB.Save.Rip = VMCB.Control.NextRip (PAUSE rip+2)
    //   and does VMRUN.  This keeps the watchdog alive.
    //
    vmcb->Control.ExitCode = SVM_EXIT_PAUSE;

    //
    // Step C: Call hvax64's original handler.
    //
    if (g_SvmOriginalHandler)
        g_SvmOriginalHandler(0, 0, 0, 0);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  MSR exit handler
// ─────────────────────────────────────────────────────────────────────────────
static void HandleMsr(Vmcb* vmcb, GpRegs* Regs)
{
    const UINT32 msrIndex = (UINT32)Regs->Rcx;
    const bool   isWrite  = (vmcb->Control.ExitInfo1 & 1) != 0;

    if (isWrite) {
        const UINT64 val = ((Regs->Rdx & 0xFFFFFFFF) << 32)
                         | (vmcb->Save.Rax & 0xFFFFFFFF);
        __writemsr(msrIndex, val);
    } else {
        const UINT64 val = __readmsr(msrIndex);
        vmcb->Save.Rax  = val & 0xFFFFFFFF;
        Regs->Rdx       = val >> 32;
    }

    AdvanceGuestRip(vmcb);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main dispatcher — called from svm_asm.asm after every VMEXIT
//  Returns 0 → VMRUN again, 1 → stop
// ─────────────────────────────────────────────────────────────────────────────
extern "C" UINT64 SvmExitDispatch(GpRegs* Regs)
{
    Vmcb* vmcb = GetCurrentVmcb();
    if (!vmcb) return 1;   // should never happen

    const UINT64 exitCode = vmcb->Control.ExitCode;

    switch (exitCode)
    {
    case SVM_EXIT_CPUID:
        HandleCpuid(vmcb, Regs);
        return 0;

    case SVM_EXIT_VMMCALL:
        // Hypercall interface placeholder
        vmcb->Save.Rax = (UINT64)-1;
        AdvanceGuestRip(vmcb);
        return 0;

    case SVM_EXIT_NPF:
        HandleNpf(vmcb);
        return 0;

    case SVM_EXIT_MSR:
        HandleMsr(vmcb, Regs);
        return 0;

    case SVM_EXIT_INVD:
        __wbinvd();
        AdvanceGuestRip(vmcb);
        return 0;

    case SVM_EXIT_XSETBV:
        _xsetbv((UINT32)Regs->Rcx,
                ((Regs->Rdx & 0xFFFFFFFF) << 32) | (vmcb->Save.Rax & 0xFFFFFFFF));
        AdvanceGuestRip(vmcb);
        return 0;

    case SVM_EXIT_HLT:
        AdvanceGuestRip(vmcb);
        return 0;

    case SVM_EXIT_VMRUN:
    case SVM_EXIT_VMLOAD:
    case SVM_EXIT_VMSAVE:
    case SVM_EXIT_STGI:
    case SVM_EXIT_CLGI:
        // Nested SVM instructions — #UD or just advance, depending on policy
        AdvanceGuestRip(vmcb);
        return 0;

    case SVM_EXIT_INVALID:
        HV_LOG("[SVM] VMCB invalid on CPU %u", KeGetCurrentProcessorNumberEx(nullptr));
        return 1;   // stop

    default:
        // Unhandled — let guest continue
        break;
    }

    return 0;
}

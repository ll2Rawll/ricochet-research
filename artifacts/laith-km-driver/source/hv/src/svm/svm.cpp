#include "svm.h"
#include "vmcb_defs.h"
#include "../vmx/vmcs_defs.h"   // shared MSR indices (IA32_PAT, IA32_DEBUGCTL, etc.)
#include "../cpu/cpu.h"
#include "../common/memory.h"
#include "../npt/npt.h"

// ─────────────────────────────────────────────────────────────────────────────

static void SetupMsrPermissionMap(void* Bitmap)
{
    RtlZeroMemory(Bitmap, 0x2000);  // all-zero = full pass-through
}

// ─────────────────────────────────────────────────────────────────────────────

NTSTATUS Svm::Initialize(VcpuContext* Vcpu)
{
    // Enable SVM in EFER
    __writemsr(MSR_EFER, __readmsr(MSR_EFER) | EFER_SVME);

    // VMCB (1 page, physically contiguous)
    Vcpu->VmcbVa = Memory::AllocContiguous(PAGE_SIZE, &Vcpu->VmcbPhys);
    if (!Vcpu->VmcbVa) return STATUS_INSUFFICIENT_RESOURCES;

    // Host save area (CPU saves host state here on VMRUN)
    Vcpu->HostSaveAreaVa = Memory::AllocContiguous(PAGE_SIZE, &Vcpu->HostSaveAreaPhys);
    if (!Vcpu->HostSaveAreaVa) return STATUS_INSUFFICIENT_RESOURCES;
    __writemsr(MSR_VM_HSAVE_PA, Vcpu->HostSaveAreaPhys);

    // MSR permission map (8 KB)
    Vcpu->MsrBitmapVa = Memory::AllocContiguous(0x2000, &Vcpu->MsrBitmapPhys);
    if (!Vcpu->MsrBitmapVa) return STATUS_INSUFFICIENT_RESOURCES;
    SetupMsrPermissionMap(Vcpu->MsrBitmapVa);

    // Host stack
    Vcpu->HostStackBase = Memory::AllocContiguous(HOST_STACK_SIZE, nullptr);
    if (!Vcpu->HostStackBase) return STATUS_INSUFFICIENT_RESOURCES;

    const UINT64 stackTop = (UINT64)Vcpu->HostStackBase + HOST_STACK_SIZE;
    *reinterpret_cast<VcpuContext**>(stackTop - sizeof(UINT64)) = Vcpu;
    Vcpu->HostStackTop = stackTop - sizeof(UINT64);

    return STATUS_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fill a VmcbSeg using the Cpu:: helpers and MSVC intrinsics.
// No inline asm — segment selectors come from AsmReadXx() in vmx_asm.asm.
// ─────────────────────────────────────────────────────────────────────────────
static void FillVmcbSeg(VmcbSeg& seg, UINT16 selector)
{
    seg.Selector = selector;

    if (selector == 0) {
        // Null descriptor
        seg.Attrib = 0;
        seg.Limit  = 0;
        seg.Base   = 0;
        return;
    }

    // GetSegmentArFromSelector returns VMCS-format AR word [15:0].
    // VMCB attrib is the same bit layout in the low 12 bits:
    //   [7:0]  = descriptor byte 5 (type, S, DPL, P)
    //   [11:8] = descriptor byte 6 high nibble (AVL, L, D/B, G)
    const UINT32 ar    = Cpu::GetSegmentArFromSelector(selector);
    seg.Attrib = (UINT16)(ar & 0x0FFF);

    // __segmentlimit returns the expanded limit (byte granularity)
    seg.Limit = __segmentlimit(selector);

    seg.Base  = Cpu::GetSegmentBase(selector);
}

NTSTATUS Svm::LaunchVcpu(VcpuContext* Vcpu)
{
    auto* vmcb = static_cast<Vmcb*>(Vcpu->VmcbVa);

    // ── Control area ─────────────────────────────────────────────────────────
    // Intercept: CPUID(18), INVD(13), VMMCALL(VMRUN/VMLOAD/VMSAVE bits)
    vmcb->Control.InterceptMisc1 =
        (1u << 18) |    // CPUID
        (1u << 13);     // INVD

    vmcb->Control.InterceptMisc2 =
        (1u << 2) |     // VMRUN
        (1u << 3) |     // VMMCALL
        (1u << 4) |     // VMLOAD
        (1u << 5);      // VMSAVE

    vmcb->Control.InterceptException = 0;
    vmcb->Control.Asid               = 1;
    vmcb->Control.TlbCtrl            = TlbControl::FlushGuestTlb;
    vmcb->Control.MsrPmBasePa        = Vcpu->MsrBitmapPhys;
    vmcb->Control.NpEnable            = SVM_NP_ENABLE_NP;
    vmcb->Control.NCr3                = g_Hv.SlatRootPhys;

    // ── Guest state — read current CPU context ────────────────────────────────
    // Segment selectors via ASM helpers (x64 MSVC has no inline asm)
    const UINT16 cs_sel  = AsmReadCs();
    const UINT16 ss_sel  = AsmReadSs();
    const UINT16 ds_sel  = AsmReadDs();
    const UINT16 es_sel  = AsmReadEs();
    const UINT16 fs_sel  = AsmReadFs();
    const UINT16 gs_sel  = AsmReadGs();
    const UINT16 tr_sel  = AsmReadTr();
    const UINT16 ldt_sel = AsmReadLdtr();

    FillVmcbSeg(vmcb->Save.Cs,   cs_sel);
    FillVmcbSeg(vmcb->Save.Ss,   ss_sel);
    FillVmcbSeg(vmcb->Save.Ds,   ds_sel);
    FillVmcbSeg(vmcb->Save.Es,   es_sel);
    FillVmcbSeg(vmcb->Save.Fs,   fs_sel);
    FillVmcbSeg(vmcb->Save.Gs,   gs_sel);
    FillVmcbSeg(vmcb->Save.Tr,   tr_sel);
    FillVmcbSeg(vmcb->Save.Ldtr, ldt_sel);

#pragma pack(push, 1)
    struct GdtrIdtr { UINT16 Limit; UINT64 Base; };
#pragma pack(pop)
    GdtrIdtr gdtr = {}, idtr = {};
    AsmSgdt(&gdtr);
    AsmSidt(&idtr);

    vmcb->Save.Gdtr.Base  = gdtr.Base;
    vmcb->Save.Gdtr.Limit = gdtr.Limit;
    vmcb->Save.Idtr.Base  = idtr.Base;
    vmcb->Save.Idtr.Limit = idtr.Limit;

    vmcb->Save.Efer   = __readmsr(MSR_EFER) | EFER_SVME;
    vmcb->Save.Cr0    = __readcr0();
    vmcb->Save.Cr3    = __readcr3();
    vmcb->Save.Cr4    = __readcr4();
    vmcb->Save.Dr7    = __readdr(7);
    vmcb->Save.Rflags = AsmReadEflags();
    vmcb->Save.GPat   = __readmsr(IA32_PAT);
    vmcb->Save.DbgCtl = __readmsr(IA32_DEBUGCTL);
    vmcb->Save.SysenterCs  = __readmsr(IA32_SYSENTER_CS);
    vmcb->Save.SysenterEsp = __readmsr(IA32_SYSENTER_ESP);
    vmcb->Save.SysenterEip = __readmsr(IA32_SYSENTER_EIP);

    // Guest RAX / RSP / RIP are written by SvmLaunchAndCapture before VMRUN.
    // Pre-fill RAX here; RSP and RIP are set inside the ASM stub.
    vmcb->Save.Rax = 0;

    // Write RSP and RIP into VMCB now (the ASM stub will overwrite RSP/RIP
    // with the caller's live values just before VMRUN).
    // The ASM stub (SvmLaunchAndCapture) pre-fills these from the live stack.
    // We set SvmGuestEntry as the guest RIP so on successful VMRUN the guest
    // immediately returns 0.
    vmcb->Save.Rip = (UINT64)SvmGuestEntry;
    vmcb->Save.Rsp = Vcpu->HostStackTop;   // placeholder; ASM overwrites with live RSP

    const UINT64 result = SvmLaunchAndCapture(Vcpu->VmcbPhys);
    if (result != 0 && vmcb->Control.ExitCode == SVM_EXIT_INVALID) {
        HV_LOG("VMRUN failed CPU %u ExitCode=INVALID", Vcpu->Index);
        return STATUS_UNSUCCESSFUL;
    }

    Vcpu->Launched = true;
    return STATUS_SUCCESS;
}

void Svm::Shutdown(VcpuContext* Vcpu)
{
    if (Vcpu->Launched) {
        SvmOff();
        Vcpu->Launched = false;
    }
    if (Vcpu->VmcbVa)        { Memory::FreeContiguous(Vcpu->VmcbVa,        PAGE_SIZE);       Vcpu->VmcbVa = nullptr; }
    if (Vcpu->HostSaveAreaVa) { Memory::FreeContiguous(Vcpu->HostSaveAreaVa, PAGE_SIZE);      Vcpu->HostSaveAreaVa = nullptr; }
    if (Vcpu->MsrBitmapVa)   { Memory::FreeContiguous(Vcpu->MsrBitmapVa,   0x2000);          Vcpu->MsrBitmapVa = nullptr; }
    if (Vcpu->HostStackBase)  { Memory::FreeContiguous(Vcpu->HostStackBase, HOST_STACK_SIZE); Vcpu->HostStackBase = nullptr; }

    __writemsr(MSR_EFER, __readmsr(MSR_EFER) & ~EFER_SVME);
}

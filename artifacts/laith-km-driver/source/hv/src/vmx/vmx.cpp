#include "vmx.h"
#include "vmcs_defs.h"
#include <intrin.h>
#include "../cpu/cpu.h"
#include "../common/memory.h"
#include "../ept/ept.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static bool Vmwrite(UINT64 Field, UINT64 Value)
{
    return (__vmx_vmwrite(Field, Value) == 0);
}

static UINT64 Vmread(UINT64 Field)
{
    UINT64 val = 0;
    __vmx_vmread(Field, &val);
    return val;
}

#pragma pack(push, 1)
struct GdtrIdtr { UINT16 Limit; UINT64 Base; };
#pragma pack(pop)

// ─────────────────────────────────────────────────────────────────────────────
// MSR bitmap  –  all-zero = full pass-through
// ─────────────────────────────────────────────────────────────────────────────
static void SetupMsrBitmap(void* Bitmap)
{
    RtlZeroMemory(Bitmap, PAGE_SIZE);
}

// ─────────────────────────────────────────────────────────────────────────────

NTSTATUS Vmx::Initialize(VcpuContext* Vcpu)
{
    // Enable CR4.VMXE
    __writecr4(__readcr4() | (1ull << 13));

    // Satisfy FIXED0/FIXED1 CR0 requirements
    UINT64 cr0 = __readcr0();
    cr0 |= __readmsr(IA32_VMX_CR0_FIXED0);
    cr0 &= __readmsr(IA32_VMX_CR0_FIXED1);
    __writecr0(cr0);

    // VMXON region
    Vcpu->VmxonVa = Memory::AllocContiguous(PAGE_SIZE, &Vcpu->VmxonPhys);
    if (!Vcpu->VmxonVa) return STATUS_INSUFFICIENT_RESOURCES;

    const UINT32 revId = (UINT32)(__readmsr(IA32_VMX_BASIC) & 0x7FFFFFFF);
    *static_cast<UINT32*>(Vcpu->VmxonVa) = revId;

    UINT8 rc = __vmx_on(&Vcpu->VmxonPhys);
    if (rc != 0) {
        HV_LOG("VMXON failed CPU %u rc=%u", Vcpu->Index, rc);
        return STATUS_UNSUCCESSFUL;
    }

    // VMCS region
    Vcpu->VmcsVa = Memory::AllocContiguous(PAGE_SIZE, &Vcpu->VmcsPhys);
    if (!Vcpu->VmcsVa) { __vmx_off(); return STATUS_INSUFFICIENT_RESOURCES; }
    *static_cast<UINT32*>(Vcpu->VmcsVa) = revId;

    // MSR bitmap
    Vcpu->MsrBitmapVa = Memory::AllocContiguous(PAGE_SIZE, &Vcpu->MsrBitmapPhys);
    if (!Vcpu->MsrBitmapVa) { __vmx_off(); return STATUS_INSUFFICIENT_RESOURCES; }
    SetupMsrBitmap(Vcpu->MsrBitmapVa);

    // Host stack — top holds the VcpuContext* for the exit handler
    Vcpu->HostStackBase = Memory::AllocContiguous(HOST_STACK_SIZE, nullptr);
    if (!Vcpu->HostStackBase) { __vmx_off(); return STATUS_INSUFFICIENT_RESOURCES; }

    const UINT64 stackTop = (UINT64)Vcpu->HostStackBase + HOST_STACK_SIZE;
    // Store VcpuContext* in the topmost slot so the exit handler can retrieve it
    *reinterpret_cast<VcpuContext**>(stackTop - sizeof(UINT64)) = Vcpu;
    Vcpu->HostStackTop = stackTop - sizeof(UINT64);

    return STATUS_SUCCESS;
}

NTSTATUS Vmx::LaunchVcpu(VcpuContext* Vcpu)
{
    if (__vmx_vmclear(&Vcpu->VmcsPhys) != 0) return STATUS_UNSUCCESSFUL;
    if (__vmx_vmptrld(&Vcpu->VmcsPhys) != 0) return STATUS_UNSUCCESSFUL;

    // ── Read current CPU state using the ASM helpers (no inline asm in x64) ──
    const UINT16 cs   = AsmReadCs();
    const UINT16 ss   = AsmReadSs();
    const UINT16 ds   = AsmReadDs();
    const UINT16 es   = AsmReadEs();
    const UINT16 fs   = AsmReadFs();
    const UINT16 gs   = AsmReadGs();
    const UINT16 tr   = AsmReadTr();

    GdtrIdtr gdtr = {}, idtr = {};
    AsmSgdt(&gdtr);
    AsmSidt(&idtr);

    // ── Pin-based controls ────────────────────────────────────────────────────
    Vmwrite(VMCS_CTRL_PIN_EXEC,
            Cpu::AdjustControlValue(IA32_VMX_TRUE_PINBASED,
                PIN_NMI_EXITING | PIN_VIRTUAL_NMIS));

    // ── Processor-based controls ──────────────────────────────────────────────
    Vmwrite(VMCS_CTRL_PROC_EXEC,
            Cpu::AdjustControlValue(IA32_VMX_TRUE_PROCBASED,
                PROC_USE_MSR_BITMAP | PROC_ACTIVATE_SECONDARY | PROC_HLT_EXITING));

    // ── Secondary controls ────────────────────────────────────────────────────
    Vmwrite(VMCS_CTRL_SECONDARY_EXEC,
            Cpu::AdjustControlValue(IA32_VMX_PROCBASED_CTLS2,
                SECONDARY_ENABLE_EPT | SECONDARY_ENABLE_RDTSCP |
                SECONDARY_ENABLE_VPID | SECONDARY_ENABLE_XSAVES));

    // ── VM-exit controls ──────────────────────────────────────────────────────
    Vmwrite(VMCS_CTRL_EXIT_CTLS,
            Cpu::AdjustControlValue(IA32_VMX_TRUE_EXIT_CTLS,
                EXIT_HOST_ADDR_SPACE_64 | EXIT_SAVE_EFER |
                EXIT_LOAD_EFER | EXIT_SAVE_DEBUG_CTLS));

    // ── VM-entry controls ─────────────────────────────────────────────────────
    Vmwrite(VMCS_CTRL_ENTRY_CTLS,
            Cpu::AdjustControlValue(IA32_VMX_TRUE_ENTRY_CTLS,
                ENTRY_IA32E_GUEST | ENTRY_LOAD_EFER | ENTRY_LOAD_DEBUG_CTLS));

    // ── EPTP ──────────────────────────────────────────────────────────────────
    Vmwrite(VMCS_CTRL_EPTP,
            g_Hv.SlatRootPhys |
            ((UINT64)EPT_PAGEWALK_4 << 3) |
            (UINT64)EPT_MEMORY_TYPE_WB);

    // VPID (must not be 0)
    Vmwrite(0x0000 /* VMCS_CTRL_VPID */, (UINT64)(Vcpu->Index + 1));

    // MSR bitmap / exception bitmap
    Vmwrite(VMCS_CTRL_MSR_BITMAP,      Vcpu->MsrBitmapPhys);
    Vmwrite(VMCS_CTRL_EXCEPTION_BITMAP, 0);
    Vmwrite(VMCS_CTRL_CR3_TARGET_COUNT, 0);
    Vmwrite(VMCS_GUEST_VMCS_LINK_PTR,  ~0ull);

    // ── Host state ────────────────────────────────────────────────────────────
    Vmwrite(VMCS_HOST_CR0,    __readcr0());
    Vmwrite(VMCS_HOST_CR3,    __readcr3());
    Vmwrite(VMCS_HOST_CR4,    __readcr4());

    Vmwrite(VMCS_HOST_ES_SEL, es & 0xF8u);
    Vmwrite(VMCS_HOST_CS_SEL, cs & 0xF8u);
    Vmwrite(VMCS_HOST_SS_SEL, ss & 0xF8u);
    Vmwrite(VMCS_HOST_DS_SEL, ds & 0xF8u);
    Vmwrite(VMCS_HOST_FS_SEL, fs & 0xF8u);
    Vmwrite(VMCS_HOST_GS_SEL, gs & 0xF8u);
    Vmwrite(VMCS_HOST_TR_SEL, tr & 0xF8u);

    Vmwrite(VMCS_HOST_FS_BASE,   __readmsr(IA32_FS_BASE));
    Vmwrite(VMCS_HOST_GS_BASE,   __readmsr(IA32_GS_BASE));
    Vmwrite(VMCS_HOST_TR_BASE,   Cpu::GetSegmentBase(tr));
    Vmwrite(VMCS_HOST_GDTR_BASE, gdtr.Base);
    Vmwrite(VMCS_HOST_IDTR_BASE, idtr.Base);
    Vmwrite(VMCS_HOST_SYSENTER_CS,  __readmsr(IA32_SYSENTER_CS));
    Vmwrite(VMCS_HOST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));
    Vmwrite(VMCS_HOST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
    Vmwrite(VMCS_HOST_EFER, __readmsr(IA32_EFER));
    Vmwrite(VMCS_HOST_PAT,  __readmsr(IA32_PAT));
    Vmwrite(VMCS_HOST_RSP,  Vcpu->HostStackTop);
    Vmwrite(VMCS_HOST_RIP,  (UINT64)VmxHostEntryPoint);

    // ── Guest state (snapshot of current register state) ─────────────────────
    Vmwrite(VMCS_GUEST_CR0,    __readcr0());
    Vmwrite(VMCS_GUEST_CR3,    __readcr3());
    Vmwrite(VMCS_GUEST_CR4,    __readcr4());
    Vmwrite(VMCS_GUEST_DR7,    __readdr(7));
    Vmwrite(VMCS_GUEST_RFLAGS, AsmReadEflags());
    Vmwrite(VMCS_GUEST_DEBUGCTL, __readmsr(IA32_DEBUGCTL));
    Vmwrite(VMCS_GUEST_EFER,    __readmsr(IA32_EFER));
    Vmwrite(VMCS_GUEST_PAT,     __readmsr(IA32_PAT));
    Vmwrite(VMCS_GUEST_SYSENTER_CS,  __readmsr(IA32_SYSENTER_CS));
    Vmwrite(VMCS_GUEST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));
    Vmwrite(VMCS_GUEST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
    Vmwrite(VMCS_GUEST_FS_BASE, __readmsr(IA32_FS_BASE));
    Vmwrite(VMCS_GUEST_GS_BASE, __readmsr(IA32_GS_BASE));

    // Guest selectors
    Vmwrite(VMCS_GUEST_ES_SEL,   es);   Vmwrite(VMCS_GUEST_CS_SEL, cs);
    Vmwrite(VMCS_GUEST_SS_SEL,   ss);   Vmwrite(VMCS_GUEST_DS_SEL, ds);
    Vmwrite(VMCS_GUEST_FS_SEL,   fs);   Vmwrite(VMCS_GUEST_GS_SEL, gs);
    Vmwrite(VMCS_GUEST_TR_SEL,   tr);   Vmwrite(VMCS_GUEST_LDTR_SEL, 0);

    // Guest bases
    Vmwrite(VMCS_GUEST_ES_BASE, 0); Vmwrite(VMCS_GUEST_CS_BASE, 0);
    Vmwrite(VMCS_GUEST_SS_BASE, 0); Vmwrite(VMCS_GUEST_DS_BASE, 0);
    Vmwrite(VMCS_GUEST_TR_BASE,    Cpu::GetSegmentBase(tr));
    Vmwrite(VMCS_GUEST_LDTR_BASE,  0);
    Vmwrite(VMCS_GUEST_GDTR_BASE,  gdtr.Base);
    Vmwrite(VMCS_GUEST_IDTR_BASE,  idtr.Base);

    // Guest limits
    Vmwrite(VMCS_GUEST_ES_LIMIT,   __segmentlimit(es));
    Vmwrite(VMCS_GUEST_CS_LIMIT,   __segmentlimit(cs));
    Vmwrite(VMCS_GUEST_SS_LIMIT,   __segmentlimit(ss));
    Vmwrite(VMCS_GUEST_DS_LIMIT,   __segmentlimit(ds));
    Vmwrite(VMCS_GUEST_FS_LIMIT,   __segmentlimit(fs));
    Vmwrite(VMCS_GUEST_GS_LIMIT,   __segmentlimit(gs));
    Vmwrite(VMCS_GUEST_TR_LIMIT,   __segmentlimit(tr));
    Vmwrite(VMCS_GUEST_LDTR_LIMIT, 0xFFFF);
    Vmwrite(VMCS_GUEST_GDTR_LIMIT, gdtr.Limit);
    Vmwrite(VMCS_GUEST_IDTR_LIMIT, idtr.Limit);

    // Guest access rights
    Vmwrite(VMCS_GUEST_ES_AR,   Cpu::GetSegmentArFromSelector(es));
    Vmwrite(VMCS_GUEST_CS_AR,   Cpu::GetSegmentArFromSelector(cs));
    Vmwrite(VMCS_GUEST_SS_AR,   Cpu::GetSegmentArFromSelector(ss));
    Vmwrite(VMCS_GUEST_DS_AR,   Cpu::GetSegmentArFromSelector(ds));
    Vmwrite(VMCS_GUEST_FS_AR,   Cpu::GetSegmentArFromSelector(fs));
    Vmwrite(VMCS_GUEST_GS_AR,   Cpu::GetSegmentArFromSelector(gs));
    Vmwrite(VMCS_GUEST_TR_AR,   Cpu::GetSegmentArFromSelector(tr));
    Vmwrite(VMCS_GUEST_LDTR_AR, 0x10000);   // unusable

    Vmwrite(VMCS_GUEST_ACTIVITY_STATE,   0);
    Vmwrite(VMCS_GUEST_INTERRUPTIBILITY, 0);

    // Guest RSP / RIP are written inside VmxLaunchAndCapture() in the ASM stub
    const UINT64 result = VmxLaunchAndCapture();
    if (result != 0) {
        const UINT32 err = (UINT32)Vmread(VMCS_RO_INSN_ERROR);
        HV_LOG("VMLAUNCH failed CPU %u RFLAGS=0x%llX err=%u", Vcpu->Index, result, err);
        return STATUS_UNSUCCESSFUL;
    }

    Vcpu->Launched = true;
    return STATUS_SUCCESS;
}

void Vmx::Shutdown(VcpuContext* Vcpu)
{
    if (Vcpu->Launched) {
        AsmVmxOff();
        Vcpu->Launched = false;
    }
    if (Vcpu->VmxonVa)      { Memory::FreeContiguous(Vcpu->VmxonVa,      PAGE_SIZE);       Vcpu->VmxonVa = nullptr; }
    if (Vcpu->VmcsVa)       { Memory::FreeContiguous(Vcpu->VmcsVa,       PAGE_SIZE);       Vcpu->VmcsVa = nullptr; }
    if (Vcpu->MsrBitmapVa)  { Memory::FreeContiguous(Vcpu->MsrBitmapVa,  PAGE_SIZE);       Vcpu->MsrBitmapVa = nullptr; }
    if (Vcpu->HostStackBase) { Memory::FreeContiguous(Vcpu->HostStackBase, HOST_STACK_SIZE); Vcpu->HostStackBase = nullptr; }
}

struct InveptDescriptor { UINT64 Eptp; UINT64 Reserved; };

void Vmx::InvEpt(UINT64 EptpPhys)
{
    InveptDescriptor desc = { EptpPhys, 0 };
    AsmInvept(1, &desc);
}

void Vmx::InvVpid(UINT16 Vpid, UINT64 Gva)
{
    struct { UINT16 Vpid; UINT16 R[3]; UINT64 Gva; } desc = { Vpid, {}, Gva };
    AsmInvvpid(1, &desc);
}

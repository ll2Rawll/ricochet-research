#include "cpu.h"
#include "../hv.h"

// ── CPUID leaf / bit definitions ─────────────────────────────────────────────
#define CPUID_VENDOR                0x00000000
#define CPUID_FEATURES              0x00000001
#define CPUID_EXT_FEATURES          0x80000001

// Intel feature bits
#define CPUID_ECX_VMX               (1u << 5)

// AMD feature bits
#define CPUID_ECX_SVM               (1u << 2)

// MSR indices
#define IA32_FEATURE_CONTROL        0x3A
#define IA32_VMX_BASIC              0x480
#define IA32_VMX_PINBASED_CTLS      0x481
#define IA32_VMX_PROCBASED_CTLS     0x482
#define IA32_VMX_EXIT_CTLS          0x483
#define IA32_VMX_ENTRY_CTLS         0x484
#define IA32_VMX_TRUE_PINBASED      0x48D
#define IA32_VMX_TRUE_PROCBASED     0x48E
#define IA32_VMX_TRUE_EXIT_CTLS     0x48F
#define IA32_VMX_TRUE_ENTRY_CTLS    0x490

#define MSR_EFER                    0xC0000080
#define EFER_SVME                   (1ull << 12)

#define MSR_VM_CR                   0xC0010114
#define VM_CR_SVMDIS                (1u << 4)

#pragma pack(push, 1)
struct SegDescriptor {
    UINT16 LimitLow;
    UINT16 BaseLow;
    UINT8  BaseMid;
    UINT8  Access;
    UINT8  GranLimit;
    UINT8  BaseHigh;
};
struct SegDescriptor64 {
    SegDescriptor Desc;
    UINT32        BaseUpper;
    UINT32        Reserved;
};
#pragma pack(pop)

CpuVendor Cpu::DetectVendor()
{
    int regs[4] = {};
    __cpuid(regs, CPUID_VENDOR);
    // EBX, EDX, ECX hold the 12-char vendor string
    // Intel: "GenuineIntel"  AMD: "AuthenticAMD"
    if (regs[1] == 'uneG' && regs[3] == 'Ieni' && regs[2] == 'letn')
        return CpuVendor::Intel;
    if (regs[1] == 'htuA' && regs[3] == 'itne' && regs[2] == 'DMAc')
        return CpuVendor::AMD;
    return CpuVendor::Unknown;
}

bool Cpu::VmxSupported()
{
    // 1. CPUID.01H:ECX.VMX[bit 5] must be set
    int regs[4] = {};
    __cpuid(regs, CPUID_FEATURES);
    if (!(regs[2] & CPUID_ECX_VMX))
        return false;

    // 2. IA32_FEATURE_CONTROL must allow VMX outside SMX
    const UINT64 fc = __readmsr(IA32_FEATURE_CONTROL);
    if (!(fc & 0x1))          return false;  // lock bit must be set
    if (!(fc & 0x4))          return false;  // VMX outside SMX enable

    return true;
}

bool Cpu::SvmSupported()
{
    // 1. CPUID.80000001H:ECX.SVM[bit 2]
    int regs[4] = {};
    __cpuid(regs, CPUID_EXT_FEATURES);
    if (!(regs[2] & CPUID_ECX_SVM))
        return false;

    // 2. VM_CR.SVMDIS must NOT be set
    const UINT64 vmcr = __readmsr(MSR_VM_CR);
    if (vmcr & VM_CR_SVMDIS)
        return false;

    return true;
}

UINT64 Cpu::GetSegmentBase(UINT16 Selector)
{
    if (Selector == 0)
        return 0;

    struct { UINT16 Limit; UINT64 Base; } gdtr = {};
    AsmSgdt(&gdtr);

    const UINT16 index = (Selector >> 3) & 0x1FFF;
    const UINT64 offset = (UINT64)index * 8;
    if (offset + 8 > (UINT64)gdtr.Limit)
        return 0;

    const auto* d = reinterpret_cast<const SegDescriptor*>(gdtr.Base + offset);
    UINT64 base = (UINT64)d->BaseLow
                | ((UINT64)d->BaseMid  << 16)
                | ((UINT64)d->BaseHigh << 24);

    // System (S=0) descriptors with type ≥ 4 are 16-byte in 64-bit mode
    if (!(d->Access & 0x10)) {
        const auto* d64 = reinterpret_cast<const SegDescriptor64*>(d);
        base |= ((UINT64)d64->BaseUpper << 32);
    }
    return base;
}

UINT32 Cpu::GetSegmentArFromSelector(UINT16 Selector)
{
    if (Selector == 0)
        return 0x10000;     // unusable

    struct { UINT16 Limit; UINT64 Base; } gdtr = {};
    AsmSgdt(&gdtr);

    const UINT16 index = (Selector >> 3) & 0x1FFF;
    const UINT64 offset = (UINT64)index * 8;
    if (offset + 8 > (UINT64)gdtr.Limit)
        return 0x10000;

    const auto* d = reinterpret_cast<const SegDescriptor*>(gdtr.Base + offset);
    const UINT8  access    = d->Access;
    const UINT8  granLimit = d->GranLimit;

    // Build the VMCS AR field (bits [15:0] from descriptor, unusable in [16])
    UINT32 ar = 0;
    ar |= (access    & 0xFF);
    ar |= ((UINT32)(granLimit & 0xF0) << 8);    // G, D/B, L, AVL are in high nibble
    return ar;
}

UINT32 Cpu::AdjustControlValue(UINT32 CtlMsr, UINT32 Desired)
{
    const UINT64 msr = __readmsr(CtlMsr);
    // Bits set in low  32 bits must be 1
    // Bits clear in high 32 bits must be 0
    Desired |= (UINT32)(msr & 0xFFFFFFFF);
    Desired &= (UINT32)(msr >> 32);
    return Desired;
}

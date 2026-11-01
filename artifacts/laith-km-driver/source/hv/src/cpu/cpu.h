#pragma once
#include <ntddk.h>
#include <intrin.h>
#include "../hv.h"

namespace Cpu
{
    //
    // Detect which vendor is present.
    //
    CpuVendor DetectVendor();

    //
    // Returns true if VMX is available and can be enabled on the current core.
    //
    bool VmxSupported();

    //
    // Returns true if SVM is available and can be enabled on the current core.
    //
    bool SvmSupported();

    //
    // Read a segment base from the current GDT.
    //
    UINT64 GetSegmentBase(UINT16 Selector);

    //
    // Read the full segment access-rights dword for VMCS guest-state.
    //
    UINT32 GetSegmentArFromSelector(UINT16 Selector);

    //
    // Adjust a control MSR value using the allowed-0 / allowed-1 masks
    // (IA32_VMX_TRUE_*_CTLS or IA32_VMX_*_CTLS).
    //
    UINT32 AdjustControlValue(UINT32 CtlMsr, UINT32 Desired);
}

//
// ─── ASM segment-selector helpers (vmx_asm.asm) ──────────────────────────────
// x64 MSVC has no inline assembly.  These tiny procs read CPU registers that
// have no MSVC intrinsic equivalent.
//
extern "C" {
    UINT16 AsmReadCs();
    UINT16 AsmReadSs();
    UINT16 AsmReadDs();
    UINT16 AsmReadEs();
    UINT16 AsmReadFs();
    UINT16 AsmReadGs();
    UINT16 AsmReadTr();
    UINT16 AsmReadLdtr();
    UINT64 AsmReadEflags();
    void   AsmSgdt(void* buf);   // stores 10-byte GDTR at buf
    void   AsmSidt(void* buf);   // stores 10-byte IDTR at buf
    void   AsmInvept(UINT64 type, void* desc);
    void   AsmInvvpid(UINT64 type, void* desc);
}

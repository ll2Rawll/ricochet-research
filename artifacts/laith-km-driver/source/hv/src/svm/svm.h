#pragma once
#include <ntddk.h>
#include "../hv.h"
#include "vmcb_defs.h"

// Exported from svm_asm.asm
extern "C" {
    //
    // Launch trampoline: C caller pre-fills VMCB.Save.Rip/Rsp, then calls
    // this which puts VmcbPhys in RAX and executes VMRUN.
    // Returns 0 (from SvmGuestEntry) on success, 1 on VMRUN failure.
    //
    UINT64 SvmLaunchAndCapture(UINT64 VmcbPhys);

    //
    // Resume point for the guest after a successful VMRUN (returns 0).
    //
    UINT64 SvmGuestEntry();

    //
    // VMRUN loop entry: RCX = VMCB phys.  Loops until SvmExitDispatch
    // returns non-zero, then returns to caller.
    //
    void SvmHostEntryPoint(UINT64 VmcbPhys);

    //
    // Disable SVM on the current CPU (clears EFER.SVME).
    //
    void SvmOff();
}

namespace Svm
{
    NTSTATUS Initialize(VcpuContext* Vcpu);
    NTSTATUS LaunchVcpu(VcpuContext* Vcpu);
    void     Shutdown(VcpuContext* Vcpu);
}

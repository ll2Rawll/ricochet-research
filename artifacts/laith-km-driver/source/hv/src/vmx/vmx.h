#pragma once
#include <ntddk.h>
#include "../hv.h"

// Exported from vmx_asm.asm
extern "C" {
    //
    // Launch trampoline: writes current RSP + &VmxGuestEntry into VMCS,
    // then VMLAUNCH.  Returns 0 on success (via VmxGuestEntry), or RFLAGS
    // (non-zero) on failure.
    //
    UINT64 VmxLaunchAndCapture();

    //
    // Resume point for the guest after VMLAUNCH succeeds (returns 0).
    //
    UINT64 VmxGuestEntry();

    //
    // Host VM-exit entry point (loaded as HOST_RIP into the VMCS).
    //
    void VmxHostEntryPoint();

    //
    // Shut down VMX on the current logical processor.
    //
    void AsmVmxOff();
}

namespace Vmx
{
    //
    // Check support and enable VMX on the current CPU.
    //
    NTSTATUS Initialize(VcpuContext* Vcpu);

    //
    // Set up VMCS fields and launch the VM.
    // On success returns STATUS_SUCCESS and the caller is now a guest.
    //
    NTSTATUS LaunchVcpu(VcpuContext* Vcpu);

    //
    // VMXOFF + teardown on the current CPU.
    //
    void Shutdown(VcpuContext* Vcpu);

    //
    // Invalidate EPT caches on the current CPU (single-context INVEPT).
    //
    void InvEpt(UINT64 EptpPhys);

    //
    // Invalidate VPID on the current CPU.
    //
    void InvVpid(UINT16 Vpid, UINT64 Gva);
}

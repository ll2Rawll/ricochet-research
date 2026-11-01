#pragma once
#include <ntddk.h>
#include "../hv.h"

// ─────────────────────────────────────────────────────────────────────────────
// Magic CPUID leaf used to issue hypercalls to our parasite.
// Any guest usermode / kernel code that does CPUID with this leaf will have
// its call intercepted by VmxHyperjackDispatch instead of being forwarded.
// ─────────────────────────────────────────────────────────────────────────────
#define HV_HYPERCALL_MAGIC_LEAF   0x4C4B4849u   // 'HIKL'

// Sub-operations in RCX when CPUID leaf == HV_HYPERCALL_MAGIC_LEAF
#define HV_OP_GET_CR3             0x01u
#define HV_OP_READ_PHYS           0x02u
#define HV_OP_WRITE_PHYS          0x03u

// ─────────────────────────────────────────────────────────────────────────────
// Jump targets for the ASM hook stubs — set by HvHook::Initialize().
// Defined in hv_hook_asm.asm (.DATA section) so the ASM can reference them
// without a PROC-style EXTERN.
// ─────────────────────────────────────────────────────────────────────────────
extern "C" {
    // Pointer to the trampoline buffer that executes original bytes + JMP back
    extern void* g_VmxTrampolineTarget;   // Intel
    extern void* g_SvmTrampolineTarget;   // AMD (stub, not used on Intel)
}

// ─────────────────────────────────────────────────────────────────────────────
// Hyperjack namespace
// ─────────────────────────────────────────────────────────────────────────────
namespace HvHook {

    // True after a successful Initialize() — driver reads this to skip
    // its own VMLAUNCH/VMRUN path.
    extern bool Active;

    // Detect Microsoft Hyper-V running as L1 hypervisor.
    bool IsHyperVPresent();

    // Hook hvix64 / hvax64's VM-exit handler via physical-memory write.
    // Returns STATUS_SUCCESS if the hook was installed and hyperjack is live.
    // Returns STATUS_NOT_SUPPORTED / STATUS_NOT_FOUND if the system is not
    // running Hyper-V or the exit handler could not be located.
    // On failure the caller should proceed with standalone VMX/SVM mode.
    NTSTATUS Initialize();

    // Remove the hook, restore original bytes, free trampoline buffer.
    void Shutdown();

} // namespace HvHook

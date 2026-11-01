#pragma once
#include <ntddk.h>
#include "../hv.h"

//
// Called from vmx_asm.asm on every VM-exit (standalone mode).
// Returns 0 to VMRESUME, 1 to trigger shutdown.
//
extern "C" UINT64 VmxExitDispatch(GpRegs* Regs);

//
// Called from hv_hook_asm.asm on every VM-exit in HYPERJACK mode.
// Hyper-V still owns VMRESUME — we just handle our exits then hand back.
//
extern "C" void VmxHyperjackDispatch(GpRegs* HostRegs);

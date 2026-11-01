#pragma once
#include <ntddk.h>
#include "../hv.h"
#include "vmcb_defs.h"

//
// Called from svm_asm.asm on every VMEXIT.
// Regs points to the GPR save frame built on the host stack.
// Returns 0 → loop back to VMRUN, 1 → stop (shutdown).
//
extern "C" UINT64 SvmExitDispatch(GpRegs* Regs);

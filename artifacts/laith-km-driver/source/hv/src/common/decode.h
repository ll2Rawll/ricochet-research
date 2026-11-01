#pragma once
#include <ntddk.h>

//
// Minimal x86-64 instruction-length decoder.
//
// This is used on AMD to compute VMCB.control.next_rip when the hardware
// leaves it blank on a #NPF (Nested Page Fault) exit — a known gap:
//   hardware only fills next_rip for synchronous exits (CPUID, VMMCALL…),
//   NOT for faults like #NPF.  Without a valid next_rip, faking a PAUSE
//   exit to hvax64 or advancing past a faulting instruction is impossible.
//
// The decoder handles the full Intel 64 prefix / opcode / ModRM / SIB /
// disp / imm pipeline, covering all instruction encodings a guest OS would
// realistically touch for memory operations.
//
namespace Decode
{
    //
    // Returns the byte-length of the x86-64 instruction at `InsnBytes`.
    // `MaxBytes` is the upper bound of readable bytes (typically 15).
    // Returns 0 if the stream cannot be decoded.
    //
    UINT32 InsnLength(const UINT8* InsnBytes, UINT32 MaxBytes = 15);
}

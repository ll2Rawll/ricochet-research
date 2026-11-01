#pragma once
#include <ntddk.h>

namespace Memory
{
    //
    // Map a host physical address into kernel virtual space.
    // Returns nullptr on failure.
    //
    void* MapPhysical(UINT64 Phys, SIZE_T Size);

    //
    // Unmap a previously mapped region.
    //
    void  UnmapPhysical(void* Va, SIZE_T Size);

    //
    // Translate a virtual address using a given CR3.
    // Returns the physical address, or 0 on fault.
    //
    UINT64 WalkPageTable(UINT64 Cr3, UINT64 Va);

    //
    // Allocate a contiguous physically-backed page.
    // Returns both VA and PA.
    //
    void* AllocContiguous(SIZE_T Size, UINT64* OutPhys);

    //
    // Free a contiguous allocation.
    //
    void  FreeContiguous(void* Va, SIZE_T Size);

    //
    // Allocate a non-paged pool page, zero it, return VA + PA.
    //
    void* AllocNonPaged(SIZE_T Size, UINT64* OutPhys);

    //
    // Free a non-paged pool allocation.
    //
    void  FreeNonPaged(void* Va);

    //
    // Get the physical address of a kernel VA.
    //
    UINT64 VaToPa(void* Va);
}

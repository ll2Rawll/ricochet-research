#pragma once
#include <ntddk.h>
#include "../hv.h"

//
// ─── EPT paging-structure entry ───────────────────────────────────────────────
//
#pragma pack(push, 8)
union EptEntry {
    UINT64 Value;
    struct {
        UINT64 Read       : 1;
        UINT64 Write      : 1;
        UINT64 Execute    : 1;
        UINT64 MemType    : 3;    // for PTEs: EPT_MEMORY_TYPE_WB
        UINT64 IgnorePat  : 1;
        UINT64 LargePage  : 1;    // 1 = 2MB (PDE) or 1GB (PDPTE)
        UINT64 Accessed   : 1;
        UINT64 Dirty      : 1;
        UINT64 ExecUser   : 1;    // user-mode execute (if mode-based x only)
        UINT64 Res0       : 1;
        UINT64 Pfn        : 40;
        UINT64 Res1       : 11;
        UINT64 SuppVe     : 1;    // suppress #VE
    };
};
#pragma pack(pop)

static_assert(sizeof(EptEntry) == 8, "EptEntry must be 8 bytes");

namespace Ept
{
    //
    // Build the identity-mapped EPT from MmGetPhysicalMemoryRanges().
    // Uses 2 MB large pages for the initial build.
    //
    NTSTATUS Initialize(UINT64* OutRootPhys, void** OutRootVa);

    //
    // Tear down all EPT structures.
    //
    void Destroy(UINT64 RootPhys, void* RootVa);

    //
    // Add a shadow-page hook on the 4 KB page at TargetGpa.
    // ShadowHpa is the host physical address of the shadow copy
    // (your patched/instrumented page).
    //
    NTSTATUS AddHook(void* RootVa, UINT64 TargetGpa, UINT64 ShadowHpa);

    //
    // Remove an existing hook on TargetGpa (merges 4 KB PTEs back to 2 MB).
    //
    NTSTATUS RemoveHook(void* RootVa, UINT64 TargetGpa);

    //
    // Replace the PFN of a GPA with the dummy page (hides it from guest).
    //
    NTSTATUS HidePage(void* RootVa, UINT64 TargetGpa);

    //
    // Walk the EPT and return the current PTE pointer for a GPA.
    // Split any large page as needed.
    //
    EptEntry* GetOrSplitPte(void* RootVa, UINT64 Gpa, bool Split);
}

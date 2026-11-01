#pragma once
#include <ntddk.h>
#include "../hv.h"

//
// ─── NPT paging-structure entry ──────────────────────────────────────────────
// NPT (Nested Page Tables) uses the exact same layout as regular x86-64
// page tables but controls guest-physical → host-physical mappings.
//
#pragma pack(push, 8)
union NptEntry {
    UINT64 Value;
    struct {
        UINT64 Present    : 1;
        UINT64 Write      : 1;
        UINT64 User       : 1;
        UINT64 Pwt        : 1;
        UINT64 Pcd        : 1;
        UINT64 Accessed   : 1;
        UINT64 Dirty      : 1;
        UINT64 LargePage  : 1;
        UINT64 Global     : 1;
        UINT64 Avl        : 3;
        UINT64 Pfn        : 40;
        UINT64 Res0       : 11;
        UINT64 Nx         : 1;   // No-execute bit
    };
};
#pragma pack(pop)

static_assert(sizeof(NptEntry) == 8, "NptEntry must be 8 bytes");

namespace Npt
{
    //
    // Build the identity-mapped nested page table (2 MB large pages).
    // Creates BOTH the Hyper-V nested CR3 and the hook nested CR3.
    //
    NTSTATUS Initialize(UINT64* OutHvNcrPhys, void** OutHvNcrVa,
                        UINT64* OutHookNcrPhys, void** OutHookNcrVa);

    void Destroy(void* HvNcrVa, void* HookNcrVa);

    //
    // Add an NPT hook:
    //   • In HvNCR   : mark TargetGpa as non-executable
    //   • In HookNCR : map TargetGpa's PTEs to ShadowHpa with NX=0
    //                  (all other pages in HookNCR are NX=1)
    //
    NTSTATUS AddHook(void* HvNcrVa, void* HookNcrVa,
                     UINT64 TargetGpa, UINT64 ShadowHpa);

    NTSTATUS RemoveHook(void* HvNcrVa, void* HookNcrVa, UINT64 TargetGpa);

    NTSTATUS HidePage(void* HvNcrVa, void* HookNcrVa, UINT64 TargetGpa);

    //
    // Walk the NPT (either HvNCR or HookNCR) and return the PTE for a GPA.
    //
    NptEntry* GetPte(void* NcrVa, UINT64 Gpa, bool SplitLarge);
}

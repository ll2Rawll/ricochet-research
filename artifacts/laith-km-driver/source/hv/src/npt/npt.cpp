#include "npt.h"
#include "../common/memory.h"

// ─────────────────────────────────────────────────────────────────────────────
// AMD NPT hook model (from the hyper-reV design):
//
//   HvNCR (Hyper-V nested CR3) – normal execution:
//     All pages: present, rw, BUT NX=1 on hooked pages → causes #NPF on exec
//
//   HookNCR (hook nested CR3) – activated on #NPF exec fault:
//     Non-hooked pages: NX=1 (will cause #NPF once we leave the hooked page)
//     Hooked page: NX=0, PFN = shadow page → guest executes shadow
//
// The exit handler toggles the VMCB.NCr3 between HvNCR and HookNCR.
// ─────────────────────────────────────────────────────────────────────────────

#define NPT_ENTRY_COUNT     512
#define NPT_TABLE_SIZE      (NPT_ENTRY_COUNT * sizeof(NptEntry))

static UINT64 g_NptDummyPhys = 0;

static NptEntry* AllocNptTable(UINT64* OutPhys)
{
    return static_cast<NptEntry*>(Memory::AllocContiguous(NPT_TABLE_SIZE, OutPhys));
}

// Build a 4-level identity-mapped NCR for physical memory ranges.
// If NxAll = true, all pages are NX=1 (used as the starting state for HookNCR).
static NTSTATUS BuildNcr(UINT64* OutPhys, void** OutVa, bool NxAll)
{
    UINT64 pml4Phys = 0;
    NptEntry* pml4 = AllocNptTable(&pml4Phys);
    if (!pml4) return STATUS_INSUFFICIENT_RESOURCES;

    PPHYSICAL_MEMORY_RANGE ranges = MmGetPhysicalMemoryRanges();
    if (!ranges) { Memory::FreeContiguous(pml4, NPT_TABLE_SIZE); return STATUS_INSUFFICIENT_RESOURCES; }

    for (UINT32 r = 0; ; ++r) {
        if (!ranges[r].BaseAddress.QuadPart && !ranges[r].NumberOfBytes.QuadPart) break;

        UINT64 base = (UINT64)ranges[r].BaseAddress.QuadPart & ~0x1FFFFFull;
        UINT64 end  = ((UINT64)ranges[r].BaseAddress.QuadPart
                        + (UINT64)ranges[r].NumberOfBytes.QuadPart + 0x1FFFFFull) & ~0x1FFFFFull;

        for (UINT64 addr = base; addr < end; addr += 0x200000ULL) {
            const UINT32 i4 = (addr >> 39) & 0x1FF;
            const UINT32 i3 = (addr >> 30) & 0x1FF;
            const UINT32 i2 = (addr >> 21) & 0x1FF;

            if (!pml4[i4].Present) {
                UINT64 pdptPhys = 0;
                NptEntry* pdpt = AllocNptTable(&pdptPhys);
                if (!pdpt) { ExFreePool(ranges); return STATUS_INSUFFICIENT_RESOURCES; }
                pml4[i4].Value   = 0;
                pml4[i4].Present = 1;
                pml4[i4].Write   = 1;
                pml4[i4].User    = 1;
                pml4[i4].Pfn     = pdptPhys >> 12;
            }

            auto* pdpt = static_cast<NptEntry*>(Memory::MapPhysical(pml4[i4].Pfn << 12, NPT_TABLE_SIZE));
            if (!pdpt) { ExFreePool(ranges); return STATUS_INSUFFICIENT_RESOURCES; }

            if (!pdpt[i3].Present) {
                UINT64 pdPhys = 0;
                NptEntry* pd = AllocNptTable(&pdPhys);
                if (!pd) { Memory::UnmapPhysical(pdpt, NPT_TABLE_SIZE); ExFreePool(ranges); return STATUS_INSUFFICIENT_RESOURCES; }
                pdpt[i3].Value   = 0;
                pdpt[i3].Present = 1;
                pdpt[i3].Write   = 1;
                pdpt[i3].User    = 1;
                pdpt[i3].Pfn     = pdPhys >> 12;
            }
            UINT64 pdPhys = pdpt[i3].Pfn << 12;
            Memory::UnmapPhysical(pdpt, NPT_TABLE_SIZE);

            auto* pd = static_cast<NptEntry*>(Memory::MapPhysical(pdPhys, NPT_TABLE_SIZE));
            if (!pd) { ExFreePool(ranges); return STATUS_INSUFFICIENT_RESOURCES; }

            pd[i2].Value     = 0;
            pd[i2].Present   = 1;
            pd[i2].Write     = 1;
            pd[i2].User      = 1;
            pd[i2].LargePage = 1;
            pd[i2].Pfn       = addr >> 12;
            pd[i2].Nx        = NxAll ? 1 : 0;

            Memory::UnmapPhysical(pd, NPT_TABLE_SIZE);
        }
    }

    ExFreePool(ranges);
    *OutPhys = pml4Phys;
    *OutVa   = pml4;
    return STATUS_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────

NTSTATUS Npt::Initialize(UINT64* OutHvNcrPhys, void** OutHvNcrVa,
                         UINT64* OutHookNcrPhys, void** OutHookNcrVa)
{
    // Allocate dummy page
    void* dummy = Memory::AllocContiguous(PAGE_SIZE, &g_NptDummyPhys);
    if (!dummy) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(dummy, PAGE_SIZE);

    // HvNCR: normal identity map, NX=0 everywhere
    NTSTATUS st = BuildNcr(OutHvNcrPhys, OutHvNcrVa, false);
    if (!NT_SUCCESS(st)) return st;

    // HookNCR: identity map, NX=1 everywhere (hook entries will be flipped later)
    st = BuildNcr(OutHookNcrPhys, OutHookNcrVa, true);
    return st;
}

void Npt::Destroy(void* HvNcrVa, void* HookNcrVa)
{
    if (HvNcrVa)   Memory::FreeContiguous(HvNcrVa,   NPT_TABLE_SIZE);
    if (HookNcrVa) Memory::FreeContiguous(HookNcrVa, NPT_TABLE_SIZE);
}

// Walk an NCR, optionally splitting a 2 MB PDE to 4 KB PTEs
NptEntry* Npt::GetPte(void* NcrVa, UINT64 Gpa, bool SplitLarge)
{
    auto* pml4 = static_cast<NptEntry*>(NcrVa);

    const UINT32 i4  = (Gpa >> 39) & 0x1FF;
    const UINT32 i3  = (Gpa >> 30) & 0x1FF;
    const UINT32 i2  = (Gpa >> 21) & 0x1FF;
    const UINT32 i1  = (Gpa >> 12) & 0x1FF;

    if (!pml4[i4].Present) return nullptr;
    auto* pdpt = static_cast<NptEntry*>(Memory::MapPhysical(pml4[i4].Pfn << 12, NPT_TABLE_SIZE));
    if (!pdpt) return nullptr;

    if (!pdpt[i3].Present || pdpt[i3].LargePage) { Memory::UnmapPhysical(pdpt, NPT_TABLE_SIZE); return nullptr; }
    UINT64 pdPhys = pdpt[i3].Pfn << 12;
    Memory::UnmapPhysical(pdpt, NPT_TABLE_SIZE);

    auto* pd = static_cast<NptEntry*>(Memory::MapPhysical(pdPhys, NPT_TABLE_SIZE));
    if (!pd) return nullptr;

    if (pd[i2].LargePage && SplitLarge) {
        // Split 2MB → 512×4KB
        UINT64 ptPhys = 0;
        NptEntry* pt = AllocNptTable(&ptPhys);
        if (!pt) { Memory::UnmapPhysical(pd, NPT_TABLE_SIZE); return nullptr; }

        const UINT64 base2m = pd[i2].Pfn << 12;
        const bool   nx     = (bool)pd[i2].Nx;
        for (UINT32 k = 0; k < NPT_ENTRY_COUNT; ++k) {
            pt[k].Value   = 0;
            pt[k].Present = 1;
            pt[k].Write   = 1;
            pt[k].User    = 1;
            pt[k].Pfn     = (base2m >> 12) + k;
            pt[k].Nx      = nx ? 1 : 0;
        }
        pd[i2].Value     = 0;
        pd[i2].Present   = 1;
        pd[i2].Write     = 1;
        pd[i2].User      = 1;
        pd[i2].LargePage = 0;
        pd[i2].Pfn       = ptPhys >> 12;
    }

    if (pd[i2].LargePage || !pd[i2].Present) { Memory::UnmapPhysical(pd, NPT_TABLE_SIZE); return nullptr; }
    UINT64 ptPhys = pd[i2].Pfn << 12;
    Memory::UnmapPhysical(pd, NPT_TABLE_SIZE);

    auto* pt = static_cast<NptEntry*>(Memory::MapPhysical(ptPhys, NPT_TABLE_SIZE));
    if (!pt) return nullptr;

    return &pt[i1];   // caller must unmap pt when done
}

NTSTATUS Npt::AddHook(void* HvNcrVa, void* HookNcrVa,
                      UINT64 TargetGpa, UINT64 ShadowHpa)
{
    const UINT64 pageGpa = TargetGpa & ~0xFFFull;

    // HvNCR: mark target page non-executable (will trigger #NPF on exec)
    NptEntry* hvPte = GetPte(HvNcrVa, pageGpa, /*split=*/true);
    if (!hvPte) return STATUS_NOT_FOUND;
    hvPte->Nx = 1;

    // HookNCR: map target page to shadow, NX=0
    NptEntry* hookPte = GetPte(HookNcrVa, pageGpa, /*split=*/true);
    if (!hookPte) return STATUS_NOT_FOUND;
    hookPte->Pfn = ShadowHpa >> 12;
    hookPte->Nx  = 0;

    // Also enable execute for the page just before/after (boundary safety)
    UINT64 boundaries[2] = { pageGpa - PAGE_SIZE, pageGpa + PAGE_SIZE };
    for (UINT32 i = 0; i < 2; ++i) {
        NptEntry* boundPte = GetPte(HookNcrVa, boundaries[i], /*split=*/false);
        if (boundPte) boundPte->Nx = 0;
    }

    return STATUS_SUCCESS;
}

NTSTATUS Npt::RemoveHook(void* HvNcrVa, void* HookNcrVa, UINT64 TargetGpa)
{
    const UINT64 pageGpa = TargetGpa & ~0xFFFull;

    NptEntry* hvPte = GetPte(HvNcrVa, pageGpa, false);
    if (hvPte) hvPte->Nx = 0;

    NptEntry* hookPte = GetPte(HookNcrVa, pageGpa, false);
    if (hookPte) {
        hookPte->Pfn = pageGpa >> 12;   // restore original PFN
        hookPte->Nx  = 1;               // back to all-NX state
    }
    return STATUS_SUCCESS;
}

NTSTATUS Npt::HidePage(void* HvNcrVa, void* HookNcrVa, UINT64 TargetGpa)
{
    if (!g_NptDummyPhys) return STATUS_DEVICE_NOT_READY;
    const UINT64 pageGpa = TargetGpa & ~0xFFFull;

    NptEntry* hvPte = GetPte(HvNcrVa, pageGpa, true);
    if (hvPte) hvPte->Pfn = g_NptDummyPhys >> 12;

    NptEntry* hookPte = GetPte(HookNcrVa, pageGpa, true);
    if (hookPte) hookPte->Pfn = g_NptDummyPhys >> 12;

    return STATUS_SUCCESS;
}

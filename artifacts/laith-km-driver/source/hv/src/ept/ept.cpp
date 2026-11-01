#include "ept.h"
#include "../common/memory.h"
#include "../vmx/vmcs_defs.h"

// ─────────────────────────────────────────────────────────────────────────────
// EPT uses a 4-level hierarchy identical in layout to regular x86-64 paging:
//   PML4 (512 entries × 8 bytes) → PDPT → PD → PT
// We build an identity map (GPA == HPA) using 2 MB large pages to cover all
// physical memory.  Hooks cause single 2 MB regions to be split into 512 4 KB
// PTEs and then manipulate individual entries.
// ─────────────────────────────────────────────────────────────────────────────

#define EPT_ENTRY_COUNT     512
#define EPT_TABLE_SIZE      (EPT_ENTRY_COUNT * sizeof(EptEntry))

// Dummy PFN used when hiding a page from the guest
static UINT64 g_DummyPagePhys = 0;

// ── Allocate a zero-filled EPT table page ───────────────────────────────────
static EptEntry* AllocEptTable(UINT64* OutPhys)
{
    auto* va = static_cast<EptEntry*>(Memory::AllocContiguous(EPT_TABLE_SIZE, OutPhys));
    return va;
}

// ── Build a 4 KB PTE table to replace a 2 MB large-page PDE ────────────────
static EptEntry* SplitLargePde(EptEntry* Pde)
{
    if (!(Pde->LargePage))
        return nullptr;

    UINT64 ptPhys = 0;
    EptEntry* pt = AllocEptTable(&ptPhys);
    if (!pt) return nullptr;

    const UINT64 base2m = Pde->Pfn << 12; // 2 MB aligned base

    for (UINT32 i = 0; i < EPT_ENTRY_COUNT; ++i) {
        pt[i].Value   = 0;
        pt[i].Read    = 1;
        pt[i].Write   = 1;
        pt[i].Execute = 1;
        pt[i].MemType = EPT_MEMORY_TYPE_WB;
        pt[i].Pfn     = (base2m >> 12) + i;
    }

    Pde->Value     = 0;
    Pde->Read      = 1;
    Pde->Write     = 1;
    Pde->Execute   = 1;
    Pde->LargePage = 0;
    Pde->Pfn       = ptPhys >> 12;

    return pt;
}

// ── Walk to PTE, splitting 2 MB large pages as needed ────────────────────────
EptEntry* Ept::GetOrSplitPte(void* RootVa, UINT64 Gpa, bool Split)
{
    auto* pml4 = static_cast<EptEntry*>(RootVa);

    const UINT32 pml4i = (Gpa >> 39) & 0x1FF;
    const UINT32 pdpti = (Gpa >> 30) & 0x1FF;
    const UINT32 pdi   = (Gpa >> 21) & 0x1FF;
    const UINT32 pti   = (Gpa >> 12) & 0x1FF;

    if (!pml4[pml4i].Value) return nullptr;
    UINT64 pdptPhys = pml4[pml4i].Pfn << 12;
    auto*  pdpt     = static_cast<EptEntry*>(Memory::MapPhysical(pdptPhys, EPT_TABLE_SIZE));
    if (!pdpt) return nullptr;

    if (!pdpt[pdpti].Value) { Memory::UnmapPhysical(pdpt, EPT_TABLE_SIZE); return nullptr; }
    if (pdpt[pdpti].LargePage) {
        // 1 GB – not currently split (shouldn't happen in our 2 MB initial map)
        Memory::UnmapPhysical(pdpt, EPT_TABLE_SIZE);
        return nullptr;
    }
    UINT64 pdPhys = pdpt[pdpti].Pfn << 12;
    Memory::UnmapPhysical(pdpt, EPT_TABLE_SIZE);

    auto* pd = static_cast<EptEntry*>(Memory::MapPhysical(pdPhys, EPT_TABLE_SIZE));
    if (!pd) return nullptr;

    EptEntry* pde = &pd[pdi];
    if (pde->LargePage && Split) {
        SplitLargePde(pde);  // pde is now a PD entry pointing to the new PT
    }
    if (pde->LargePage || !pde->Value) {
        Memory::UnmapPhysical(pd, EPT_TABLE_SIZE);
        return nullptr;
    }
    UINT64 ptPhys = pde->Pfn << 12;
    Memory::UnmapPhysical(pd, EPT_TABLE_SIZE);

    auto* pt = static_cast<EptEntry*>(Memory::MapPhysical(ptPhys, EPT_TABLE_SIZE));
    if (!pt) return nullptr;

    // Return pointer directly into the mapping – caller must unmap via
    // Memory::UnmapPhysical(pt, EPT_TABLE_SIZE) when done.
    // NOTE: For simplicity we return the pointer and let callers handle
    //       lifetime; they modify the entry before calling INVEPT.
    EptEntry* pte = &pt[pti];
    return pte;   // caller holds pt mapping
}

// ─────────────────────────────────────────────────────────────────────────────

NTSTATUS Ept::Initialize(UINT64* OutRootPhys, void** OutRootVa)
{
    // Allocate a dummy page for hiding
    g_DummyPagePhys = 0;
    void* dummy = Memory::AllocContiguous(PAGE_SIZE, &g_DummyPagePhys);
    if (!dummy) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(dummy, PAGE_SIZE);

    // Allocate PML4
    UINT64 pml4Phys = 0;
    EptEntry* pml4 = AllocEptTable(&pml4Phys);
    if (!pml4) return STATUS_INSUFFICIENT_RESOURCES;

    // Get physical memory ranges
    PPHYSICAL_MEMORY_RANGE ranges = MmGetPhysicalMemoryRanges();
    if (!ranges) {
        Memory::FreeContiguous(pml4, EPT_TABLE_SIZE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (UINT32 r = 0; ; ++r) {
        if (!ranges[r].BaseAddress.QuadPart && !ranges[r].NumberOfBytes.QuadPart)
            break;

        UINT64 base = (UINT64)ranges[r].BaseAddress.QuadPart & ~((UINT64)0x1FFFFF);
        UINT64 end  = (UINT64)ranges[r].BaseAddress.QuadPart + (UINT64)ranges[r].NumberOfBytes.QuadPart;
        end = (end + 0x1FFFFF) & ~((UINT64)0x1FFFFF);

        for (UINT64 addr = base; addr < end; addr += 0x200000ULL) {
            const UINT32 pml4i = (addr >> 39) & 0x1FF;
            const UINT32 pdpti = (addr >> 30) & 0x1FF;
            const UINT32 pdi   = (addr >> 21) & 0x1FF;

            // Allocate PDPT if needed
            if (!pml4[pml4i].Value) {
                UINT64 pdptPhys = 0;
                EptEntry* pdpt = AllocEptTable(&pdptPhys);
                if (!pdpt) { ExFreePool(ranges); return STATUS_INSUFFICIENT_RESOURCES; }
                pml4[pml4i].Value = 0;
                pml4[pml4i].Read = pml4[pml4i].Write = pml4[pml4i].Execute = 1;
                pml4[pml4i].Pfn  = pdptPhys >> 12;
            }

            UINT64 pdptPhys = pml4[pml4i].Pfn << 12;
            auto*  pdpt = static_cast<EptEntry*>(Memory::MapPhysical(pdptPhys, EPT_TABLE_SIZE));
            if (!pdpt) { ExFreePool(ranges); return STATUS_INSUFFICIENT_RESOURCES; }

            // Allocate PD if needed
            if (!pdpt[pdpti].Value) {
                UINT64 pdPhys = 0;
                EptEntry* pd = AllocEptTable(&pdPhys);
                if (!pd) { Memory::UnmapPhysical(pdpt, EPT_TABLE_SIZE); ExFreePool(ranges); return STATUS_INSUFFICIENT_RESOURCES; }
                pdpt[pdpti].Value = 0;
                pdpt[pdpti].Read = pdpt[pdpti].Write = pdpt[pdpti].Execute = 1;
                pdpt[pdpti].Pfn  = pdPhys >> 12;
            }

            UINT64 pdPhys = pdpt[pdpti].Pfn << 12;
            Memory::UnmapPhysical(pdpt, EPT_TABLE_SIZE);

            auto* pd = static_cast<EptEntry*>(Memory::MapPhysical(pdPhys, EPT_TABLE_SIZE));
            if (!pd) { ExFreePool(ranges); return STATUS_INSUFFICIENT_RESOURCES; }

            // Map 2 MB large page
            pd[pdi].Value     = 0;
            pd[pdi].Read      = 1;
            pd[pdi].Write     = 1;
            pd[pdi].Execute   = 1;
            pd[pdi].MemType   = EPT_MEMORY_TYPE_WB;
            pd[pdi].LargePage = 1;
            pd[pdi].Pfn       = addr >> 12;

            Memory::UnmapPhysical(pd, EPT_TABLE_SIZE);
        }
    }

    ExFreePool(ranges);

    *OutRootPhys = pml4Phys;
    *OutRootVa   = pml4;
    return STATUS_SUCCESS;
}

void Ept::Destroy(UINT64 RootPhys, void* RootVa)
{
    UNREFERENCED_PARAMETER(RootPhys);
    // In a full implementation we'd walk the tables and free each level.
    // For now we free just the PML4 allocation (table pages are allocated
    // with MmAllocateContiguousMemory per-entry, kept alive for driver lifetime).
    if (RootVa)
        Memory::FreeContiguous(RootVa, EPT_TABLE_SIZE);
}

NTSTATUS Ept::AddHook(void* RootVa, UINT64 TargetGpa, UINT64 ShadowHpa)
{
    const UINT64 pageGpa = TargetGpa & ~0xFFFull;

    // Walk to PTE, splitting the 2 MB page if needed
    EptEntry* pte = GetOrSplitPte(RootVa, pageGpa, /*Split=*/true);
    if (!pte) return STATUS_NOT_FOUND;

    // Set the PTE: read+write to original page, but NOT executable
    // Execute access will be to the shadow page
    // Intel EPT hook: keep rw- permissions, --x on shadow
    // For simplicity we use a single-page shadow model:
    //   - normal PTE: rw- (original pfn)
    //   - shadow PTE view: --x (shadow pfn)
    // Since we don't have dual-mapping here, we set execute-only to shadow.
    pte->Value    = 0;
    pte->Read     = 0;
    pte->Write    = 0;
    pte->Execute  = 1;    // execute → shadow
    pte->MemType  = EPT_MEMORY_TYPE_WB;
    pte->Pfn      = ShadowHpa >> 12;

    // The EPT violation handler in exit_handler.cpp will toggle between
    // read/write mode (original pfn, rw-) and execute mode (shadow pfn, --x).

    return STATUS_SUCCESS;
}

NTSTATUS Ept::RemoveHook(void* RootVa, UINT64 TargetGpa)
{
    const UINT64 pageGpa = TargetGpa & ~0xFFFull;
    EptEntry* pte = GetOrSplitPte(RootVa, pageGpa, /*Split=*/false);
    if (!pte) return STATUS_NOT_FOUND;

    // Restore to identity map (rw-execute, original pfn)
    pte->Value    = 0;
    pte->Read     = 1;
    pte->Write    = 1;
    pte->Execute  = 1;
    pte->MemType  = EPT_MEMORY_TYPE_WB;
    pte->Pfn      = pageGpa >> 12;

    return STATUS_SUCCESS;
}

NTSTATUS Ept::HidePage(void* RootVa, UINT64 TargetGpa)
{
    if (!g_DummyPagePhys) return STATUS_DEVICE_NOT_READY;

    const UINT64 pageGpa = TargetGpa & ~0xFFFull;
    EptEntry* pte = GetOrSplitPte(RootVa, pageGpa, /*Split=*/true);
    if (!pte) return STATUS_NOT_FOUND;

    pte->Value   = 0;
    pte->Read    = 1;
    pte->Write   = 1;
    pte->Execute = 1;
    pte->MemType = EPT_MEMORY_TYPE_WB;
    pte->Pfn     = g_DummyPagePhys >> 12;

    return STATUS_SUCCESS;
}

#include "memory.h"

// ─────────────────────────────────────────────────────────────────────────────

void* Memory::MapPhysical(UINT64 Phys, SIZE_T Size)
{
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (LONGLONG)Phys;
    return MmMapIoSpaceEx(pa, Size, PAGE_READWRITE);
}

void Memory::UnmapPhysical(void* Va, SIZE_T Size)
{
    MmUnmapIoSpace(Va, Size);
}

// Walk a CR3-rooted 4-level page table to translate Va → Pa.
// Handles 4KB, 2MB, and 1GB pages. Returns 0 on any fault.
UINT64 Memory::WalkPageTable(UINT64 Cr3, UINT64 Va)
{
    auto ReadPte = [](UINT64 TablePhys, UINT64 Index) -> UINT64 {
        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (LONGLONG)(TablePhys + Index * 8);
        const auto* va = static_cast<UINT64*>(MmMapIoSpaceEx(pa, sizeof(UINT64), PAGE_READONLY));
        if (!va) return 0;
        const UINT64 val = *va;
        MmUnmapIoSpace(const_cast<UINT64*>(va), sizeof(UINT64));
        return val;
    };

    const UINT64 pml4i  = (Va >> 39) & 0x1FF;
    const UINT64 pdpti  = (Va >> 30) & 0x1FF;
    const UINT64 pdi    = (Va >> 21) & 0x1FF;
    const UINT64 pti    = (Va >> 12) & 0x1FF;
    const UINT64 offset =  Va        & 0xFFF;

    const UINT64 cr3base = Cr3 & ~0xFFFull;

    UINT64 pml4e = ReadPte(cr3base, pml4i);
    if (!(pml4e & 1)) return 0;

    UINT64 pdpte = ReadPte(pml4e & 0x000FFFFFFFFFF000ull, pdpti);
    if (!(pdpte & 1)) return 0;
    if (pdpte & (1ull << 7)) {
        // 1 GB page
        return (pdpte & 0x000FFFFFC0000000ull) | (Va & 0x3FFFFFFFull);
    }

    UINT64 pde = ReadPte(pdpte & 0x000FFFFFFFFFF000ull, pdi);
    if (!(pde & 1)) return 0;
    if (pde & (1ull << 7)) {
        // 2 MB page
        return (pde & 0x000FFFFFFFE00000ull) | (Va & 0x1FFFFFull);
    }

    UINT64 pte = ReadPte(pde & 0x000FFFFFFFFFF000ull, pti);
    if (!(pte & 1)) return 0;

    return (pte & 0x000FFFFFFFFFF000ull) | offset;
}

void* Memory::AllocContiguous(SIZE_T Size, UINT64* OutPhys)
{
    PHYSICAL_ADDRESS low  = {}, high = {};
    high.QuadPart = (LONGLONG)-1;

    void* va = MmAllocateContiguousMemory(Size, high);
    if (!va) return nullptr;

    RtlZeroMemory(va, Size);

    if (OutPhys) {
        PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(va);
        *OutPhys = (UINT64)pa.QuadPart;
    }
    return va;
}

void Memory::FreeContiguous(void* Va, SIZE_T Size)
{
    UNREFERENCED_PARAMETER(Size);
    if (Va) MmFreeContiguousMemory(Va);
}

void* Memory::AllocNonPaged(SIZE_T Size, UINT64* OutPhys)
{
    void* va = ExAllocatePoolWithTag(NonPagedPool, Size, (ULONG)'vHyp');
    if (va) RtlZeroMemory(va, Size);
    if (!va) return nullptr;

    if (OutPhys) {
        PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(va);
        *OutPhys = (UINT64)pa.QuadPart;
    }
    return va;
}

void Memory::FreeNonPaged(void* Va)
{
    if (Va) ExFreePoolWithTag(Va, 'vHyp');
}

UINT64 Memory::VaToPa(void* Va)
{
    PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(Va);
    return (UINT64)pa.QuadPart;
}

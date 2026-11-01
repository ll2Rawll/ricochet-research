#pragma once
#include <ntifs.h>

// This is the offset to the DirectoryTableBase field within the EPROCESS structure.
// NOTE: This offset can vary between different Windows versions. You may need to
// update this value for newer or older OS builds.
#define DIRECTORY_TABLE_BASE_OFFSET 0x328 

// Constants for Page Table Walk
#define PTE_ADDRESS_MASK 0x000FFFFFFFFFF000 // Mask to get the physical page address from a PTE
#define PML4_INDEX(va) ((((ULONG_PTR)va) >> 39) & 0x1FF)
#define PDPT_INDEX(va) ((((ULONG_PTR)va) >> 30) & 0x1FF)
#define PDE_INDEX(va) ((((ULONG_PTR)va) >> 21) & 0x1FF)
#define PTE_INDEX(va) ((((ULONG_PTR)va) >> 12) & 0x1FF)

// This function translates a virtual address to its corresponding physical address
// by manually walking the page tables.
NTSTATUS TranslateVirtualAddress(UINT64 dir_base, PVOID virtual_address, PUINT64 physical_address) {
    if (!dir_base || !virtual_address || !physical_address) {
        return STATUS_INVALID_PARAMETER;
    }

    PHYSICAL_ADDRESS pml4_base;
    pml4_base.QuadPart = dir_base;

    // 1. Get PML4 Entry
    UINT64 pml4_index = PML4_INDEX(virtual_address);
    PUINT64 pml4_entry_addr = (PUINT64)MmGetVirtualForPhysical(pml4_base);
    if (!pml4_entry_addr) return STATUS_UNSUCCESSFUL;
    UINT64 pml4_entry = pml4_entry_addr[pml4_index];
    if ((pml4_entry & 1) == 0) return STATUS_UNSUCCESSFUL; // Not present

    // 2. Get PDPT Entry
    PHYSICAL_ADDRESS pdpt_base;
    pdpt_base.QuadPart = pml4_entry & PTE_ADDRESS_MASK;
    UINT64 pdpt_index = PDPT_INDEX(virtual_address);
    PUINT64 pdpt_entry_addr = (PUINT64)MmGetVirtualForPhysical(pdpt_base);
    if (!pdpt_entry_addr) return STATUS_UNSUCCESSFUL;
    UINT64 pdpt_entry = pdpt_entry_addr[pdpt_index];
    if ((pdpt_entry & 1) == 0) return STATUS_UNSUCCESSFUL; // Not present

    // 3. Get PDE Entry
    PHYSICAL_ADDRESS pde_base;
    pde_base.QuadPart = pdpt_entry & PTE_ADDRESS_MASK;
    UINT64 pde_index = PDE_INDEX(virtual_address);
    PUINT64 pde_entry_addr = (PUINT64)MmGetVirtualForPhysical(pde_base);
    if (!pde_entry_addr) return STATUS_UNSUCCESSFUL;
    UINT64 pde_entry = pde_entry_addr[pde_index];
    if ((pde_entry & 1) == 0) return STATUS_UNSUCCESSFUL; // Not present

    // Handle large pages (2MB)
    if ((pde_entry & 0x80)) {
        *physical_address = (pde_entry & 0x000FFFFFE00000) + ((ULONG_PTR)virtual_address & 0x1FFFFF);
        return STATUS_SUCCESS;
    }

    // 4. Get PTE Entry
    PHYSICAL_ADDRESS pte_base;
    pte_base.QuadPart = pde_entry & PTE_ADDRESS_MASK;
    UINT64 pte_index = PTE_INDEX(virtual_address);
    PUINT64 pte_entry_addr = (PUINT64)MmGetVirtualForPhysical(pte_base);
    if (!pte_entry_addr) return STATUS_UNSUCCESSFUL;
    UINT64 pte_entry = pte_entry_addr[pte_index];
    if ((pte_entry & 1) == 0) return STATUS_UNSUCCESSFUL; // Not present

    // 5. Calculate final physical address
    *physical_address = (pte_entry & PTE_ADDRESS_MASK) + ((ULONG_PTR)virtual_address & 0xFFF);
    return STATUS_SUCCESS;
}

// This function uses the page table walk to read memory from a target process.
NTSTATUS ReadProcessMemoryByPageWalk(PEPROCESS target_process, PVOID source_address, PVOID target_buffer, SIZE_T read_size) {
    if (!target_process || !source_address || !target_buffer || read_size == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    UINT64 dir_base = *(UINT64*)((PUCHAR)target_process + DIRECTORY_TABLE_BASE_OFFSET);
    PUCHAR read_ptr = (PUCHAR)source_address;
    PUCHAR write_ptr = (PUCHAR)target_buffer;
    SIZE_T bytes_remaining = read_size;

    // Read page by page
    while (bytes_remaining > 0) {
        UINT64 phys_addr = 0;
        NTSTATUS status = TranslateVirtualAddress(dir_base, read_ptr, &phys_addr);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        PHYSICAL_ADDRESS phys;
        phys.QuadPart = phys_addr;

        // Map the physical page into our driver's virtual address space
        PVOID mapped_page = MmMapIoSpace(phys, PAGE_SIZE, MmCached);
        if (!mapped_page) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        // Calculate how much to copy from this page
        ULONG_PTR offset_in_page = (ULONG_PTR)read_ptr & 0xFFF;
        SIZE_T bytes_to_copy = min(PAGE_SIZE - offset_in_page, bytes_remaining);

        // Copy the data
        RtlCopyMemory(write_ptr, (PUCHAR)mapped_page + offset_in_page, bytes_to_copy);

        // Unmap the page
        MmUnmapIoSpace(mapped_page, PAGE_SIZE);

        bytes_remaining -= bytes_to_copy;
        read_ptr += bytes_to_copy;
        write_ptr += bytes_to_copy;
    }

    return STATUS_SUCCESS;
}
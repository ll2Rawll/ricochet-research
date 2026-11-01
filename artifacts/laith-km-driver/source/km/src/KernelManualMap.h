#pragma once
#include "helpers.h"

// ========================================================================
// === KERNEL MANUAL MAPPER REQUEST STRUCT ================================
// ========================================================================
// The raw DLL buffer will follow immediately after the TIDs array in memory.
typedef struct _KernelManualMapRequest {
    ULONG TargetPid;
    ULONG ThreadCount;
    SIZE_T DllSize;
    ULONG TargetTids[1]; // Flexible array member: TIDs first, then the raw DLL bytes
} KernelManualMapRequest, * PKernelManualMapRequest;


// ========================================================================
// === KERNEL-SIDE PE PARSING & MAPPING ===================================
// ========================================================================

#define RVA_TO_ADDR(type, base, rva) (type)((ULONG_PTR)base + rva)

NTSTATUS KernelManualMapDll(PIRP irp, SIZE_T* information)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    *information = 0;

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PKernelManualMapRequest request = (PKernelManualMapRequest)irp->AssociatedIrp.SystemBuffer;
    
    if (!request || !stack || request->DllSize == 0 || request->ThreadCount == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    SIZE_T tid_array_size = request->ThreadCount * sizeof(ULONG);
    PUCHAR raw_dll_buffer = (PUCHAR)request->TargetTids + tid_array_size;

    PEPROCESS target_process = nullptr;
    status = PsLookupProcessByProcessIdPtr((HANDLE)request->TargetPid, &target_process);
    if (!NT_SUCCESS(status)) {
        KLOG("[KernelMM] Failed to lookup process: 0x%X", status);
        return status;
    }

    KLOG("[KernelMM] Starting Kernel Manual Map for PID %lu", request->TargetPid);

    KAPC_STATE apc_state;
    PVOID allocated_base = nullptr;
    PVOID remote_params_base = nullptr;

    __try {
        // 1. Parse Headers from Raw Buffer
        PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)raw_dll_buffer;
        if (pDos->e_magic != IMAGE_DOS_SIGNATURE) {
            KLOG("[KernelMM] Invalid DOS signature");
            status = STATUS_INVALID_IMAGE_FORMAT;
            __leave;
        }

        PIMAGE_NT_HEADERS pNt = (PIMAGE_NT_HEADERS)(raw_dll_buffer + pDos->e_lfanew);
        if (pNt->Signature != IMAGE_NT_SIGNATURE) {
            KLOG("[KernelMM] Invalid NT signature");
            status = STATUS_INVALID_IMAGE_FORMAT;
            __leave;
        }

        if (pNt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            KLOG("[KernelMM] Only 64-bit DLLs are supported");
            status = STATUS_INVALID_IMAGE_FORMAT;
            __leave;
        }

        SIZE_T image_size = pNt->OptionalHeader.SizeOfImage;

        // 2. Allocate Memory in Target Process
        KeStackAttachProcess(target_process, &apc_state);

        status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &allocated_base, 0, &image_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!NT_SUCCESS(status)) {
            KLOG("[KernelMM] Failed to allocate memory in target process: 0x%X", status);
            KeUnstackDetachProcess(&apc_state);
            __leave;
        }
        KLOG("[KernelMM] Allocated memory at %p (Size: 0x%lX)", allocated_base, image_size);

        // 3. Map Sections
        // Copy Headers
        RtlCopyMemory(allocated_base, raw_dll_buffer, pNt->OptionalHeader.SizeOfHeaders);

        // Copy Sections
        PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNt);
        for (WORD i = 0; i < pNt->FileHeader.NumberOfSections; i++) {
            if (pSection[i].SizeOfRawData == 0) continue;
            PVOID dest = (PVOID)((ULONG_PTR)allocated_base + pSection[i].VirtualAddress);
            PVOID src = (PVOID)(raw_dll_buffer + pSection[i].PointerToRawData);
            RtlCopyMemory(dest, src, pSection[i].SizeOfRawData);
        }
        KLOG("[KernelMM] Mapped sections");

        // 4. Apply Relocations
        ULONG_PTR delta = (ULONG_PTR)allocated_base - pNt->OptionalHeader.ImageBase;
        if (delta != 0) {
            IMAGE_DATA_DIRECTORY& relocDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
            if (relocDir.Size > 0) {
                PIMAGE_BASE_RELOCATION pReloc = RVA_TO_ADDR(PIMAGE_BASE_RELOCATION, allocated_base, relocDir.VirtualAddress);
                while (pReloc->VirtualAddress != 0) {
                    WORD* pRelocData = (WORD*)((PUCHAR)pReloc + sizeof(IMAGE_BASE_RELOCATION));
                    DWORD numEntries = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);

                    for (DWORD i = 0; i < numEntries; i++) {
                        WORD type = pRelocData[i] >> 12;
                        WORD offset = pRelocData[i] & 0xFFF;

                        if (type == IMAGE_REL_BASED_DIR64) {
                            ULONG_PTR* pAddress = RVA_TO_ADDR(ULONG_PTR*, allocated_base, pReloc->VirtualAddress + offset);
                            *pAddress += delta;
                        }
                    }
                    pReloc = (PIMAGE_BASE_RELOCATION)((PUCHAR)pReloc + pReloc->SizeOfBlock);
                }
            }
        }
        KLOG("[KernelMM] Applied relocations");

        // 5. Resolve Imports
        IMAGE_DATA_DIRECTORY& importDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (importDir.Size > 0) {
            PIMAGE_IMPORT_DESCRIPTOR pImportDesc = RVA_TO_ADDR(PIMAGE_IMPORT_DESCRIPTOR, allocated_base, importDir.VirtualAddress);
            
            while (pImportDesc->Name != 0) {
                char* moduleName = RVA_TO_ADDR(char*, allocated_base, pImportDesc->Name);
                
                // Convert narrow module name to UNICODE_STRING
                ANSI_STRING aModuleName;
                UNICODE_STRING uModuleName;
                RtlInitAnsiString(&aModuleName, moduleName);
                if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&uModuleName, &aModuleName, TRUE))) {
                    
                    // Find module base in target PEB
                    PVOID pRemoteModuleBase = (PVOID)get_module_base_x64(target_process, uModuleName);
                    
                    if (!pRemoteModuleBase) {
                        KLOG("[KernelMM] Dependency %wZ not loaded in target process!", &uModuleName);
                        // We must fail if a dependency is missing, or risk a crash.
                        RtlFreeUnicodeString(&uModuleName);
                        status = STATUS_NOT_FOUND;
                        KeUnstackDetachProcess(&apc_state);
                        __leave;
                    }

                    PIMAGE_THUNK_DATA pThunk = RVA_TO_ADDR(PIMAGE_THUNK_DATA, allocated_base, pImportDesc->OriginalFirstThunk ? pImportDesc->OriginalFirstThunk : pImportDesc->FirstThunk);
                    PIMAGE_THUNK_DATA pIAT = RVA_TO_ADDR(PIMAGE_THUNK_DATA, allocated_base, pImportDesc->FirstThunk);

                    while (pThunk->u1.AddressOfData != 0) {
                        if (IMAGE_SNAP_BY_ORDINAL(pThunk->u1.Ordinal)) {
                            // Resolve by ordinal - Not supported by kernel_get_proc_address_safe currently!
                            // We would need to implement an ordinal lookup. For now, assume most cheats use name imports.
                            KLOG("[KernelMM] Ordinal imports not supported in kernel mapper yet!");
                            status = STATUS_NOT_SUPPORTED;
                            RtlFreeUnicodeString(&uModuleName);
                            KeUnstackDetachProcess(&apc_state);
                            __leave;
                        } else {
                            // Resolve by name
                            PIMAGE_IMPORT_BY_NAME pImportName = RVA_TO_ADDR(PIMAGE_IMPORT_BY_NAME, allocated_base, pThunk->u1.AddressOfData);
                            PVOID pFuncAddr = kernel_get_proc_address_safe(pRemoteModuleBase, pImportName->Name);
                            if (pFuncAddr) {
                                pIAT->u1.Function = (ULONG_PTR)pFuncAddr;
                            } else {
                                KLOG("[KernelMM] Failed to resolve %s in %wZ", pImportName->Name, &uModuleName);
                                status = STATUS_NOT_FOUND;
                                RtlFreeUnicodeString(&uModuleName);
                                KeUnstackDetachProcess(&apc_state);
                                __leave;
                            }
                        }
                        pThunk++;
                        pIAT++;
                    }
                    RtlFreeUnicodeString(&uModuleName);
                }
                pImportDesc++;
            }
        }
        KLOG("[KernelMM] Resolved imports");

        // 6. Flip Protections to RX using PTE modification
        // We must detach to use set_pages_rx_by_page_walk as it attaches internally
        KeUnstackDetachProcess(&apc_state);
        
        KLOG("[KernelMM] Flipping mapped memory to RX via PTEs...");
        status = set_pages_rx_by_page_walk(target_process, allocated_base, image_size);
        if (!NT_SUCCESS(status)) {
            KLOG("[KernelMM] Failed to set PTEs to RX: 0x%X", status);
            __leave;
        }

        KeStackAttachProcess(target_process, &apc_state);

        // 7. Execution (Queue APC)
        // We will use the same APC trick, but we must allocate memory for the shellcode and params.
        
        // Manual Map Shellcode:
        // sub rsp, 0x28
        // mov rbx, rcx
        // mov rdx, [rbx + 0x30] (custom params ptr)
        // mov rax, [rdx + 0x8] (hinstDLL)
        // mov rcx, rax
        // mov edx, 1 (DLL_PROCESS_ATTACH)
        // xor r8, r8
        // mov rax, [rdx] (pDllMain)
        // call rax
        // add rsp, 0x28
        // ret
        BYTE shellcode[] = {
            0x48, 0x83, 0xEC, 0x28,       
            0x4C, 0x8B, 0xD1,             
            0x4D, 0x8B, 0x52, 0x30,       
            0x49, 0x8B, 0x4A, 0x08,       
            0xBA, 0x01, 0x00, 0x00, 0x00, 
            0x4D, 0x31, 0xC0,             
            0x49, 0x8B, 0x02,             
            0xFF, 0xD0,                   
            0x48, 0x83, 0xC4, 0x28,       
            0xC3                          
        };
        SIZE_T shellcode_size = sizeof(shellcode);
        PVOID remote_shellcode_base = nullptr;
        status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &remote_shellcode_base, 0, &shellcode_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!NT_SUCCESS(status)) { __leave; }
        RtlCopyMemory(remote_shellcode_base, shellcode, shellcode_size);

        // Params
        SIZE_T params_alloc_size = sizeof(ApcShellcodeParams);
        status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &remote_params_base, 0, &params_alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!NT_SUCCESS(status)) { __leave; }
        
        // Custom Params struct inside the Path buffer
        struct {
            PVOID pDllMain;
            PVOID hinstDLL;
        } custom_params;
        
        PVOID pDllMain = (PVOID)((ULONG_PTR)allocated_base + pNt->OptionalHeader.AddressOfEntryPoint);
        custom_params.pDllMain = pDllMain;
        custom_params.hinstDLL = allocated_base;

        SIZE_T custom_params_size = sizeof(custom_params);
        PVOID remote_custom_params_base = nullptr;
        status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &remote_custom_params_base, 0, &custom_params_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!NT_SUCCESS(status)) { __leave; }
        RtlCopyMemory(remote_custom_params_base, &custom_params, custom_params_size);

        ApcShellcodeParams local_params = { 0 };
        local_params.pDllBase = remote_custom_params_base; // Point to custom params
        RtlCopyMemory(remote_params_base, &local_params, sizeof(local_params));

        // Flip shellcode to RX
        KeUnstackDetachProcess(&apc_state);
        set_pages_rx_by_page_walk(target_process, remote_shellcode_base, shellcode_size);
        KeStackAttachProcess(target_process, &apc_state);

        KLOG("[KernelMM] Starting Shotgun APC Execution...");
        ULONG successful_apcs = 0;
        for (ULONG i = 0; i < request->ThreadCount; i++) {
            PETHREAD target_thread = nullptr;
            status = PsLookupThreadByThreadId((HANDLE)request->TargetTids[i], &target_thread);
            if (!NT_SUCCESS(status)) continue;

            PKAPC apc = (PKAPC)ExAllocatePoolWithTag(NonPagedPool, sizeof(KAPC), POOL_TAG);
            if (apc) {
                KeInitializeApc(
                    apc,
                    target_thread,
                    OriginalApcEnvironment,
                    (PKKERNEL_ROUTINE)kernel_apc_stub,
                    (PKRUNDOWN_ROUTINE)NULL,
                    (PKNORMAL_ROUTINE)remote_shellcode_base,
                    UserMode,
                    remote_params_base
                );

                if (KeInsertQueueApc(apc, remote_params_base, NULL, 0)) successful_apcs++;
                else ExFreePoolWithTag(apc, POOL_TAG);
            }
            ObDereferenceObject(target_thread);
        }

        KLOG("[KernelMM] Successfully queued %lu APCs", successful_apcs);
        if (successful_apcs > 0) status = STATUS_SUCCESS;
        else status = STATUS_UNSUCCESSFUL;

        KeUnstackDetachProcess(&apc_state);

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
        KLOG("[KernelMM] Exception occurred during manual map: 0x%X", status);
        // Ensure we detach if we crash while attached
        if (PsGetCurrentProcess() == target_process) {
            KeUnstackDetachProcess(&apc_state);
        }
    }

    if (target_process) ObDereferenceObject(target_process);
    
    *information = 0;
    return status;
}

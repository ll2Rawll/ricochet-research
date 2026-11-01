#pragma once
#include <ntifs.h>
#include <ntddmou.h>
#include <ntintsafe.h>
#include <ntimage.h>
#include "memory.h"
#include <ntstrsafe.h>   // RtlStringCbVPrintfA
#include <stdarg.h>      // va_list, va_start, va_end
#include <intrin.h> // Required for __invlpg
typedef unsigned char BYTE;
#define SystemModuleInformation 11
#define POOL_TAG 'oamL'
#define ActiveProcessLinks 0x1d8
#define PebOffset 0x2e0
#define SectionBaseAddress 0x2b0
#define ImageFileName 0x338
#define ThreadListHead 0x370
#define ActiveThreads 0x380
#define DIRECTORY_TABLE_BASE_OFFSET 0x28 // in memory.h too
#define PROCESS_QUERY_INFORMATION (0x0400)
// Define the IOCTL for connecting to the mouse driver
#define IOCTL_INTERNAL_MOUSE_CONNECT 0x000F0203
// Physical-to-Virtual mapping for when we find our physical address
#define physical_to_virtual(address) MmGetVirtualForPhysical(address)
#define KLOG(fmt, ...) log_write_kernel("[K] " fmt, __VA_ARGS__)
VOID log_write_kernel(const char* fmt, ...);
#define MAX_COALESCE_PAGES 16 // tune this: more pages -> fewer calls but more risk of non-contiguous phys
#define PAGE_BITS 12
#define PAGE_SIZE 0x1000
#define PAGE_MASK 0xFFF
#define MAX_CHUNK_BYTES (256 * 1024) // 256 KB
#define MAX_PAGES_PER_CHUNK (MAX_CHUNK_BYTES / PAGE_SIZE)
#define MAX_MODULE_ENTRY_NAME 260 // Typical MAX_PATH-length name buffer

typedef struct _MODULE_ENTRY_INFO {
    UINT64 baseAddress;                        // Base address of the module in memory
    ULONG  size;                               // Size of the image
    WCHAR  moduleName[MAX_MODULE_ENTRY_NAME];  // Unicode module name (null-terminated)
} MODULE_ENTRY_INFO, * PMODULE_ENTRY_INFO;

// Global variables defined once to prevent redefinition errors
static PEPROCESS cached_process = nullptr;
static HANDLE cached_pid = nullptr;

static PDEVICE_OBJECT g_mouse_device_object = NULL;
static VOID(*g_mouse_service_callback)(PDEVICE_OBJECT, PMOUSE_INPUT_DATA, PMOUSE_INPUT_DATA, PULONG) = NULL;

extern "C" {
    NTKERNELAPI PVOID NTAPI PsGetProcessSectionBaseAddress(PEPROCESS Process);
    NTKERNELAPI NTSTATUS IoCreateDriver(PUNICODE_STRING DriverName, PDRIVER_INITIALIZE InitializationFunction);
    NTKERNELAPI NTSTATUS MmCopyVirtualMemory(PEPROCESS SourceProcess, PVOID SourceAddress, PEPROCESS TargetProcess, PVOID TargetAddress, SIZE_T BufferSize, KPROCESSOR_MODE PreviousMode, PSIZE_T ReturnSize);
    NTSYSAPI NTSTATUS NTAPI ZwQuerySystemInformation(ULONG InfoClass, PVOID Buffer, ULONG Length, PULONG ReturnLength);
    NTKERNELAPI PPEB NTAPI PsGetProcessPeb(PEPROCESS Process);
    NTSYSAPI NTSTATUS NTAPI ZwQueryInformationProcess(HANDLE ProcessHandle, ULONG ProcessInformationClass, PVOID ProcessInformation, ULONG ProcessInformationLength, PULONG ReturnLength);
    extern "C" NTKERNELAPI PCHAR NTAPI PsGetProcessImageFileName(PEPROCESS Process);
}
extern "C" {
    NTSTATUS NTAPI NtReadVirtualMemory(
        IN HANDLE ProcessHandle,
        IN PVOID BaseAddress,
        OUT PVOID Buffer,
        IN SIZE_T BufferSize,
        OUT PSIZE_T NumberOfBytesRead OPTIONAL
    );
    // --- APC Function Definitions ---
    typedef struct _KTHREAD* PKTHREAD;
    typedef struct _KAPC* PKAPC;

    typedef enum _KAPC_ENVIRONMENT {
        OriginalApcEnvironment,
        AttachedApcEnvironment,
        CurrentApcEnvironment,
        InsertApcEnvironment
    } KAPC_ENVIRONMENT;

    typedef VOID(NTAPI* PKNORMAL_ROUTINE)(
        PVOID NormalContext,
        PVOID SystemArgument1,
        PVOID SystemArgument2
        );

    typedef VOID(NTAPI* PKKERNEL_ROUTINE)(
        PKAPC Apc,
        PKNORMAL_ROUTINE* NormalRoutine,
        PVOID* NormalContext,
        PVOID* SystemArgument1,
        PVOID* SystemArgument2
        );

    typedef VOID(NTAPI* PKRUNDOWN_ROUTINE)(PKAPC Apc);

    NTKERNELAPI VOID NTAPI KeInitializeApc(
        _Inout_ PKAPC Apc,
        _In_ PKTHREAD Thread,
        _In_ KAPC_ENVIRONMENT Environment,
        _In_ PKKERNEL_ROUTINE KernelRoutine,
        _In_opt_ PKRUNDOWN_ROUTINE RundownRoutine,
        _In_opt_ PKNORMAL_ROUTINE NormalRoutine,
        _In_ KPROCESSOR_MODE ProcessorMode,
        _In_opt_ PVOID NormalContext
    );

    NTKERNELAPI BOOLEAN NTAPI KeInsertQueueApc(
        _Inout_ PKAPC Apc,
        _In_ PVOID SystemArgument1,
        _In_ PVOID SystemArgument2,
        _In_ KPRIORITY Increment
    );

    NTSYSAPI
        NTSTATUS
        NTAPI
        ZwProtectVirtualMemory(
            __in HANDLE ProcessHandle,
            __inout PVOID* BaseAddress,
            __inout PSIZE_T RegionSize,
            __in ULONG NewProtect,
            __out PULONG OldProtect
        );
}

// Function pointer typedefs from forum's struct.h
typedef PVOID(*PMM_GET_VIRTUAL_FOR_PHYSICAL)(IN PHYSICAL_ADDRESS PhysicalAddress);
typedef NTSTATUS(*PPS_LOOKUP_PROCESS_BY_PROCESS_ID)(
    HANDLE ProcessId,
    PEPROCESS* Process
    );

// Global function pointers (defined in main.cpp)
extern PPS_LOOKUP_PROCESS_BY_PROCESS_ID PsLookupProcessByProcessIdPtr;
extern PMM_GET_VIRTUAL_FOR_PHYSICAL MmGetVirtualForPhysicalPtr;

// Global Version struct (defined in main.cpp)
extern RTL_OSVERSIONINFOW Version;

// Macros from forum's defs.h
#define PFN_TO_PAGE(pfn) (pfn << 12)
#define PAGE_SHIFT 12

// Page struct from forum's defs.h
typedef struct _SPY_PAGE { // Renamed from _PAGE to avoid conflicts
    PVOID VirtualAddress;    // Virtual address of mapped page
    PVOID PTE;               // Corresponding PTE (changed to PVOID to avoid conflict)
    ULONG64 PreviousPageFrame; // Original PFN
} SPY_PAGE;

// Page table structs from forum's struct.h (renamed to SPY_)
#pragma pack(push, 1)
typedef union _SPY_PTE_CR3
{
    ULONG64 Value;
    struct
    {
        ULONG64 Ignored1 : 3;
        ULONG64 WriteThrough : 1;
        ULONG64 CacheDisable : 1;
        ULONG64 Ignored2 : 7;
        ULONG64 Pml4 : 40;
        ULONG64 Reserved : 12;
    };
} SPY_PTE_CR3;

typedef union _SPY_VIRTUAL_ADDRESS
{
    ULONG64 Value;
    void* Pointer;
    struct
    {
        ULONG64 Offset : 12;
        ULONG64 PtIndex : 9;
        ULONG64 PdIndex : 9;
        ULONG64 PdptIndex : 9;
        ULONG64 Pml4Index : 9;
        ULONG64 Reserved : 16;
    };
} SPY_VIRTUAL_ADDRESS;

typedef union _SPY_PML4E
{
    ULONG64 Value;
    struct
    {
        ULONG64 Present : 1;
        ULONG64 Rw : 1;
        ULONG64 User : 1;
        ULONG64 WriteThrough : 1;
        ULONG64 CacheDisable : 1;
        ULONG64 Accessed : 1;
        ULONG64 Ignored1 : 1;
        ULONG64 Reserved1 : 1;
        ULONG64 Ignored2 : 4;
        ULONG64 Pdpt : 40; // This is the PFN
        ULONG64 Ignored3 : 11;
        ULONG64 Xd : 1;
    };
} SPY_PML4E;

typedef union _SPY_PDPTE
{
    ULONG64 Value;
    struct
    {
        ULONG64 Present : 1;
        ULONG64 Rw : 1;
        ULONG64 User : 1;
        ULONG64 WriteThrough : 1;
        ULONG64 CacheDisable : 1;
        ULONG64 Accessed : 1;
        ULONG64 Dirty : 1;
        ULONG64 PageSize : 1;
        ULONG64 Ignored2 : 4;
        ULONG64 Pd : 40; // This is the PFN
        ULONG64 Ignored3 : 11;
        ULONG64 Xd : 1;
    };
} SPY_PDPTE;

typedef union _SPY_PDE
{
    ULONG64 Value;
    struct
    {
        ULONG64 Present : 1;
        ULONG64 Rw : 1;
        ULONG64 User : 1;
        ULONG64 WriteThrough : 1;
        ULONG64 CacheDisable : 1;
        ULONG64 Accessed : 1;
        ULONG64 Dirty : 1;
        ULONG64 PageSize : 1;
        ULONG64 Ignored2 : 4;
        ULONG64 Pt : 40; // This is the PFN
        ULONG64 Ignored3 : 11;
        ULONG64 Xd : 1;
    };
} SPY_PDE;

typedef union _SPY_PTE
{
    ULONG64 Value;
    SPY_VIRTUAL_ADDRESS VirtualAddress;
    struct
    {
        ULONG64 Present : 1;
        ULONG64 Rw : 1;
        ULONG64 User : 1;
        ULONG64 WriteThrough : 1;
        ULONG64 CacheDisable : 1;
        ULONG64 Accessed : 1;
        ULONG64 Dirty : 1;
        ULONG64 Pat : 1;
        ULONG64 Global : 1;
        ULONG64 Ignored1 : 3;
        ULONG64 PFN : 40; // This is the PFN
        ULONG64 Ignored3 : 11;
        ULONG64 Xd : 1;
    };
} SPY_PTE;
#pragma pack(pop)

typedef struct _KernelInjectApcRequest {
    ULONG TargetPid;
    ULONG ThreadCount;
    SIZE_T PathSize;
    SIZE_T ShellcodeSize;
    ULONG TargetTids[1]; // Flexible array member
} KernelInjectApcRequest, * PKernelInjectApcRequest;

typedef struct _ApcShellcodeParams {
    PVOID pLoadLibraryA;
    PVOID pGetProcAddress;
    PVOID pUser32Base;
    PVOID pKernel32Base;
    PVOID pUcrtBase;
    PVOID pMagicByte;
    PVOID pDllBase; // this is the path
} ApcShellcodeParams, * PApcShellcodeParams;

typedef struct _k_alloc_mem_request {
    ULONG pid, allocation_type, protect;
    ULONGLONG addr; // Using ULONGLONG to match Payson's struct
    SIZE_T size;
} k_alloc_mem_request, * pk_alloc_mem_request;

typedef struct _k_protect_mem_request {
    ULONG pid, protect;
    ULONGLONG addr;
    SIZE_T size;
} k_protect_mem_request, * pk_protect_mem_request;

typedef struct _k_free_mem_request {
    ULONG pid;
    ULONGLONG addr; // Base address to free
} k_free_mem_request, * pk_free_mem_request;

// From forum driver.cpp, MODIFIED to use SPY_ structs
namespace ReadPageMemory {
    // Global page mapping array
    SPY_PAGE PageList[64];

    // Convert physical address to virtual address
    PVOID PhysicalToVirtual(ULONG64 address) {
        PHYSICAL_ADDRESS physical;
        physical.QuadPart = address;
        // Use our global function pointer
        return MmGetVirtualForPhysicalPtr(physical);
    }

    // Get PTE for a virtual address (in our *own* driver context)
    SPY_PTE* MemoryGetPte(const ULONG64 address) {
        SPY_VIRTUAL_ADDRESS virtualAddress;
        virtualAddress.Value = address;
        SPY_PTE_CR3 cr3;
        cr3.Value = __readcr3(); // Read our *driver's* CR3

        // Walk our *own* page tables
        SPY_PML4E* pml4 = (SPY_PML4E*)(PhysicalToVirtual(PFN_TO_PAGE(cr3.Pml4)));
        const SPY_PML4E* pml4e = (pml4 + virtualAddress.Pml4Index);
        if (!pml4e->Present) return 0;

        SPY_PDPTE* pdpt = (SPY_PDPTE*)(PhysicalToVirtual(PFN_TO_PAGE(pml4e->Pdpt)));
        const SPY_PDPTE* pdpte = (pdpt + virtualAddress.PdptIndex);
        if (!pdpte->Present) return 0;
        if (pdpte->PageSize) return 0;

        SPY_PDE* pd = (SPY_PDE*)(PhysicalToVirtual(PFN_TO_PAGE(pdpte->Pd)));
        const SPY_PDE* pde = (pd + virtualAddress.PdIndex);
        if (!pde->Present) return 0;
        if (pde->PageSize) return 0;

        SPY_PTE* pt = (SPY_PTE*)(PhysicalToVirtual(PFN_TO_PAGE(pde->Pt)));
        SPY_PTE* pte = (pt + virtualAddress.PtIndex);
        if (!pte->Present) return 0;

        return pte;
    }

    // Initialize memory management structures
    NTSTATUS InitializeMemoryManagement() {
        for (UINT32 i = 0; i < 64; i++) {
            PHYSICAL_ADDRESS maxAddress;
            maxAddress.QuadPart = MAXULONG64;

            // Allocate contiguous memory for page mapping
            PageList[i].VirtualAddress = MmAllocateContiguousMemory(PAGE_SIZE, maxAddress);
            if (!PageList[i].VirtualAddress) return STATUS_PROCEDURE_NOT_FOUND;

            // Get PTE for the allocated page
            PageList[i].PTE = MemoryGetPte((ULONG64)(PageList[i].VirtualAddress));
            if (!PageList[i].PTE) return STATUS_PROCEDURE_NOT_FOUND;
        }
        return STATUS_SUCCESS;
    }

    // Read from physical memory using page remapping
    VOID ReadPhysicalAddress(UINT32 Index, ULONG64 phy, PVOID buffer, SIZE_T size) {
        SPY_PAGE* pageInfo = &PageList[Index];
        // Cast the PVOID from the struct to the correct SPY_PTE* type
        SPY_PTE* pte = (SPY_PTE*)pageInfo->PTE;
        const ULONG64 OldPFN = pte->PFN;

        _disable(); // --- INTERRUPTS OFF ---

        // Remap page to target physical address
        pte->PFN = phy >> PAGE_SHIFT;
        __invlpg(reinterpret_cast<PVOID>(pageInfo->VirtualAddress)); // Flush TLB

        // Copy data from remapped page
        __try {
            __movsb((PUCHAR)buffer, (PUCHAR)pageInfo->VirtualAddress + (phy & 0xFFF), size);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            KLOG("[ReadPhysicalAddress] Exception caught during memory read.");
        }

        // Restore original mapping
        pte->PFN = OldPFN;
        __invlpg(reinterpret_cast<PVOID>(pageInfo->VirtualAddress)); // Flush TLB again

        _enable(); // --- INTERRUPTS ON ---
    }

    // Translate virtual address to physical address using target CR3
    ULONG64 TransformationCR3(UINT32 Index, ULONG64 cr3, ULONG64 VirtualAddress) {
        cr3 &= ~0xf;
        ULONG64 PAGE_OFFSET = VirtualAddress & ~(~0ul << 12);
        ULONG64 a = 0, b = 0, c = 0;

        // Walk page tables using physical memory reads
        ReadPhysicalAddress(Index, (cr3 + 8 * ((VirtualAddress >> 39) & (0x1ffll))), &a, sizeof(a));
        if (~a & 1) return 0;

        ReadPhysicalAddress(Index, ((a & ((~0xfull << 8) & 0xfffffffffull)) + 8 * ((VirtualAddress >> 30) & (0x1ffll))), &b, sizeof(b));
        if (~b & 1) return 0;
        if (b & 0x80) return (b & (~0ull << 42 >> 12)) + (VirtualAddress & ~(~0ull << 30));

        ReadPhysicalAddress(Index, ((b & ((~0xfull << 8) & 0xfffffffffull)) + 8 * ((VirtualAddress >> 21) & (0x1ffll))), &c, sizeof(c));
        if (~c & 1) return 0;
        if (c & 0x80) return (c & ((~0xfull << 8) & 0xfffffffffull)) + (VirtualAddress & ~(~0ull << 21));

        ULONG64 address = 0;
        ReadPhysicalAddress(Index, ((c & ((~0xfull << 8) & 0xfffffffffull)) + 8 * ((VirtualAddress >> 12) & (0x1ffll))), &address, sizeof(address));
        address &= ((~0xfull << 8) & 0xfffffffffull);
        if (!address) return 0;

        return address + PAGE_OFFSET;
    }
} // end namespace ReadPageMemory

// --- END SPY PAGE DEPENDENCIES & NAMESPACE ---

void debug_print(PCSTR text);
template <typename str_type, typename str_type_2>
__forceinline bool crt_strcmp(str_type str, str_type_2 in_str, bool two);
HANDLE GetPID(const wchar_t* process_name);

void debug_print(PCSTR text) {
#ifndef DEBUG
    UNREFERENCED_PARAMETER(text);
#endif // DEBUG

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, text));
}

// From forum functions.h
// Get DirectoryTableBase offset based on Windows build number
ULONG GetDirectoryTableOffset() {
    RtlGetVersion(&Version); // Uses the global 'Version'
    switch (Version.dwBuildNumber) {
    case 17763: // 1809
        return 0x0278;
    case 18363: // 1909
        return 0x0280;
    case 19041: // 2004
    case 19569: // 20H2
    case 20180: // 21H1
        return 0x0388;
    default:
        return 0x0388; // Default for newer versions
    }
}

// Get process CR3 value (replaces your old one)
ULONG64 GetProcessCr3(PEPROCESS Process) {
    // kVarBit is your DIRECTORY_TABLE_BASE_OFFSET
    ULONG64 CR3 = *(ULONG64*)((PUCHAR)Process + DIRECTORY_TABLE_BASE_OFFSET);
    if (!CR3) CR3 = *(ULONG64*)((PUCHAR)Process + GetDirectoryTableOffset());
    return CR3;
}

template <typename str_type, typename str_type_2>
__forceinline bool crt_strcmp(str_type str, str_type_2 in_str, bool two)
{
    if (!str || !in_str)
        return false;

    wchar_t c1, c2;
#define to_lower(c_char) ((c_char >= 'A' && c_char <= 'Z') ? (c_char + 32) : c_char)

    do
    {
        c1 = *str++; c2 = *in_str++;
        c1 = to_lower(c1); c2 = to_lower(c2);

        if (!c1 && (two ? !c2 : 1))
            return true;

    } while (c1 == c2);

    return false;
}

// Structure for reading memory from a process (matches usermode)
typedef struct _Request
{
    HANDLE process_id;
    HANDLE WriteRequest;
    PVOID target;
    PVOID buffer;
    SIZE_T size;
} Request, * PRequest;

typedef struct _NtReadRequest
{
    HANDLE process_id;
    PVOID target;
    SIZE_T size;
} NtReadRequest, * PNtReadRequest;

// --- END ADDED STRUCTURES ---

// Structure for getting pid by name (matches usermode PID_PACK)
typedef struct _PID_PACK
{
    UINT32 pid;
    WCHAR name[1024];
} PID_PACK, * P_PID_PACK;

typedef struct _PEB_PACK_KM {
    UINT32 pid;
    UINT64 peb;    // out
} PEB_PACK_KM, * PPEB_PACK_KM;

//===============[ KERNEL MODULE STRUCTURES ]================
// Necessary for finding mouclass.sys base address
typedef struct _RTL_PROCESS_MODULE_INFORMATION {
    HANDLE Section;
    PVOID MappedBase;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION, * PRTL_PROCESS_MODULE_INFORMATION;

// The data packet the kernel passes around
typedef struct _KEYBOARD_INPUT_DATA {
    USHORT UnitId;
    USHORT MakeCode;
    USHORT Flags;
    USHORT Reserved;
    ULONG ExtraInformation;
} KEYBOARD_INPUT_DATA, * PKEYBOARD_INPUT_DATA;

typedef struct _KEY_REQUEST {
    USHORT MakeCode;
    USHORT Flags; // 0 = Down, 1 = Up
} KEY_REQUEST, * PKEY_REQUEST;

typedef struct _KBD_CLASS_DEVICE_EXTENSION {
    // 0x00 - 0x53: Standard Device Extension Header & PnP data
    UCHAR Padding_Header[0x54];

    // 0x54: The actual count of keys waiting (Accessed as DWORD)
    ULONG InputCount;                   // 0x54 (Code: *(_DWORD *)(ext + 84))

    // 0x58 - 0x67: Padding/Unknown
    UCHAR Padding_Mid[0x10];

    // 0x68: Buffer Boundaries & Pointers
    PKEYBOARD_INPUT_DATA InputDataStart;// 0x68 (Code: *(_QWORD *)(ext + 104))
    PKEYBOARD_INPUT_DATA DataIn;        // 0x70 (Write Ptr) (Code: *(_QWORD *)(ext + 112))
    PKEYBOARD_INPUT_DATA DataOut;       // 0x78 (Read Ptr)  (Code: *(_QWORD *)(ext + 120))

    // 0x80 - 0x8B: Padding/Unknown
    UCHAR Padding_Ptrs[0xC];

    // 0x8C: Buffer Size or Limit (Accessed as DWORD/ULONG)
    ULONG InputDataSize;                // 0x8C (Code: *(unsigned int *)(ext + 140))

    // 0x90 - 0x9F: Padding
    UCHAR Padding_PreLock[0x10];

    // 0xA0: The SpinLock (Correct in your struct)
    KSPIN_LOCK SpinLock;                // 0xA0 (Code: ext + 160)

    // 0xA8: Read Queue (LIST_ENTRY), not DataIn/DataOut
    LIST_ENTRY ReadQueue;               // 0xA8 (Code: ext + 168)
    // Flink at 0xA8, Blink at 0xB0

} KBD_CLASS_DEVICE_EXTENSION, * PKBD_CLASS_DEVICE_EXTENSION;

typedef struct _RTL_PROCESS_MODULES {
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES, * PRTL_PROCESS_MODULES;

// Structure for getting module address base (matches usermode MODULE_PACK)
typedef struct _MODULE_PACK_KM {
    UINT32 pid;
    UINT64 baseAddress;
    WCHAR moduleName[1024];
} MODULE_PACK_KM, * PMODULE_PACK_KM;

// Add these structures near your other kernel typedefs
typedef struct _BatchReadRequest {
    DWORD64 address;
    SIZE_T size;
    SIZE_T offset_in_buffer; // Offset where this read's data starts in the output buffer
} BatchReadRequest, * PBatchReadRequest;

typedef struct _BatchReadHeader {
    HANDLE process_id;
    UINT32 num_requests;
    SIZE_T total_buffer_size;
    // Followed by BatchReadRequest array in the input buffer
} BatchReadHeader, * PBatchReadHeader;

// This is the struct our usermode client will send
typedef struct _SCREENSHOT_HOOK_INFO
{
    HANDLE hScreenshotEvent; // Event for kernel to signal
    HANDLE hAckEvent;        // Event for usermode to signal back
    PVOID pSharedFlag;      // Usermode address of Settings::GlobalShowVisuals
} SCREENSHOT_HOOK_INFO, * PSCREENSHOT_HOOK_INFO;

// Kernel-space pointers to the objects
static PKEVENT g_pScreenshotEvent = nullptr;
static PKEVENT g_pAckEvent = nullptr;
static PVOID   g_pSharedFlag = nullptr; // This will be the UM address of Settings::GlobalShowVisuals
static PEPROCESS g_pClientProcess = nullptr; // The process that owns the handles

// We need to clear these when the client disconnects
VOID clear_screenshot_hook_objects()
{
    KLOG("[clear_ss_hook] Client disconnected, clearing objects.\n");
    if (g_pScreenshotEvent) {
        ObDereferenceObject(g_pScreenshotEvent);
        g_pScreenshotEvent = nullptr;
    }
    if (g_pAckEvent) {
        ObDereferenceObject(g_pAckEvent);
        g_pAckEvent = nullptr;
    }
    if (g_pClientProcess) {
        ObDereferenceObject(g_pClientProcess);
        g_pClientProcess = nullptr;
    }
    g_pSharedFlag = nullptr;
}

typedef struct _SYSTEM_MODULE {
    ULONG_PTR Reserved[2];
    PVOID Base;
    ULONG Size;
    ULONG Flags;
    USHORT Index;
    USHORT Unknown;
    USHORT LoadCount;
    USHORT ModuleNameOffset;
    CHAR ImageName[256];
} SYSTEM_MODULE, * PSYSTEM_MODULE;

// Define the request struct for the IOCTL
typedef struct _CONTROLLER_REQUEST {
    USHORT Buttons;
    SHORT  LeftTrigger;
    SHORT  RightTrigger;
    SHORT  ThumbLX;
    SHORT  ThumbLY;
    SHORT  ThumbRX;
    SHORT  ThumbRY;
} CONTROLLER_REQUEST, * PCONTROLLER_REQUEST;

typedef struct _SYSTEM_MODULE_INFORMATION {
    ULONG_PTR ulModuleCount;
    SYSTEM_MODULE Modules[1];
} SYSTEM_MODULE_INFORMATION, * PSYSTEM_MODULE_INFORMATION;

// FIX: Undefined here to prevent conflict with enum member of the same name.
#undef SystemModuleInformation

// Manually define the function pointer type for the mouse service callback
typedef VOID(*MouseClassServiceCallback)(PDEVICE_OBJECT, PMOUSE_INPUT_DATA, PMOUSE_INPUT_DATA, PULONG);

// Manually declare missing kernel variables and functions
extern "C" {
    extern POBJECT_TYPE* IoDriverObjectType;
    NTKERNELAPI NTSTATUS ObReferenceObjectByName(
        _In_ PUNICODE_STRING ObjectName,
        _In_ ULONG Attributes,
        _In_opt_ PACCESS_STATE AccessState,
        _In_opt_ ACCESS_MASK DesiredAccess,
        _In_ POBJECT_TYPE ObjectType,
        _In_ KPROCESSOR_MODE AccessMode,
        _Inout_opt_ PVOID ParseContext,
        _Out_ PVOID* Object
    );
}

typedef enum _SYSTEM_INFORMATION_CLASS
{
    SystemInformationClassMin = 0,
    SystemBasicInformation = 0,
    SystemProcessorInformation = 1,
    SystemPerformanceInformation = 2,
    SystemTimeOfDayInformation = 3,
    SystemPathInformation = 4,
    SystemNotImplemented1 = 4,
    SystemProcessInformation = 5,
    SystemProcessesAndThreadsInformation = 5,
    SystemCallCountInfoInformation = 6,
    SystemCallCounts = 6,
    SystemDeviceInformation = 7,
    SystemConfigurationInformation = 7,
    SystemProcessorPerformanceInformation = 8,
    SystemProcessorTimes = 8,
    SystemFlagsInformation = 9,
    SystemGlobalFlag = 9,
    SystemCallTimeInformation = 10,
    SystemNotImplemented2 = 10,
    SystemModuleInformation = 11,
    SystemLocksInformation = 12,
    SystemLockInformation = 12,
    SystemStackTraceInformation = 13,
    SystemNotImplemented3 = 13,
    SystemPagedPoolInformation = 14,
    SystemNotImplemented4 = 14,
    SystemNonPagedPoolInformation = 15,
    SystemNotImplemented5 = 15,
    SystemHandleInformation = 16,
    SystemObjectInformation = 17,
    SystemPageFileInformation = 18,
    SystemPagefileInformation = 18,
    SystemVdmInstemulInformation = 19,
    SystemInstructionEmulationCounts = 19,
    SystemVdmBopInformation = 20,
    SystemInvalidInfoClass1 = 20,
    SystemFileCacheInformation = 21,
    SystemCacheInformation = 21,
    SystemPoolTagInformation = 22,
    SystemInterruptInformation = 23,
    SystemProcessorStatistics = 23,
    SystemDpcBehaviourInformation = 24,
    SystemDpcInformation = 24,
    SystemFullMemoryInformation = 25,
    SystemNotImplemented6 = 25,
    SystemLoadImage = 26,
    SystemUnloadImage = 27,
    SystemTimeAdjustmentInformation = 28,
    SystemTimeAdjustment = 28,
    SystemSummaryMemoryInformation = 29,
    SystemNotImplemented7 = 29,
    SystemNextEventIdInformation = 30,
    SystemNotImplemented8 = 30,
    SystemEventIdsInformation = 31,
    SystemNotImplemented9 = 31,
    SystemCrashDumpInformation = 32,
    SystemExceptionInformation = 33,
    SystemCrashDumpStateInformation = 34,
    SystemKernelDebuggerInformation = 35,
    SystemContextSwitchInformation = 36,
    SystemRegistryQuotaInformation = 37,
    SystemLoadAndCallImage = 38,
    SystemPrioritySeparation = 39,
    SystemPlugPlayBusInformation = 40,
    SystemNotImplemented10 = 40,
    SystemDockInformation = 41,
    SystemNotImplemented11 = 41,
    SystemInvalidInfoClass2 = 42,
    SystemProcessorSpeedInformation = 43,
    SystemInvalidInfoClass3 = 43,
    SystemCurrentTimeZoneInformation = 44,
    SystemTimeZoneInformation = 44,
    SystemLookasideInformation = 45,
    SystemSetTimeSlipEvent = 46,
    SystemCreateSession = 47,
    SystemDeleteSession = 48,
    SystemInvalidInfoClass4 = 49,
    SystemRangeStartInformation = 50,
    SystemVerifierInformation = 51,
    SystemAddVerifier = 52,
    SystemSessionProcessesInformation = 53,
    SystemInformationClassMax
} SYSTEM_INFORMATION_CLASS;

typedef struct _SYSTEM_THREAD_INFORMATION {
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER CreateTime;
    ULONG WaitTime;
    PVOID StartAddress;
    CLIENT_ID ClientId;
    KPRIORITY Priority;
    LONG BasePriority;
    ULONG ContextSwitches;
    ULONG ThreadState;
    KWAIT_REASON WaitReason;
} SYSTEM_THREAD_INFORMATION, * PSYSTEM_THREAD_INFORMATION;

typedef struct _SYSTEM_PROCESS_INFORMATION {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    KPRIORITY BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
    ULONG_PTR UniqueProcessKey;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER ReadOperationCount;
    LARGE_INTEGER WriteOperationCount;
    LARGE_INTEGER OtherOperationCount;
    LARGE_INTEGER ReadTransferCount;
    LARGE_INTEGER WriteTransferCount;
    LARGE_INTEGER OtherTransferCount;
    SYSTEM_THREAD_INFORMATION Threads[1];
} SYSTEM_PROCESS_INFORMATION, * PSYSTEM_PROCESS_INFORMATION;


typedef struct _PEB_LDR_DATA {
    ULONG Length;
    UCHAR Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} PEB_LDR_DATA, * PPEB_LDR_DATA;

typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG Flags;
    USHORT LoadCount;
    USHORT TlsIndex;
    union {
        LIST_ENTRY HashLinks;
        struct {
            PVOID SectionPointer;
            ULONG CheckSum;
        }Info;
    };
    union {
        ULONG TimeDateStamp;
        PVOID LoadedImports;
    };
    PVOID EntryPointActivationContext;
    PVOID PatchInformation;
} LDR_DATA_TABLE_ENTRY, * PLDR_DATA_TABLE_ENTRY;

typedef struct _PEB {
    UCHAR InheritedAddressSpace;
    UCHAR ReadImageFileExecOptions;
    UCHAR BeingDebugged;
    UCHAR BitField;
    PVOID Mutant;
    PVOID ImageBaseAddress;
    PPEB_LDR_DATA Ldr;
    // ... other fields truncated for brevity
} PEB, * PPEB;


// Define the structures for each level of the x64 page table hierarchy
typedef struct _PML4E
{
    union
    {
        struct
        {
            UINT64 Present : 1;
            UINT64 ReadWrite : 1;
            UINT64 UserSupervisor : 1;
            UINT64 PageWriteThrough : 1;
            UINT64 PageCacheDisable : 1;
            UINT64 Accessed : 1;
            UINT64 Ignored : 1;
            UINT64 PageSize : 1; // Must be 0 for PML4E
            UINT64 Ignored2 : 4;
            UINT64 PageFrameNumber : 36;
            UINT64 Reserved : 4;
            UINT64 Ignored3 : 11;
            UINT64 ExecuteDisable : 1;
        };
        UINT64 Value;
    };
} PML4E, * PPML4E;

typedef struct _PDPTE
{
    union
    {
        struct
        {
            UINT64 Present : 1;
            UINT64 ReadWrite : 1;
            UINT64 UserSupervisor : 1;
            UINT64 PageWriteThrough : 1;
            UINT64 PageCacheDisable : 1;
            UINT64 Accessed : 1;
            UINT64 Ignored : 1;
            UINT64 PageSize : 1; // 1 for 1GB pages
            UINT64 Ignored2 : 4;
            UINT64 PageFrameNumber : 36;
            UINT64 Reserved : 4;
            UINT64 Ignored3 : 11;
            UINT64 ExecuteDisable : 1;
        };
        UINT64 Value;
    };
} PDPTE, * PPDPTE;

typedef struct _PDE
{
    union
    {
        struct
        {
            UINT64 Present : 1;
            UINT64 ReadWrite : 1;
            UINT64 UserSupervisor : 1;
            UINT64 PageWriteThrough : 1;
            UINT64 PageCacheDisable : 1;
            UINT64 Accessed : 1;
            UINT64 Ignored : 1;
            UINT64 PageSize : 1; // 1 for 2MB pages
            UINT64 Ignored2 : 4;
            UINT64 PageFrameNumber : 36;
            UINT64 Reserved : 4;
            UINT64 Ignored3 : 11;
            UINT64 ExecuteDisable : 1;
        };
        UINT64 Value;
    };
} PDE, * PPDE;

typedef struct _PTE
{
    union
    {
        struct
        {
            UINT64 Present : 1;
            UINT64 ReadWrite : 1;
            UINT64 UserSupervisor : 1;
            UINT64 PageWriteThrough : 1;
            UINT64 PageCacheDisable : 1;
            UINT64 Accessed : 1;
            UINT64 Dirty : 1;
            UINT64 PageAttributeTable : 1;
            UINT64 Global : 1;
            UINT64 Ignored : 3;
            UINT64 PageFrameNumber : 36;
            UINT64 Reserved : 4;
            UINT64 Ignored2 : 7;
            UINT64 ProtectionKey : 4;
            UINT64 ExecuteDisable : 1;
        };
        UINT64 Value;
    };
} PTE, * PPTE;



// simple kernel circular log buffer
static CHAR g_log_buffer[4096];
static ULONG g_log_write_pos = 0;
static ULONG g_log_read_pos = 0;
static KSPIN_LOCK g_log_lock;

// debug print configuration - use DbgPrintEx so dbgview can pick up reliably
#ifdef DEBUG
#define debug_print(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, fmt, __VA_ARGS__)
#else
#define debug_print(fmt, ...) ((void)0)
#endif

// Kernel-mode safe wide char to ANSI conversion
NTSTATUS WideCharToAnsi(PWCHAR WideString, PCHAR AnsiString, ULONG AnsiStringLength)
{
    UNICODE_STRING unicodeString;
    ANSI_STRING ansiString;
    NTSTATUS status;

    RtlInitUnicodeString(&unicodeString, WideString);

    ansiString.Buffer = AnsiString;
    ansiString.Length = 0;
    ansiString.MaximumLength = (USHORT)AnsiStringLength;

    status = RtlUnicodeStringToAnsiString(&ansiString, &unicodeString, FALSE);

    if (NT_SUCCESS(status)) {
        if (ansiString.Length < AnsiStringLength) {
            AnsiString[ansiString.Length] = '\0';
        }
        else {
            AnsiString[AnsiStringLength - 1] = '\0';
        }
    }

    return status;
}

// Helper: lowercase ASCII in-place
static inline void ascii_str_lower(char* s) {
    for (; *s; ++s) if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + 32);
}

// Helper: trim ".exe" suffix if present (ANSI)
static inline void trim_exe_suffix(char* s) {
    size_t len = strlen(s);
    if (len > 4) {
        char* suf = s + len - 4;
        if (!_stricmp(suf, ".exe")) {
            *suf = '\0';
        }
    }
}
// safe formatted append
VOID log_write_kernel(const char* fmt, ...)
{
    KIRQL oldIrql;
    va_list args;
    va_start(args, fmt);
    CHAR temp[1024];
    int len = _vsnprintf_s(temp, sizeof(temp), _TRUNCATE, fmt, args);
    va_end(args);
    if (len <= 0) return;

    // also keep system debug output for backwards compatibility
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "%s", temp);

    KeAcquireSpinLock(&g_log_lock, &oldIrql);
    // write as many bytes as fit, simple circular write
    for (int i = 0; i < len; ++i) {
        g_log_buffer[g_log_write_pos] = temp[i];
        g_log_write_pos = (g_log_write_pos + 1) % sizeof(g_log_buffer);
        // in case buffer overtakes read_pos, advance read_pos to avoid overlap
        if (g_log_write_pos == g_log_read_pos) {
            g_log_read_pos = (g_log_read_pos + 1) % sizeof(g_log_buffer);
        }
    }
    // append newline
    g_log_buffer[g_log_write_pos] = '\n';
    g_log_write_pos = (g_log_write_pos + 1) % sizeof(g_log_buffer);
    if (g_log_write_pos == g_log_read_pos) g_log_read_pos = (g_log_read_pos + 1) % sizeof(g_log_buffer);
    KeReleaseSpinLock(&g_log_lock, oldIrql);
}
NTSTATUS handle_get_logs(PIRP irp, SIZE_T* information)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PVOID user_buffer = irp->AssociatedIrp.SystemBuffer;
    ULONG output_buffer_length = stack->Parameters.DeviceIoControl.OutputBufferLength;
    *information = 0;

    if (!user_buffer || output_buffer_length == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_log_lock, &oldIrql);

    ULONG bytes_to_copy = 0;
    // Calculate the number of unread bytes in the circular buffer
    if (g_log_write_pos >= g_log_read_pos) {
        bytes_to_copy = g_log_write_pos - g_log_read_pos;
    }
    else {
        // The write pointer has wrapped around
        bytes_to_copy = (sizeof(g_log_buffer) - g_log_read_pos) + g_log_write_pos;
    }

    // Ensure we don't write past the end of the user's buffer
    bytes_to_copy = min(bytes_to_copy, output_buffer_length);

    if (bytes_to_copy > 0) {
        // Handle the case where the data wraps around the end of our log buffer
        if (g_log_write_pos < g_log_read_pos) {
            ULONG first_chunk_size = sizeof(g_log_buffer) - g_log_read_pos;
            if (first_chunk_size > bytes_to_copy) {
                first_chunk_size = bytes_to_copy;
            }
            RtlCopyMemory(user_buffer, &g_log_buffer[g_log_read_pos], first_chunk_size);

            ULONG second_chunk_size = bytes_to_copy - first_chunk_size;
            if (second_chunk_size > 0) {
                RtlCopyMemory((PCHAR)user_buffer + first_chunk_size, &g_log_buffer[0], second_chunk_size);
            }
        }
        else { // The data is in one contiguous block
            RtlCopyMemory(user_buffer, &g_log_buffer[g_log_read_pos], bytes_to_copy);
        }
        // Advance the read pointer to mark these logs as "read"
        g_log_read_pos = (g_log_read_pos + bytes_to_copy) % sizeof(g_log_buffer);
    }
    KeReleaseSpinLock(&g_log_lock, oldIrql);

    *information = bytes_to_copy;
    return STATUS_SUCCESS;
}
// In helpers.h
UINT64 translate_virtual_address(UINT64 directory_table_base_phys, UINT64 virtual_address)
{
    if (!directory_table_base_phys) return 0;

    UINT64 final_pa = 0;

    __try {
        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (LONGLONG)directory_table_base_phys;
        PML4E* pml4_table = (PML4E*)MmGetVirtualForPhysical(pa);
        if (!pml4_table) return 0;

        USHORT pml4_index = (USHORT)((virtual_address >> 39) & 0x1FF);
        PML4E pml4_entry = pml4_table[pml4_index];
        if (!pml4_entry.Present) return 0;

        pa.QuadPart = (LONGLONG)(pml4_entry.PageFrameNumber << 12);
        PDPTE* pdpt_table = (PDPTE*)MmGetVirtualForPhysical(pa);
        if (!pdpt_table) return 0;

        USHORT pdpt_index = (USHORT)((virtual_address >> 30) & 0x1FF);
        PDPTE pdpt_entry = pdpt_table[pdpt_index];
        if (!pdpt_entry.Present) return 0;
        if (pdpt_entry.PageSize) {
            final_pa = (pdpt_entry.PageFrameNumber << 30) + (virtual_address & 0x3FFFFFFF);
            return final_pa;
        }

        pa.QuadPart = (LONGLONG)(pdpt_entry.PageFrameNumber << 12);
        PDE* pd_table = (PDE*)MmGetVirtualForPhysical(pa);
        if (!pd_table) return 0;

        USHORT pd_index = (USHORT)((virtual_address >> 21) & 0x1FF);
        PDE pd_entry = pd_table[pd_index];
        if (!pd_entry.Present) return 0;
        if (pd_entry.PageSize) {
            final_pa = (pd_entry.PageFrameNumber << 21) + (virtual_address & 0x1FFFFF);
            return final_pa;
        }

        pa.QuadPart = (LONGLONG)(pd_entry.PageFrameNumber << 12);
        PTE* pt_table = (PTE*)MmGetVirtualForPhysical(pa);
        if (!pt_table) return 0;

        USHORT pt_index = (USHORT)((virtual_address >> 12) & 0x1FF);
        PTE pt_entry = pt_table[pt_index];
        if (!pt_entry.Present) return 0;

        final_pa = (pt_entry.PageFrameNumber << 12) + (virtual_address & 0xFFF);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // If we try to access an invalid physical page mapping during the walk, catch it safely
        final_pa = 0;
    }

    return final_pa;
}
HANDLE GetPID(const wchar_t* process_name)
{
    if (!process_name) return NULL;

    // Create ANSI copy of target
    CHAR target_ansi[260] = { 0 };
    if (!NT_SUCCESS(WideCharToAnsi((PWCHAR)process_name, target_ansi, sizeof(target_ansi)))) {
        return NULL;
    }
    ascii_str_lower(target_ansi);
    trim_exe_suffix(target_ansi);

    ULONG length = 0;
    ZwQuerySystemInformation(SystemProcessInformation, NULL, 0, &length);
    if (length == 0) return NULL;

    PVOID buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, length, 'prcB');
    if (!buffer) return NULL;

    NTSTATUS status = ZwQuerySystemInformation(SystemProcessInformation, buffer, length, &length);
    if (!NT_SUCCESS(status)) {
        ExFreePool(buffer);
        return NULL;
    }

    PSYSTEM_PROCESS_INFORMATION procInfo = (PSYSTEM_PROCESS_INFORMATION)buffer;
    HANDLE foundPid = NULL;

    for (;;) {
        if (procInfo->ImageName.Buffer && procInfo->ImageName.Length > 0) {
            CHAR procNameAnsi[260] = { 0 };
            SIZE_T chars = procInfo->ImageName.Length / sizeof(WCHAR);
            if (chars >= 260) chars = 259;

            UNICODE_STRING tmp;
            tmp.Buffer = procInfo->ImageName.Buffer;
            tmp.Length = (USHORT)(chars * sizeof(WCHAR));
            tmp.MaximumLength = tmp.Length;

            if (NT_SUCCESS(WideCharToAnsi(tmp.Buffer, procNameAnsi, sizeof(procNameAnsi)))) {
                ascii_str_lower(procNameAnsi);
                trim_exe_suffix(procNameAnsi);

                if (strcmp(procNameAnsi, target_ansi) == 0) {
                    foundPid = procInfo->UniqueProcessId;
                    debug_print("[GetPID] Found pid: %p\n", foundPid);
                    break;
                }
            }
        }

        if (procInfo->NextEntryOffset == 0) break;
        procInfo = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)procInfo + procInfo->NextEntryOffset);
    }

    ExFreePool(buffer);
    return foundPid;
}

NTSTATUS handle_attach(PIRP irp, SIZE_T* information) {
    Request* request = (Request*)irp->AssociatedIrp.SystemBuffer;
    if (!request) {
        debug_print("[handle_attach] Invalid request pointer\n");
        *information = 0;
        return STATUS_INVALID_PARAMETER;
    }

    debug_print("[handle_attach] Attaching to pid=%p\n", request->process_id);

    PEPROCESS temp_process = nullptr;
    NTSTATUS status = PsLookupProcessByProcessIdPtr((HANDLE)request->process_id, &temp_process);

    if (NT_SUCCESS(status)) {
        if (cached_process && cached_pid != request->process_id) {
            debug_print("[handle_attach] Clearing old cached_process (pid=%p)\n", cached_pid);
            ObDereferenceObject(cached_process);
            cached_process = nullptr;
        }

        cached_process = temp_process;
        cached_pid = request->process_id;

        debug_print("[handle_attach] Cached process set to %p (pid=%p)\n", cached_process, cached_pid);
    }
    else {
        debug_print("[handle_attach] PsLookupProcessByProcessId failed: 0x%x\n", status);
    }

    *information = (NT_SUCCESS(status)) ? sizeof(Request) : 0;
    return status;
}

NTSTATUS handle_get_pid(PIRP irp, SIZE_T* information) {
    P_PID_PACK pidPack = (P_PID_PACK)irp->AssociatedIrp.SystemBuffer;

    if (pidPack && pidPack->name[0] != 0) {
        HANDLE processId = GetPID(pidPack->name);
        pidPack->pid = HandleToULong(processId);

        NTSTATUS status = (processId != NULL) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
        *information = sizeof(PID_PACK);
        return status;
    }
    else {
        *information = 0;
        return STATUS_INVALID_PARAMETER;
    }
}


// Add this function to helpers.h
ULONG64 get_module_base_x64(PEPROCESS proc, UNICODE_STRING module_name)
{
    KAPC_STATE apcState;
    KeStackAttachProcess(proc, &apcState);

    ULONG64 baseAddress = 0;

    __try {
        PPEB peb = PsGetProcessPeb(proc);
        if (peb) {
            PPEB_LDR_DATA ldr = (PPEB_LDR_DATA)peb->Ldr;
            if (ldr) {
                PLIST_ENTRY list = ldr->InLoadOrderModuleList.Flink;
                PLIST_ENTRY head = &ldr->InLoadOrderModuleList;

                while (list != head) {
                    PLDR_DATA_TABLE_ENTRY entry = CONTAINING_RECORD(list, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

                    if (RtlCompareUnicodeString(&entry->BaseDllName, &module_name, TRUE) == 0) {
                        debug_print("[get_module_base_x64] Found module!\n");
                        baseAddress = (ULONG64)entry->DllBase;
                        break;
                    }

                    list = list->Flink;
                }
            } else {
                debug_print("[get_module_base_x64] No LDR.\n");
            }
        } else {
            debug_print("[get_module_base_x64] No PEB.\n");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        debug_print("Exception caught while parsing PEB.\n");
    }

    KeUnstackDetachProcess(&apcState);

    if (baseAddress == 0) {
        debug_print("[get_module_base_x64] Module not found in list.\n");
    }

    return baseAddress;
}

// ── Safe variant for callers already attached via KeStackAttachProcess ─────────
// Calling get_module_base_x64() from inside an existing KeStackAttachProcess
// causes a nested attach on the same process. The inner detach unwinds the outer
// attach context, corrupting PsGetProcessPeb() and silently returning NULL.
// Use this variant whenever the caller has already attached to proc.
// ──────────────────────────────────────────────────────────────────────────────
ULONG64 get_module_base_x64_attached(PEPROCESS proc, UNICODE_STRING module_name)
{
    ULONG64 baseAddress = 0;

    __try {
        PPEB peb = PsGetProcessPeb(proc);
        if (peb) {
            PPEB_LDR_DATA ldr = (PPEB_LDR_DATA)peb->Ldr;
            if (ldr) {
                PLIST_ENTRY list = ldr->InLoadOrderModuleList.Flink;
                PLIST_ENTRY head = &ldr->InLoadOrderModuleList;

                ULONG guard = 0;
                while (list != head && guard < 1024) {
                    PLDR_DATA_TABLE_ENTRY entry = CONTAINING_RECORD(list, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

                    if (entry->DllBase && entry->BaseDllName.Buffer &&
                        RtlCompareUnicodeString(&entry->BaseDllName, &module_name, TRUE) == 0) {
                        baseAddress = (ULONG64)entry->DllBase;
                        break;
                    }

                    list = list->Flink;
                    guard++;
                }
            } else {
                KLOG("[get_module_base_x64_attached] PEB->Ldr is NULL for proc %p", proc);
            }
        } else {
            KLOG("[get_module_base_x64_attached] PsGetProcessPeb returned NULL for proc %p", proc);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        KLOG("[get_module_base_x64_attached] Exception 0x%X while walking PEB", GetExceptionCode());
    }

    if (baseAddress == 0) {
        KLOG("[get_module_base_x64_attached] Module not found: %wZ", &module_name);
    }

    return baseAddress;
}

ULONG64 get_image_base(PEPROCESS proc) {
    if (!proc) return 0;
    // This is the correct and version-independent way to get the base address.
    return (ULONG64)PsGetProcessSectionBaseAddress(proc);
}

NTSTATUS handle_get_image_base(PIRP irp, SIZE_T* information) {
    PMODULE_PACK_KM modulePack = (PMODULE_PACK_KM)irp->AssociatedIrp.SystemBuffer;
    if (!modulePack || modulePack->pid == 0) {
        *information = 0;
        return STATUS_INVALID_PARAMETER;
    }

    PEPROCESS targetProcess;
    NTSTATUS status = PsLookupProcessByProcessIdPtr((HANDLE)modulePack->pid, &targetProcess);
    if (!NT_SUCCESS(status)) {
        *information = 0;
        return status;
    }

    ULONG64 base = get_image_base(targetProcess);
    ObDereferenceObject(targetProcess);

    modulePack->baseAddress = base;
    *information = sizeof(MODULE_PACK_KM);

    return (base != 0) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

// Add a new handler for the new IOCTL
NTSTATUS handle_get_module_by_peb(PIRP irp, SIZE_T* information) {
    PMODULE_PACK_KM modulePack = (PMODULE_PACK_KM)irp->AssociatedIrp.SystemBuffer;
    PEPROCESS targetProcess;
    NTSTATUS status;

    if (!modulePack || modulePack->pid == 0) {
        *information = 0;
        return STATUS_INVALID_PARAMETER;
    }

    status = PsLookupProcessByProcessIdPtr((HANDLE)modulePack->pid, &targetProcess);
    if (!NT_SUCCESS(status)) {
        *information = 0;
        return status;
    }

    UNICODE_STRING moduleName;
    RtlInitUnicodeString(&moduleName, modulePack->moduleName);

    // Call the kernel-side PEB walk function here.
    PVOID baseAddr = (PVOID)get_module_base_x64(targetProcess, moduleName);

    ObDereferenceObject(targetProcess);

    modulePack->baseAddress = (UINT64)baseAddr;
    status = (baseAddr != nullptr) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
    *information = sizeof(MODULE_PACK_KM);
    return status;
}

// REPLACE the old handle_read function with this new version
NTSTATUS handle_read(PIRP irp, SIZE_T* information) {
    Request* request = (Request*)irp->AssociatedIrp.SystemBuffer;
    *information = 0;

    if (!request || request->size == 0 || !request->process_id) {
        if (request) KLOG("[handle_read] Invalid request parameter pid=%p size=%llu\n", request->process_id, request->size);
        return STATUS_INVALID_PARAMETER;
    }

    PEPROCESS target_process = nullptr;
    NTSTATUS status;

    // --- START: MODIFIED LOGIC ---
    // Check if the requested PID matches our cache
    if (cached_process && request->process_id == cached_pid) {
        target_process = cached_process;
        // Increment the reference count because we're using this object
        ObReferenceObject(target_process);
        status = STATUS_SUCCESS;
        KLOG("[handle_read] Using cached process %p\n", target_process);
    }
    else {
        // If not cached, perform the slow lookup using safe ptr
        status = PsLookupProcessByProcessIdPtr((HANDLE)request->process_id, &target_process);
    }
    // --- END: MODIFIED LOGIC ---

    if (!NT_SUCCESS(status)) {
        KLOG("[handle_read] PsLookupProcessByProcessIdPtr failed: 0x%X\n", status);
        return status;
    }

    KAPC_STATE apc_state;
    SIZE_T bytes_copied = 0;

    KeStackAttachProcess(target_process, &apc_state);

    __try {
        MM_COPY_ADDRESS source_address = { 0 };
        source_address.VirtualAddress = request->target;
        status = MmCopyMemory(irp->AssociatedIrp.SystemBuffer, source_address, request->size, MM_COPY_MEMORY_VIRTUAL, &bytes_copied);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
        KLOG("[handle_read] Exception 0x%X while reading memory from %p\n", status, request->target);
        bytes_copied = 0;
    }

    KeUnstackDetachProcess(&apc_state);

    // Decrement the reference count to balance the ObReferenceObject or PsLookupProcessByProcessId
    ObDereferenceObject(target_process);

    if (NT_SUCCESS(status)) {
        *information = bytes_copied;
    }

    return status;
}

NTSTATUS handle_batch_read(PIRP irp, PIO_STACK_LOCATION stack_irp, SIZE_T* information)
{
    // 1. Initial validation
    PBatchReadHeader header = (PBatchReadHeader)irp->AssociatedIrp.SystemBuffer;
    *information = 0;

    // ADDED a check to limit the number of requests to 4096
    if (!header || !header->process_id || header->num_requests == 0 || header->num_requests > 4096) {
        return STATUS_INVALID_PARAMETER;
    }

    // Validate buffer sizes
    SIZE_T expected_input_size = sizeof(BatchReadHeader) + (header->num_requests * sizeof(BatchReadRequest));
    if (stack_irp->Parameters.DeviceIoControl.InputBufferLength < expected_input_size) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (stack_irp->Parameters.DeviceIoControl.OutputBufferLength < header->total_buffer_size) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    // 2. Get target process and DirectoryTableBase for page walking
    PEPROCESS target_process = nullptr;
    NTSTATUS status = PsLookupProcessByProcessIdPtr((HANDLE)header->process_id, &target_process);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    UINT64 directory_table_base = 0;
    __try {
        directory_table_base = *(UINT64*)((PUCHAR)target_process + DIRECTORY_TABLE_BASE_OFFSET);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    if (directory_table_base == 0) {
        ObDereferenceObject(target_process);
        return STATUS_UNSUCCESSFUL;
    }

    // *** THE FIX: Allocate a temporary kernel buffer to store results safely ***
    PVOID temp_result_buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, header->total_buffer_size, 'bRdP');
    if (!temp_result_buffer) {
        ObDereferenceObject(target_process);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(temp_result_buffer, header->total_buffer_size); // Zero out the buffer initially

    SIZE_T total_bytes_actually_copied = 0;

    // 3. Attach to process and execute reads
    KAPC_STATE apc_state;
    KeStackAttachProcess(target_process, &apc_state);

    PBatchReadRequest requests = (PBatchReadRequest)((PUCHAR)header + sizeof(BatchReadHeader));

    for (UINT32 i = 0; i < header->num_requests; ++i) {
        PBatchReadRequest req = nullptr;
        __try {
            req = &requests[i];
            volatile ULONG64 dummy = req->address;
            UNREFERENCED_PARAMETER(dummy);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            KLOG("[handle_batch_read] Exception accessing request array at index %d\n", i);
            break;
        }

        if ((req->offset_in_buffer + req->size) > header->total_buffer_size) continue;

        SIZE_T bytes_read_for_this_req = 0;
        while (bytes_read_for_this_req < req->size) {
            UINT64 current_va = req->address + bytes_read_for_this_req;
            UINT64 physical_address = translate_virtual_address(directory_table_base, current_va);
            if (physical_address == 0) break;

            SIZE_T bytes_left_on_page = 0x1000 - (current_va & 0xFFF);
            SIZE_T bytes_to_copy_now = min(bytes_left_on_page, req->size - bytes_read_for_this_req);

            MM_COPY_ADDRESS source_address;
            source_address.PhysicalAddress.QuadPart = (LONGLONG)physical_address;
            SIZE_T bytes_copied_this_chunk = 0;

            // Write to our safe temporary buffer, not the I/O buffer
            PUCHAR destination_ptr = (PUCHAR)temp_result_buffer + req->offset_in_buffer + bytes_read_for_this_req;

            NTSTATUS read_status = MmCopyMemory(
                destination_ptr, source_address, bytes_to_copy_now,
                MM_COPY_MEMORY_PHYSICAL, &bytes_copied_this_chunk
            );

            if (NT_SUCCESS(read_status) && bytes_copied_this_chunk > 0) {
                bytes_read_for_this_req += bytes_copied_this_chunk;
            }
            else {
                break;
            }
        }
        total_bytes_actually_copied += bytes_read_for_this_req;
    }

    KeUnstackDetachProcess(&apc_state);
    ObDereferenceObject(target_process);

    // 4. Copy the results from our safe temporary buffer to the final output buffer
    if (total_bytes_actually_copied > 0) {
        RtlCopyMemory(irp->AssociatedIrp.SystemBuffer, temp_result_buffer, total_bytes_actually_copied);
    }

    ExFreePool(temp_result_buffer); // Clean up

    *information = total_bytes_actually_copied;
    return STATUS_SUCCESS;
}
// ========================================================================
// === MODIFIED FUNCTION AS REQUESTED =====================================
// ========================================================================
NTSTATUS handle_get_peb(PIRP irp, SIZE_T* information)
{
    // Get the request packet from the IRP system buffer
    PPEB_PACK_KM request = (PPEB_PACK_KM)irp->AssociatedIrp.SystemBuffer;
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    // Validate the incoming request buffer and PID
    if (!request || request->pid == 0) {
        *information = 0;
        return STATUS_INVALID_PARAMETER;
    }

    PEPROCESS target_process = nullptr;
    // Look up the EPROCESS structure by the provided Process ID
    status = PsLookupProcessByProcessIdPtr((HANDLE)request->pid, &target_process);

    if (!NT_SUCCESS(status)) {
        request->peb = 0;
        *information = 0;
        return status;
    }

    KAPC_STATE apcState;
    KeStackAttachProcess(target_process, &apcState);

    __try {
        request->peb = (ULONG64)PsGetProcessPeb(target_process);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        request->peb = 0;
    }

    KeUnstackDetachProcess(&apcState);

    // Release the reference to the process object
    ObDereferenceObject(target_process);

    if (request->peb) {
        status = STATUS_SUCCESS;
    }
    else {
        status = STATUS_NOT_FOUND;
    }

    // Set the amount of data being returned to the caller
    *information = sizeof(PEB_PACK_KM);

    return status;
}
NTSTATUS handle_write(PIRP irp, SIZE_T* information) {
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    Request* request = (Request*)irp->AssociatedIrp.SystemBuffer;

    if (!request) {
        debug_print("[handle_write] Invalid request pointer\n");
        *information = 0;
        return STATUS_INVALID_PARAMETER;
    }

    debug_print("[handle_write] PID=%p, Addr=%p, Size=%llu\n",
        request->process_id, request->target, request->size);

    if (request->size == 0 || request->size > 0x100000) {
        debug_print("[handle_write] Size invalid\n");
        *information = 0;
        return STATUS_INVALID_PARAMETER;
    }

    if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(Request) + request->size) {
        debug_print("[handle_write] Input buffer too small\n");
        *information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    ULONG_PTR target_addr = (ULONG_PTR)request->target;
    if (target_addr < 0x10000 || target_addr > 0xFFFFFFFFFFFFULL) {
        debug_print("[handle_write] Addr out of range: %llx\n", target_addr);
        *information = 0;
        return STATUS_INVALID_PARAMETER;
    }

    PVOID source_data = (PVOID)((ULONG_PTR)request + sizeof(Request));

    PEPROCESS target_process = cached_process;
    NTSTATUS status = STATUS_SUCCESS;

    if (!target_process) {
        status = PsLookupProcessByProcessIdPtr((HANDLE)request->process_id, &target_process);
        debug_print("[handle_write] PsLookup returned %x for pid %p\n", status, request->process_id);
        if (!NT_SUCCESS(status)) {
            *information = 0;
            return status;
        }
    }
    else {
        ObReferenceObject(target_process);
        debug_print("[handle_write] Using cached_process %p\n", target_process);
    }

    SIZE_T bytes_copied = 0;

    status = MmCopyVirtualMemory(PsGetCurrentProcess(), source_data, target_process, request->target, request->size, KernelMode, &bytes_copied);

    debug_print("[handle_write] MmCopyVirtualMemory status=0x%x, bytes=%llu\n", status, bytes_copied);

    ObDereferenceObject(target_process);

    *information = 0;

    if (NT_SUCCESS(status) && bytes_copied == request->size) {
        return STATUS_SUCCESS;
    }
    else {
        return status;
    }
}
// THIS IS THE ONLY WAY. WE ARE MANUALLY WRITING TO A MEMORY OFFSET.
// THIS VERSION FIXES THE SYNTAX ERRORS FROM THE LAST ATTEMPT.
// It still uses the hardcoded offset hack.
NTSTATUS handle_nt_read(PIRP irp, SIZE_T* information)
{
    // CORRECTED: Use the '.' operator here.
    PNtReadRequest request = (PNtReadRequest)irp->AssociatedIrp.SystemBuffer;
    *information = 0;

    if (!request || !request->process_id || !request->target || request->size == 0) {
        KLOG("[handle_nt_read] Invalid request parameter.\n");
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS status = STATUS_UNSUCCESSFUL;
    SIZE_T bytesRead = 0;

    const ULONG_PTR PREVIOUS_MODE_OFFSET = 0x232;
    PUCHAR pThread = (PUCHAR)KeGetCurrentThread();
    CHAR oldMode = *(pThread + PREVIOUS_MODE_OFFSET);

    __try
    {
        *(pThread + PREVIOUS_MODE_OFFSET) = KernelMode;

        UNICODE_STRING routineName;
        RtlInitUnicodeString(&routineName, L"NtReadVirtualMemory");

        // This typedef must match the function signature EXACTLY.
        typedef NTSTATUS(*pNtReadVirtualMemory)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
        pNtReadVirtualMemory NtReadVirtualMemory_ptr = (pNtReadVirtualMemory)MmGetSystemRoutineAddress(&routineName);

        if (NtReadVirtualMemory_ptr) {
            status = NtReadVirtualMemory_ptr(
                request->process_id,
                request->target,
                irp->AssociatedIrp.SystemBuffer, // CORRECTED: Use '.' here too.
                request->size,
                &bytesRead // CORRECTED: Added the missing argument here.
            );
        }
        else {
            KLOG("[handle_nt_read] CRITICAL: Failed to find NtReadVirtualMemory via MmGetSystemRoutineAddress.\n");
            status = STATUS_NOT_FOUND;
        }
    }
    __finally
    {
        *(pThread + PREVIOUS_MODE_OFFSET) = oldMode;
    }

    if (NT_SUCCESS(status)) {
        *information = bytesRead;
    }

    return status;
}
NTSTATUS handle_page_walk_read(PIRP irp, SIZE_T* information)
{
    Request* request = (Request*)irp->AssociatedIrp.SystemBuffer;
    *information = 0;

    if (!request || !request->process_id || !request->target || !request->size)
        return STATUS_INVALID_PARAMETER;

    PEPROCESS target_process = NULL;
    // Use our NEW global function pointer
    NTSTATUS status = PsLookupProcessByProcessIdPtr((HANDLE)request->process_id, &target_process);
    if (!NT_SUCCESS(status)) return status;

    // Get the target CR3 using the new, more robust function
    ULONG64 target_cr3 = GetProcessCr3(target_process);
    if (!target_cr3) {
        ObDereferenceObject(target_process);
        return STATUS_UNSUCCESSFUL;
    }

    // We are NOT attaching. We're in the driver's context.
    // Get the current processor index to use the correct spy page from the PageList array.
    UINT32 processor_index = KeGetCurrentProcessorIndex();

    PUCHAR out_buffer = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    UINT64 va_start = (UINT64)request->target;
    SIZE_T size_total = request->size;
    SIZE_T read_total = 0;

    while (read_total < size_total)
    {
        ULONG64 current_va = va_start + read_total;

        // Get the physical address using the spy page's page table walker
        ULONG64 physical_address = ReadPageMemory::TransformationCR3(
            processor_index,
            target_cr3,
            current_va
        );

        if (!physical_address) {
            status = STATUS_PARTIAL_COPY; // Couldn't translate the address
            break;
        }

        // Calculate how much we can read from this single page
        SIZE_T bytes_remaining_on_page = PAGE_SIZE - (physical_address & 0xFFF);
        SIZE_T bytes_to_read_now = min(bytes_remaining_on_page, size_total - read_total);

        // Read from the physical address using the spy page PTE remapping
        ReadPageMemory::ReadPhysicalAddress(
            processor_index,
            physical_address,
            out_buffer + read_total,
            bytes_to_read_now
        );

        read_total += bytes_to_read_now;
    }

    ObDereferenceObject(target_process); // Release our process reference

    *information = read_total;

    if (read_total == size_total) return STATUS_SUCCESS;
    if (read_total > 0) return STATUS_PARTIAL_COPY;

    return STATUS_UNSUCCESSFUL;
}
//
// Helper function to walk the page tables and return a pointer to the PTE
// for a given virtual address. Returns nullptr if the address is not valid,
// part of a large page, or if any table entry is not present.
//
PPTE get_pte_for_virtual_address(PEPROCESS target_process, PVOID virtual_address)
{
    if (!target_process) return nullptr;

    // Get the Directory Table Base (physical address of PML4 table) from the EPROCESS structure
    UINT64 directory_table_base = 0;
    __try {
        directory_table_base = *(UINT64*)((PUCHAR)target_process + DIRECTORY_TABLE_BASE_OFFSET);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        KLOG("[get_pte] Exception accessing DirectoryTableBase!");
        return nullptr;
    }
    if (directory_table_base == 0) return nullptr;

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (LONGLONG)directory_table_base;

    // 1. Get PML4 Entry
    PML4E* pml4_table = (PML4E*)MmGetVirtualForPhysical(pa);
    if (!pml4_table) return nullptr;
    USHORT pml4_index = (USHORT)(((UINT64)virtual_address >> 39) & 0x1FF);
    PML4E pml4_entry = pml4_table[pml4_index];
    if (!pml4_entry.Present) return nullptr;

    // 2. Get PDPT Entry
    pa.QuadPart = (LONGLONG)(pml4_entry.PageFrameNumber << 12);
    PDPTE* pdpt_table = (PDPTE*)MmGetVirtualForPhysical(pa);
    if (!pdpt_table) return nullptr;
    USHORT pdpt_index = (USHORT)(((UINT64)virtual_address >> 30) & 0x1FF);
    PDPTE pdpt_entry = pdpt_table[pdpt_index];
    if (!pdpt_entry.Present) return nullptr;
    if (pdpt_entry.PageSize) { // This technique doesn't work on 1GB large pages
        KLOG("[get_pte] Address is on a 1GB large page, cannot get PTE.");
        return nullptr;
    }

    // 3. Get PDE Entry
    pa.QuadPart = (LONGLONG)(pdpt_entry.PageFrameNumber << 12);
    PDE* pd_table = (PDE*)MmGetVirtualForPhysical(pa);
    if (!pd_table) return nullptr;
    USHORT pd_index = (USHORT)(((UINT64)virtual_address >> 21) & 0x1FF);
    PDE pd_entry = pd_table[pd_index];
    if (!pd_entry.Present) return nullptr;
    if (pd_entry.PageSize) { // This technique doesn't work on 2MB large pages
        KLOG("[get_pte] Address is on a 2MB large page, cannot get PTE.");
        return nullptr;
    }

    // 4. Get PTE Entry
    pa.QuadPart = (LONGLONG)(pd_entry.PageFrameNumber << 12);
    PTE* pt_table = (PTE*)MmGetVirtualForPhysical(pa);
    if (!pt_table) return nullptr;
    USHORT pt_index = (USHORT)(((UINT64)virtual_address >> 12) & 0x1FF);
    PTE pt_entry = pt_table[pt_index];
    if (!pt_entry.Present) return nullptr;

    // We found it. Return the virtual address of the PTE itself.
    return &pt_table[pt_index];
}


//
// Main function to write to a target process's memory by manually
// manipulating page table entries for maximum stealth.
//
NTSTATUS write_by_page_walk(PEPROCESS target_process, PVOID target_address, PVOID source_buffer, SIZE_T size)
{
    if (!target_process || !target_address || !source_buffer || size == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    KAPC_STATE apc_state;
    // We MUST be in the context of the target process to use the virtual address for writing
    KeStackAttachProcess(target_process, &apc_state);

    SIZE_T bytes_written = 0;
    NTSTATUS status = STATUS_SUCCESS;

    // This loop handles writes that cross page boundaries. Fucking crucial.
    while (bytes_written < size)
    {
        PVOID current_target_va = (PVOID)((UINT64)target_address + bytes_written);
        PVOID current_source_va = (PVOID)((UINT64)source_buffer + bytes_written);

        // Figure out how many bytes we can write on the current page
        SIZE_T bytes_on_page = PAGE_SIZE - ((UINT64)current_target_va & (PAGE_SIZE - 1));
        SIZE_T bytes_to_copy = min(bytes_on_page, size - bytes_written);

        // --- FAULT IN THE PAGE ---
        // Ensure the page is physically present before attempting to read its PTE.
        // We read to fault it in, and write back to trigger Copy-On-Write (allocating a private physical page).
        __try {
            volatile CHAR* p = (volatile CHAR*)current_target_va;
            CHAR touch = *p; // Fault in for read (maps zero page or file-backed page)
            *p = touch;      // Trigger COW to allocate a private physical page
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // If writing fails (e.g. read-only page), that's fine. The read already faulted it in.
        }

        PPTE pte_ptr = get_pte_for_virtual_address(target_process, current_target_va);
        if (!pte_ptr) {
            KLOG("[write_by_page_walk] Failed to get PTE for VA %p (Target %p, bytes written so far: %llu)", current_target_va, target_address, bytes_written);
            status = STATUS_NOT_FOUND;
            break; // Abort the whole operation if any part of the memory is invalid
        }

        // --- THE DIRTY WORK ---
        PTE old_pte = *pte_ptr;
        PTE temp_pte = old_pte;
        temp_pte.ReadWrite = 1; // Force write permissions

        // Atomically swap in our writable PTE.
        _InterlockedExchange64((PLONG64)pte_ptr, temp_pte.Value);

        // Flush the CPU's cache to make sure it sees our change.
        __invlpg(current_target_va);

        // --- DO THE WRITE ---



        __try {
            RtlCopyMemory(current_target_va, current_source_va, bytes_to_copy);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            status = GetExceptionCode();
            KLOG("[write_by_page_walk] Exception 0x%X during write to %p!", status, current_target_va);
            // Even if the write fails, we MUST restore the PTE.
        }



        // --- CLEAN UP YOUR SHIT ---
        // Atomically swap the original PTE back.
        _InterlockedExchange64((PLONG64)pte_ptr, old_pte.Value);

        // Flush the cache again to restore original protections.
        __invlpg(current_target_va);

        if (!NT_SUCCESS(status)) {
            KLOG("[write_by_page_walk] Aborting loop. Status 0x%X at offset %llu", status, bytes_written);
            break; // If the write failed, get the fuck out.
        }

        bytes_written += bytes_to_copy;
    }

    KeUnstackDetachProcess(&apc_state);

    // If we didn't write everything we were supposed to, it's not a success.
    if (NT_SUCCESS(status) && bytes_written != size) {
        KLOG("[write_by_page_walk] Partial copy. Wrote %llu of %llu bytes.", bytes_written, size);
        status = STATUS_PARTIAL_COPY;
    } else if (NT_SUCCESS(status)) {
        KLOG("[write_by_page_walk] Success! Wrote %llu bytes to %p", size, target_address);
    }

    return status;
}
// Add this entire function to helpers.h, maybe near your other "handle_" functions.

NTSTATUS handle_page_walk_write(PIRP irp, SIZE_T* information) {
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    Request* request = (Request*)irp->AssociatedIrp.SystemBuffer;
    *information = 0; // Default to 0 bytes processed

    // --- Basic Validation ---
    if (!request || !stack) {
        return STATUS_INVALID_PARAMETER;
    }

    // Check if the input buffer from usermode is large enough to hold the request struct AND the data to be written.
    if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(Request) + request->size) {
        KLOG("[pg_walk_write] Input buffer too small.");
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (request->size == 0 || request->process_id == 0 || !request->target) {
        KLOG("[pg_walk_write] Invalid request parameters.");
        return STATUS_INVALID_PARAMETER;
    }

    // --- Core Logic ---
    PEPROCESS target_process = nullptr;
    NTSTATUS status = PsLookupProcessByProcessIdPtr((HANDLE)request->process_id, &target_process);

    if (!NT_SUCCESS(status)) {
        KLOG("[pg_walk_write] PsLookupProcessByProcessIdPtr failed: 0x%X", status);
        return status;
    }

    // The actual data to write is located immediately after the Request struct in the system buffer.
    PVOID source_data = (PVOID)((ULONG_PTR)request + sizeof(Request));

    // Call our shiny new function to do the actual stealth work.
    status = write_by_page_walk(target_process, request->target, source_data, request->size);

    // We're done with the process object, so release our reference to it.
    ObDereferenceObject(target_process);

    return status;
}
NTSTATUS handle_get_module_list(PIRP irp, PIO_STACK_LOCATION stack_irp, SIZE_T* information)
{
    // Input buffer contains the Request struct with the PID
    Request* request = (Request*)irp->AssociatedIrp.SystemBuffer;
    *information = 0; // Default to 0 bytes written

    if (!request || request->process_id == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    // Get output buffer details (METHOD_OUT_DIRECT uses MdlAddress)
    if (!irp->MdlAddress) {
        return STATUS_INVALID_PARAMETER;
    }
    PVOID user_buffer = MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);
    ULONG output_buffer_length = stack_irp->Parameters.DeviceIoControl.OutputBufferLength;

    if (!user_buffer || output_buffer_length == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    PEPROCESS target_process = nullptr;
    NTSTATUS status = PsLookupProcessByProcessIdPtr((HANDLE)request->process_id, &target_process);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    KLOG("[get_module_list] Enumerating modules for PID: %p", request->process_id);

    // Attach to the process context to safely read its PEB memory
    KAPC_STATE apc_state;
    KeStackAttachProcess(target_process, &apc_state);

    SIZE_T bytes_written = 0;
    status = STATUS_SUCCESS;

    __try {
        PPEB peb = PsGetProcessPeb(target_process);
        if (!peb) {
            KLOG("[get_module_list] Failed to get PEB.");
            status = STATUS_NOT_FOUND;
            __leave;
        }

        // Access PEB->Ldr
        PPEB_LDR_DATA ldr = peb->Ldr;
        if (!ldr) {
            KLOG("[get_module_list] PEB->Ldr is null.");
            status = STATUS_NOT_FOUND;
            __leave; // Exit __try block
        }

        // Get the head of the module list
        PLIST_ENTRY list_head = &ldr->InLoadOrderModuleList;
        PLIST_ENTRY list_entry = list_head->Flink;

        // Iterate through the doubly-linked list
        while (list_entry != list_head)
        {
            PLDR_DATA_TABLE_ENTRY entry = CONTAINING_RECORD(list_entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

            // Check if there is enough space in the output buffer for one more entry
            if (bytes_written + sizeof(MODULE_ENTRY_INFO) > output_buffer_length) {
                status = STATUS_BUFFER_TOO_SMALL; // Not all modules will fit
                break; // Stop iterating
            }

            // Get the virtual address where we will write this entry in the user buffer
            PMODULE_ENTRY_INFO current_out_entry = (PMODULE_ENTRY_INFO)((PUCHAR)user_buffer + bytes_written);

            // Copy data to the output buffer
            current_out_entry->baseAddress = (UINT64)entry->DllBase;
            current_out_entry->size = entry->SizeOfImage;

            // Safely copy the module name
            if (entry->BaseDllName.Buffer && entry->BaseDllName.Length > 0) {
                // Calculate bytes to copy, ensuring not to overflow our struct's buffer
                ULONG bytes_to_copy = min(entry->BaseDllName.Length, (MAX_MODULE_ENTRY_NAME - 1) * sizeof(WCHAR));
                RtlCopyMemory(current_out_entry->moduleName, entry->BaseDllName.Buffer, bytes_to_copy);
                current_out_entry->moduleName[bytes_to_copy / sizeof(WCHAR)] = L'\0'; // Null-terminate
            }
            else {
                current_out_entry->moduleName[0] = L'\0'; // Set empty string if no name
            }

            bytes_written += sizeof(MODULE_ENTRY_INFO); // Increment bytes written
            list_entry = list_entry->Flink; // Move to the next module
        }

    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        KLOG("[get_module_list] Exception while walking PEB Ldr: 0x%X", GetExceptionCode());
        status = GetExceptionCode();
        bytes_written = 0; // Indicate failure
    }

    KeUnstackDetachProcess(&apc_state);
    ObDereferenceObject(target_process);

    *information = bytes_written; // Set the total bytes written
    KLOG("[get_module_list] Wrote %llu bytes for %llu modules.", bytes_written, bytes_written / sizeof(MODULE_ENTRY_INFO));
    return status;
}
//
// This is the new IOCTL handler
//
NTSTATUS handle_register_screenshot_hook(PIRP irp, SIZE_T* information)
{
    PSCREENSHOT_HOOK_INFO info = (PSCREENSHOT_HOOK_INFO)irp->AssociatedIrp.SystemBuffer;
    *information = 0;

    if (!info) {
        return STATUS_INVALID_PARAMETER;
    }

    // First, clear any old objects just in case
    clear_screenshot_hook_objects();

    NTSTATUS status = STATUS_SUCCESS;
    PVOID tempEvent1 = nullptr;
    PVOID tempEvent2 = nullptr;
    PEPROCESS pClientProcess = IoGetCurrentProcess(); // Get the process that sent this IOCTL
    ObReferenceObject(pClientProcess); // Add a reference so we can use it later

    // We must be attached to the client process's context to get kernel handles from its user-mode handles.
    KAPC_STATE apc_state;
    KeStackAttachProcess(pClientProcess, &apc_state);

    __try
    {
        // Get kernel-space pointers from the usermode handles
        status = ObReferenceObjectByHandle(
            info->hScreenshotEvent,
            EVENT_MODIFY_STATE, // We need to be able to set this event
            *ExEventObjectType,
            UserMode, // The handle is from UserMode
            &tempEvent1,
            NULL
        );
        if (!NT_SUCCESS(status)) {
            KLOG("[ss_hook] ObReferenceObjectByHandle (ScreenshotEvent) failed: 0x%X\n", status);
            __leave;
        }

        status = ObReferenceObjectByHandle(
            info->hAckEvent,
            SYNCHRONIZE, // We only need to wait on this event
            *ExEventObjectType,
            UserMode, // The handle is from UserMode
            &tempEvent2,
            NULL
        );
        if (!NT_SUCCESS(status)) {
            KLOG("[ss_hook] ObReferenceObjectByHandle (AckEvent) failed: 0x%X\n", status);
            __leave;
        }

        // Probe the usermode address to make sure it's valid and writable from usermode
        // This is a sanity check, though our hook will write from kernel.
        if (info->pSharedFlag < (PVOID)0x10000 ||
            info->pSharedFlag >(PVOID)0x7FFFFFFFFFFF)
        {
            KLOG("[ss_hook] pSharedFlag address is invalid: %p\n", info->pSharedFlag);
            status = STATUS_ACCESS_VIOLATION;
            __leave;
        }

        // All good! Store the pointers globally.
        g_pScreenshotEvent = (PKEVENT)tempEvent1;
        g_pAckEvent = (PKEVENT)tempEvent2;
        g_pSharedFlag = info->pSharedFlag;
        g_pClientProcess = pClientProcess; // Store the referenced client process

        KLOG("[ss_hook] Successfully registered screenshot hook objects.\n");
        KLOG("[ss_hook]   Client EPROCESS    -> %p\n", g_pClientProcess);
        KLOG("[ss_hook]   g_pScreenshotEvent -> %p\n", g_pScreenshotEvent);
        KLOG("[ss_hook]   g_pAckEvent        -> %p\n", g_pAckEvent);
        KLOG("[ss_hook]   g_pSharedFlag (VA) -> %p\n", g_pSharedFlag);
    }
    __finally
    {
        KeUnstackDetachProcess(&apc_state);

        if (!NT_SUCCESS(status)) {
            // Something failed, clean up anything we might have referenced
            if (tempEvent1) ObDereferenceObject(tempEvent1);
            if (tempEvent2) ObDereferenceObject(tempEvent2);
            ObDereferenceObject(pClientProcess); // Dereference the client process on failure
        }
    }

    return status;
}

__forceinline PVOID kernel_get_proc_address_safe(PVOID pModuleBase, const char* sFunctionName)
{
    if (!pModuleBase) return NULL;

    PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)pModuleBase;
    PIMAGE_NT_HEADERS pNt;
    PIMAGE_EXPORT_DIRECTORY pExportDir;
    PULONG pFunctions;  // <-- FIX
    PULONG pNames;      // <-- FIX
    PUSHORT pOrdinals;  // <-- FIX

    __try
    {
        // Verify DOS header
        if (pDos->e_magic != IMAGE_DOS_SIGNATURE) {
            KLOG("[k_get_proc] Invalid DOS signature");

            return NULL;
        }

        //Verify NT header
        pNt = (PIMAGE_NT_HEADERS)((PUCHAR)pModuleBase + pDos->e_lfanew);
        if (pNt->Signature != IMAGE_NT_SIGNATURE) {
            KLOG("[k_get_proc] Invalid NT signature");

            return NULL;
        }

        //Get export directory
        if (pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress == 0) {
            KLOG("[k_get_proc] No export directory RVA");

            return NULL;
        }
        pExportDir = (PIMAGE_EXPORT_DIRECTORY)((PUCHAR)pModuleBase + pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
        if (!pExportDir->AddressOfFunctions) {
            KLOG("[k_get_proc] No export directory");

            return NULL;
        }

        //Get function/name/ordinal tables
        pFunctions = (PULONG)((PUCHAR)pModuleBase + pExportDir->AddressOfFunctions);  // <-- FIX
        pNames = (PULONG)((PUCHAR)pModuleBase + pExportDir->AddressOfNames);      // <-- FIX
        pOrdinals = (PUSHORT)((PUCHAR)pModuleBase + pExportDir->AddressOfNameOrdinals); // <-- FIX

        //Loop through names
        for (ULONG i = 0; i < pExportDir->NumberOfNames; i++) // <-- FIX
        {
            char* sCurrentName = (char*)((PUCHAR)pModuleBase + pNames[i]);

            // simple, safe string compare
            const char* pA = sCurrentName;
            const char* pB = sFunctionName;
            while (*pA && (*pA == *pB)) { pA++; pB++; }

            if (*pA == *pB) // if (strcmp(sCurrentName, sFunctionName) == 0)
            {
                // Found it. Get the ordinal.
                USHORT ordinal = pOrdinals[i]; // <-- FIX
                if (ordinal >= pExportDir->NumberOfFunctions) {

                    return NULL; // sanity check
                }

                // Get the function RVA
                ULONG functionRVA = pFunctions[ordinal]; // <-- FIX
                if (functionRVA == 0) {

                    return NULL; // sanity check
                }

                PVOID pFunction = (PVOID)((PUCHAR)pModuleBase + functionRVA);

                // --- THIS IS THE FORWARDER FIX ---
                // Check if the RVA is inside the export directory.
                // If it is, it's not a function, it's a forwarder string.
                ULONG exportDirStart = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress; // <-- FIX
                ULONG exportDirEnd = exportDirStart + pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size; // <-- FIX

                if (functionRVA >= exportDirStart && functionRVA < exportDirEnd)
                {
                    // This is a forwarded export.
                    KLOG("[k_get_proc] '%s' is a forwarded export, cannot resolve.", sFunctionName);

                    return NULL; // Fail.
                }

                // It's a real function. Return the address.

                return pFunction;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        KLOG("[k_get_proc] Exception parsing PE headers.");

        return NULL;
    }

    KLOG("[k_get_proc] Function '%s' not found in exports.", sFunctionName);

    return NULL;
}

NTSTATUS set_pages_rx_by_page_walk(PEPROCESS target_process, PVOID target_address, SIZE_T size)
{
    if (!target_process || !target_address || size == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    // We must be in the process context to map the virtual addresses
    KAPC_STATE apc_state;
    KeStackAttachProcess(target_process, &apc_state);

    NTSTATUS status = STATUS_SUCCESS;
    SIZE_T bytes_processed = 0;

    // We have to loop page by page
    for (SIZE_T offset = 0; offset < size; offset += PAGE_SIZE)
    {
        PVOID current_target_va = (PVOID)((UINT64)target_address + offset);

        // Uses your get_pte_for_virtual_address function
        PPTE pte_ptr = get_pte_for_virtual_address(target_process, current_target_va);
        if (!pte_ptr) {
            KLOG("[set_pages_rx] Failed to get PTE for VA %p", current_target_va);
            status = STATUS_NOT_FOUND;
            break;
        }

        // --- THE DIRTY WORK ---
        // We make a copy, modify it, then swap.
        PTE new_pte = *pte_ptr;

        new_pte.ReadWrite = 0;      // 0 = Read-Only
        new_pte.ExecuteDisable = 0; // <-- !! THIS IS THE FIX !!

        // Atomically swap in our new RX pte.
        // We are *not* restoring the old one. This is the new protection.
        _InterlockedExchange64((PLONG64)pte_ptr, new_pte.Value);

        // Flush the CPU's cache (TLB) for this page
        __invlpg(current_target_va);
    }

    KeUnstackDetachProcess(&apc_state);
    return status;
}

NTSTATUS handle_free_virtual_memory(PIRP irp, SIZE_T* information)
{
    pk_free_mem_request in = (pk_free_mem_request)irp->AssociatedIrp.SystemBuffer;
    *information = 0; // We don't return data for a free operation

    if (!in || !in->pid || !in->addr) {
        KLOG("[free_mem] Invalid request parameters.");
        return STATUS_INVALID_PARAMETER;
    }

    PEPROCESS target_proc = NULL;
    NTSTATUS status = PsLookupProcessByProcessIdPtr((HANDLE)in->pid, &target_proc);

    if (NT_SUCCESS(status)) {
        KLOG("[free_mem] Freeing 0x%llX in PID %lu", in->addr, in->pid);
        KAPC_STATE apc;
        KeStackAttachProcess(target_proc, &apc);

        PVOID base_addr = (PVOID)in->addr;
        SIZE_T region_size = 0; // MUST be 0 for MEM_RELEASE

        status = ZwFreeVirtualMemory(
            ZwCurrentProcess(),
            &base_addr,         // Ptr to the base address
            &region_size,       // Ptr to the size (0)
            MEM_RELEASE         // Free type
        );

        KeUnstackDetachProcess(&apc);
        ObDereferenceObject(target_proc);

        if (!NT_SUCCESS(status)) {
            KLOG("[free_mem] ZwFreeVirtualMemory failed: 0x%X", status);
        }
        else {
            KLOG("[free_mem] Successfully freed memory.");
        }
    }
    else {
        KLOG("[free_mem] PsLookupProcessByProcessIdPtr failed: 0x%X", status);
    }

    // *information is already 0, which is correct
    return status;
}

VOID NTAPI kernel_apc_stub(
    PKAPC Apc,
    PKNORMAL_ROUTINE* NormalRoutine, // This will point to our shellcode
    PVOID* NormalContext,           // This will point to our params
    PVOID* SystemArgument1,
    PVOID* SystemArgument2
) {
    //
    // --- !! THE MONEY LOG !! ---
    // If you see this in your driver logs, it means the OS
    // successfully delivered the APC and is about to run your shellcode.
    //
    if (NormalRoutine && *NormalRoutine) {
        KLOG("[kernel_apc_stub] APC FIRED! About to run NormalRoutine (shellcode) at %p.", *NormalRoutine);
    }
    else {
        KLOG("[kernel_apc_stub] APC FIRED! But NormalRoutine was NULL!");
    }

    // Free the APC object we allocated back in the handler
    ExFreePoolWithTag(Apc, POOL_TAG);

    // Make sure the compiler doesn't complain
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
}


NTSTATUS handle_kernel_inject_apc(PIRP irp, SIZE_T* information)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PEPROCESS target_process = nullptr;
    PETHREAD target_thread = nullptr; // We'll use this inside the loop
    PKAPC apc = nullptr;             // We'll use this inside the loop

    PVOID remote_shellcode_base = nullptr;
    PVOID remote_params_base = nullptr;
    PVOID remote_dll_base = nullptr;
    PVOID remote_magic_byte = nullptr;

    // 1. Get request and buffers
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    KernelInjectApcRequest* request = (KernelInjectApcRequest*)irp->AssociatedIrp.SystemBuffer;
    if (!request || !stack) { *information = 0; return STATUS_BUFFER_TOO_SMALL; }

    // Calculate buffer offsets based on new struct
    SIZE_T tid_array_size = request->ThreadCount * sizeof(ULONG);
    PVOID dll_path_buffer = (PVOID)((PUCHAR)request->TargetTids + tid_array_size);
    PVOID shellcode_buffer = (PVOID)((PUCHAR)dll_path_buffer + request->PathSize);

    // 2. Get process
    status = PsLookupProcessByProcessIdPtr((HANDLE)request->TargetPid, &target_process);
    if (!NT_SUCCESS(status)) {
        KLOG("[kernel_inject_apc] Failed to lookup process: 0x%X", status);
        *information = 0;
        return status;
    }

    KLOG("[kernel_inject_apc] Targeting PID: %lu (%lu threads)", request->TargetPid, request->ThreadCount);

    KAPC_STATE apc_state;
    KeStackAttachProcess(target_process, &apc_state);

    __try {
        __try {
            // 4. Find module bases and function pointers
            // NOTE: Using _attached variant — we are already inside KeStackAttachProcess.
            // Calling the standard get_module_base_x64() here would create a nested attach
            // on the same process, corrupting the APC state and making PsGetProcessPeb() return NULL.
            UNICODE_STRING sKernel32, sUser32, sUcrtBase;
            RtlInitUnicodeString(&sKernel32, L"kernel32.dll");
            RtlInitUnicodeString(&sUser32, L"user32.dll");
            RtlInitUnicodeString(&sUcrtBase, L"ucrtbase.dll");

            PVOID pKernel32Base = (PVOID)get_module_base_x64_attached(target_process, sKernel32);
            PVOID pUser32Base   = (PVOID)get_module_base_x64_attached(target_process, sUser32);
            PVOID pUcrtBase     = (PVOID)get_module_base_x64_attached(target_process, sUcrtBase);
            if (!pKernel32Base || !pUser32Base) {
                KLOG("[kernel_inject_apc] Failed to find kernel32/user32 (k32=%p u32=%p)", pKernel32Base, pUser32Base);
                status = STATUS_NOT_FOUND;
                __leave;
            }

            PVOID pLoadLibraryA = kernel_get_proc_address_safe(pKernel32Base, "LoadLibraryA");
            PVOID pGetProcAddress = kernel_get_proc_address_safe(pKernel32Base, "GetProcAddress");
            if (!pLoadLibraryA || !pGetProcAddress) {
                KLOG("[kernel_inject_apc] Failed to find LoadLibraryA/GetProcAddress");
                status = STATUS_NOT_FOUND;
                __leave;
            }

            // 5. Allocate memory for path string (RW)
            if (request->PathSize > 0)
            {
                SIZE_T dll_alloc_size = request->PathSize;
                status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &remote_dll_base, 0, &dll_alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); // RW
                if (!NT_SUCCESS(status)) { KLOG("[kernel_inject_apc] Failed to allocate path memory: 0x%X", status); __leave; }
                KLOG("[kernel_inject_apc] Allocated path at %p (RW)", remote_dll_base);
            }

            // 6. Allocate memory for shellcode (RW)
            SIZE_T sc_alloc_size = request->ShellcodeSize;
            status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &remote_shellcode_base, 0, &sc_alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); // RW
            if (!NT_SUCCESS(status)) { KLOG("[kernel_inject_apc] Failed to allocate shellcode memory: 0x%X", status); __leave; }
            KLOG("[kernel_inject_apc] Allocated shellcode at %p (RW)", remote_shellcode_base);

            // 7. Allocate memory for magic byte (RW)
            SIZE_T magic_alloc_size = PAGE_SIZE;
            status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &remote_magic_byte, 0, &magic_alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); // RW
            if (!NT_SUCCESS(status)) { KLOG("[kernel_inject_apc] Failed to allocate magic byte memory: 0x%X", status); __leave; }
            KLOG("[kernel_inject_apc] Allocated magic byte at %p (RW)", remote_magic_byte);

            // 8. Allocate memory for params (RW)
            SIZE_T params_alloc_size = sizeof(ApcShellcodeParams);
            status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &remote_params_base, 0, &params_alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); // RW
            if (!NT_SUCCESS(status)) { KLOG("[kernel_inject_apc] Failed to allocate params memory: 0x%X", status); __leave; }
            KLOG("[kernel_inject_apc] Allocated params at %p (RW)", remote_params_base);

            // 9. Write all the data
            //
            // --- !! NOTE: This assumes your `write_by_page_walk` is solid !! ---
            // You can use your `write_by_page_walk` here instead of `RtlCopyMemory`
            // for extra stealth, but `RtlCopyMemory` is fine since we are already attached.
            // For simplicity, I'm leaving `RtlCopyMemory` as it's not the bottleneck.
            //



            __try {
                if (request->PathSize > 0) {
                    RtlCopyMemory(remote_dll_base, dll_path_buffer, request->PathSize);
                }
                RtlCopyMemory(remote_shellcode_base, shellcode_buffer, request->ShellcodeSize);

                ApcShellcodeParams local_params = { 0 };
                local_params.pLoadLibraryA = pLoadLibraryA;
                local_params.pGetProcAddress = pGetProcAddress;
                // ... (fill the rest of local_params) ...
                local_params.pMagicByte = remote_magic_byte;
                local_params.pDllBase = remote_dll_base;

                RtlCopyMemory(remote_params_base, &local_params, sizeof(local_params));
                KLOG("[kernel_inject_apc] All data written successfully.");
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                status = GetExceptionCode();
                KLOG("[kernel_inject_apc] Exception 0x%X while writing memory!", status);

                __leave;
            }



            // 10. --- !! THIS IS THE UPGRADE !! ---
            // Ditch the noisy ZwProtectVirtualMemory call...
            //status = ZwProtectVirtualMemory(ZwCurrentProcess(), &remote_shellcode_base, &protect_size, PAGE_EXECUTE_READ, &old_prot);

            // ...and use our new PTE flipper.
            // (Note: set_pages_rx_by_page_walk detaches, so we must re-attach)

            // We must detach *before* calling our helper, because it does its own attach.
            KeUnstackDetachProcess(&apc_state);

            KLOG("[kernel_inject_apc] Flipping shellcode %p to RX using PTEs...", remote_shellcode_base);
            status = set_pages_rx_by_page_walk(target_process, remote_shellcode_base, request->ShellcodeSize);

            if (!NT_SUCCESS(status)) {
                KLOG("[kernel_inject_apc] !! FAILED to change memory protection via PTE: 0x%X", status);
                // We have to bail here, but we're already detached.
                // The __finally block will handle the ObDereference.
                __leave;
            }
            KLOG("[kernel_inject_apc] Flipped shellcode to RX (PTE).");

            // Re-attach so the rest of the function (and the __finally) works
            KeStackAttachProcess(target_process, &apc_state);
            // --- !! UPGRADE COMPLETE !! ---

            // ── 11a. CFG Bitmap Registration ──────────────────────────────────────────
            // Windows CFG keeps a per-process bitmap. KiUserApcDispatcher checks this
            // bitmap before dispatching any user-mode APC. If the shellcode address has
            // no bit set, the dispatcher throws FAST_FAIL_GUARD_ICALL_CHECK_FAILURE and
            // kills the process — even if the shellcode pages are RX.
            //
            // Fix: call ZwProtectVirtualMemory with PAGE_TARGETS_VALID (0x40000000)
            // combined with the existing protection. This is the native Windows mechanism
            // for registering a new call target in the CFG bitmap. No custom structs
            // needed — the VirtualProtect layer handles the bitmap update internally.
            //
            // Non-CFG targets will return a non-success code here; that is non-fatal.
            // ACG-enforcing targets (some UWP) will also fail here — same fallback.
            // ──────────────────────────────────────────────────────────────────────────
            {
                ULONG  cfg_old_prot = 0;
                PVOID  cfg_base     = remote_shellcode_base;
                SIZE_T cfg_size     = (SIZE_T)request->ShellcodeSize;

                NTSTATUS cfg_status = ZwProtectVirtualMemory(
                    ZwCurrentProcess(),
                    &cfg_base,
                    &cfg_size,
                    PAGE_EXECUTE_READ | 0x40000000, // PAGE_TARGETS_VALID
                    &cfg_old_prot
                );

                if (NT_SUCCESS(cfg_status)) {
                    KLOG("[kernel_inject_apc] CFG bitmap updated. Shellcode at %p is now a valid APC target.", remote_shellcode_base);
                } else {
                    KLOG("[kernel_inject_apc] CFG bitmap update returned 0x%X (non-fatal — target may not enforce CFG or has ACG).", cfg_status);
                }
            }
            // ── End CFG Registration ──────────────────────────────────────────────────


            // 11. Kernel APC Injection (SHOTGUN)
            KLOG("[kernel_inject_apc] Starting shotgun APC injection for %lu threads...", request->ThreadCount);
            ULONG successful_apcs = 0;

            for (ULONG i = 0; i < request->ThreadCount; i++)
            {
                target_thread = nullptr; // Reset for loop
                apc = nullptr;           // Reset for loop
                status = PsLookupThreadByThreadId((HANDLE)request->TargetTids[i], &target_thread);

                if (!NT_SUCCESS(status)) {
                    KLOG("[kernel_inject_apc] Shotgun: Failed to lookup TID %lu (0x%X), skipping.", request->TargetTids[i], status);
                    continue; // Try next thread
                }

                apc = (PKAPC)ExAllocatePoolWithTag(NonPagedPool, sizeof(KAPC), POOL_TAG);
                if (!apc) {
                    KLOG("[kernel_inject_apc] Shotgun: Failed to allocate KAPC struct, skipping TID %lu.", request->TargetTids[i]);
                    ObDereferenceObject(target_thread);
                    continue; // Try next thread
                }

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

                if (KeInsertQueueApc(apc, remote_params_base, NULL, 0)) {
                    successful_apcs++;
                }
                else {
                    KLOG("[kernel_inject_apc] Shotgun: KeInsertQueueApc FAILED for TID %lu.", request->TargetTids[i]);
                    ExFreePoolWithTag(apc, POOL_TAG);
                }

                ObDereferenceObject(target_thread);
            } // --- End of for loop ---

            KLOG("[kernel_inject_apc] Shotgun complete. Successfully queued %lu APCs.", successful_apcs);

            if (successful_apcs > 0) {
                status = STATUS_SUCCESS; // We're good
            }
            else {
                KLOG("[kernel_inject_apc] !! FAILED to queue any APCs !!");
                status = STATUS_UNSUCCESSFUL; // None worked
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            status = GetExceptionCode();
            KLOG("[kernel_inject_apc] Exception in injection block: 0x%X", status);
        }
    }
    __finally {
        // Cleanup 
        if (!NT_SUCCESS(status)) {
            KLOG("[kernel_inject_apc] Injection failed, cleaning up allocated memory");
            // We must re-attach to free memory in the correct context
            // if the failure happened *after* we detached.
            KAPC_STATE cleanup_apc_state;
            KeStackAttachProcess(target_process, &cleanup_apc_state);

            SIZE_T free_size = 0;
            if (remote_dll_base) { ZwFreeVirtualMemory(ZwCurrentProcess(), &remote_dll_base, &free_size, MEM_RELEASE); }
            if (remote_shellcode_base) { ZwFreeVirtualMemory(ZwCurrentProcess(), &remote_shellcode_base, &free_size, MEM_RELEASE); }
            if (remote_magic_byte) { ZwFreeVirtualMemory(ZwCurrentProcess(), &remote_magic_byte, &free_size, MEM_RELEASE); }
            if (remote_params_base) { ZwFreeVirtualMemory(ZwCurrentProcess(), &remote_params_base, &free_size, MEM_RELEASE); }

            KeUnstackDetachProcess(&cleanup_apc_state);
        }

        // This will detach the *original* apc_state
        KeUnstackDetachProcess(&apc_state);
        if (target_process) ObDereferenceObject(target_process);
    }

    *information = 0;
    return status;
}



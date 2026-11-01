#pragma comment(lib, "ntoskrnl.lib")
#include "helpers.h"
#include "KernelManualMap.h"
#include <intrin.h> // Required for CR0 register manipulation
#include <aux_klib.h> // You might need to link aux_klib.lib in sources
// Or just use the manual generic scanner below if you don't want to link libs.
#include <ntstrsafe.h> 
#include <ntddmou.h> 
#include "controller_inject.h"

// IOCTL from ioctl.h
#define file_device_mirrore 0x3138
#define offset_io_mirrore   0x2056
#define ioctl_protect_sprite_content (ULONG)CTL_CODE(file_device_mirrore, offset_io_mirrore + 0x0010, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
// --- TYPE FIXES ---
typedef unsigned __int64 uint64_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef __int32 int32_t;
typedef unsigned __int8 uint8_t;

// Ensure this is visible to the namespace
extern "C" PLIST_ENTRY PsLoadedModuleList;
// Struct from structs.h
#pragma pack(push, 1) // Force 1-byte alignment (No padding)
typedef struct _protect_sprite_content {
    uint32_t value;          // 4 bytes
    uint64_t window_handle;  // 8 bytes
} protect_sprite_content, * pprotect_sprite_content;
#pragma pack(pop) // Restore default packing

// Change 'char' to 'uint32_t' or 'ULONG'
typedef int64_t(*gre_protect_sprite_content_t)(int64_t, uint64_t, int32_t, uint32_t);

// Need this to find the driver object since kbdhid devices are often unnamed
extern "C" NTSTATUS ObReferenceObjectByName(
    PUNICODE_STRING ObjectName,
    ULONG Attributes,
    PACCESS_STATE AccessState,
    ACCESS_MASK DesiredAccess,
    POBJECT_TYPE ObjectType,
    KPROCESSOR_MODE AccessMode,
    PVOID ParseContext,
    PVOID* Object
);

extern "C" POBJECT_TYPE* IoDriverObjectType;

// --- [FINAL FIX] HYBRID DEFINITIONS ---

// 1. BitBlt Signature (11 Args) - For Indices 2, 3, 4, 5, 6
typedef int(__fastcall* BitBlt_t)(PVOID, PVOID, PVOID, PVOID, PVOID, PVOID, PVOID, PVOID, PVOID, PVOID, ULONG);

// 2. TextOut Signature (10 Args) - For Index 0 ONLY
typedef int(__fastcall* TextOut_t)(PVOID, PVOID, PVOID, PVOID, PVOID, PVOID, PVOID, PVOID, PVOID, ULONG);

// Originals
TextOut_t Original_Func0 = NULL; // New Safe Original for Index 0
BitBlt_t  Original_Func2 = NULL;
BitBlt_t  Original_Func3 = NULL;
BitBlt_t  Original_Func4 = NULL;
BitBlt_t  Original_Func5 = NULL;
BitBlt_t  Original_Func6 = NULL;

// --- [FIX 1] DEFINITIONS AT THE TOP ---
// Define WDFREQUEST as PVOID immediately so everything below knows what it is.
typedef PVOID WDFREQUEST;

// The "XINH" Packet Structure
typedef struct _XINH_PACKET {
    LIST_ENTRY Link;          // Offset 0x00
    WDFREQUEST RequestHandle; // Offset 0x10
    PVOID      Context;       // Offset 0x18
} XINH_PACKET, * PXINH_PACKET;

// Xbox One Report Structure
#pragma pack(push, 1)
struct XBOX_INPUT_REPORT {
    UCHAR  ReportId;      // 0x01
    USHORT Buttons;       // Bitmask
    SHORT  LeftTrigger;   // 0-1023
    SHORT  RightTrigger;
    SHORT  ThumbLX;
    SHORT  ThumbLY;
    SHORT  ThumbRX;
    SHORT  ThumbRY;
};
#pragma pack(pop)

// FIX: Define BOOL/TRUE/FALSE if missing to prevent "undefined identifier" errors
#ifndef BOOL
typedef int BOOL;
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// 1. The Physical Device (PDEV) Driver Object
// Found via RE: *(this + 0x30) + 0xB08
struct INTERNAL_PDEV
{
    // Padding to reach the "Hook Point" at 0xB08 (2824 decimal)
    UCHAR Pad_Start[0x30];

    // THE TARGET: This is the function pointer for BitBlt.
    PVOID pfnBitBlt;
};

// 2. The Internal Surface Object
// Found via RE: SURFACE::pfnBitBlt logic
struct INTERNAL_SURFACE
{
    // 0x00 - 0x30: Padding
    UCHAR Pad_0x00[0x30];

    // 0x30: Pointer to the Hardware Driver Object (PDEV)
    INTERNAL_PDEV* pDev;

    // 0x38 - 0x70: Padding to reach the flags
    UCHAR Pad_0x38[0x38];

    // 0x70: The "Is this a Screen?" Flags
    // Code check: if (Flags & 1) -> Hardware Path
    ULONG Flags;
};

// 1. Explicit __fastcall to ensure stack balance matches Windows Kernel
typedef int(__fastcall* BitBlt_t)(
    PVOID psoTrg, PVOID psoSrc, PVOID psoMask, PVOID pco, PVOID pxlo,
    PVOID prclTrg, PVOID pptlSrc, PVOID pptlMask, PVOID pbo, PVOID pptlBrush,
    ULONG rop4
    );

// 2. Global for the original function
BitBlt_t Original_DriverBitBlt = NULL;

// ==================================================================================
//                              NEW: GRE PROTECT NAMESPACE (From Utils.h)
// ==================================================================================
namespace GreProtect {
    // [FIXED] Struct with Kernel Safe Types
    typedef struct _protect_sprite_content
    {
        ULONG value;           // Was uint32_t
        ULONGLONG window_handle; // Was uint64_t
    } protect_sprite_content, * pprotect_sprite_content;

    // [FIXED] Function Pointer Typedef
    typedef int64_t(*gre_protect_sprite_content_t)(int64_t, uint64_t, int32_t, char);

    // [FIXED] Global Variable Definition
    gre_protect_sprite_content_t GreProtectSpriteContent = nullptr;
    // Isolated struct to avoid conflict with existing _KLDR_DATA_TABLE_ENTRY
    typedef struct _KLDR_DATA_TABLE_ENTRY_FULL
    {
        LIST_ENTRY InLoadOrderLinks;
        void* ExceptionTable;
        unsigned long ExceptionTableSize;
        void* GpValue;
        void* NonPagedDebugInfo;
        void* DllBase;
        void* EntryPoint;
        unsigned long SizeOfImage;
        UNICODE_STRING FullDllName;
        UNICODE_STRING BaseDllName;
        // We only need up to BaseDllName for the scan logic
    } KLDR_DATA_TABLE_ENTRY_FULL, * PKLDR_DATA_TABLE_ENTRY_FULL;

#define to_rva(address, offset) address + (int32_t)((*(int32_t*)(address + offset) + offset) + sizeof(int32_t))

    UNICODE_STRING AnsiToUnicode(const char* str)
    {
        UNICODE_STRING unicode;
        ANSI_STRING ansi_str;
        RtlInitAnsiString(&ansi_str, str);
        RtlAnsiStringToUnicodeString(&unicode, &ansi_str, TRUE);
        return unicode;
    }

    PKLDR_DATA_TABLE_ENTRY_FULL GetLdrDataByName(const char* szmodule)
    {
        PKLDR_DATA_TABLE_ENTRY_FULL ldr_entry = nullptr;
        UNICODE_STRING mod = AnsiToUnicode(szmodule);

        PLIST_ENTRY ps_loaded_module_list = PsLoadedModuleList;
        if (!ps_loaded_module_list) return ldr_entry;

        auto current_ldr_entry = reinterpret_cast<PKLDR_DATA_TABLE_ENTRY_FULL>(ps_loaded_module_list->Flink);

        while (reinterpret_cast<PLIST_ENTRY>(current_ldr_entry) != ps_loaded_module_list)
        {
            if (!RtlCompareUnicodeString(&current_ldr_entry->BaseDllName, &mod, TRUE))
            {
                ldr_entry = current_ldr_entry;
                break;
            }
            current_ldr_entry = reinterpret_cast<PKLDR_DATA_TABLE_ENTRY_FULL>(current_ldr_entry->InLoadOrderLinks.Flink);
        }
        RtlFreeUnicodeString(&mod); // Free the string allocated by AnsiToUnicode
        return ldr_entry;
    }

    template <typename str_type, typename str_type_2>
    __forceinline bool CrtStrcmp(str_type str, str_type_2 in_str, bool two)
    {
#define to_lower(c_char) ((c_char >= 'A' && c_char <= 'Z') ? (c_char + 32) : c_char)
        if (!str || !in_str) return false;
        wchar_t c1, c2;
        do {
            c1 = *str++; c2 = *in_str++;
            c1 = to_lower(c1); c2 = to_lower(c2);
            if (!c1 && (two ? !c2 : 1)) return true;
        } while (c1 == c2);
        return false;
    }

    PIMAGE_SECTION_HEADER GetSectionHeader(const uintptr_t image_base, const char* section_name)
    {
        if (!image_base || !section_name) return nullptr;
        const auto pimage_dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(image_base);
        const auto pimage_nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS64>(image_base + pimage_dos_header->e_lfanew);
        auto psection = reinterpret_cast<PIMAGE_SECTION_HEADER>(pimage_nt_headers + 1);
        const auto number_of_sections = pimage_nt_headers->FileHeader.NumberOfSections;

        for (auto i = 0; i < number_of_sections; ++i) {
            if (CrtStrcmp(reinterpret_cast<const char*>(psection->Name), section_name, false)) {
                return psection;
            }
            ++psection;
        }
        return nullptr;
    }

    bool DataCompare(const char* pdata, const char* bmask, const char* szmask)
    {
        for (; *szmask; ++szmask, ++pdata, ++bmask) {
            if (*szmask == 'x' && *pdata != *bmask) return false;
        }
        return !*szmask;
    }

    uintptr_t FindPattern(const uintptr_t base, const size_t size, const char* bmask, const char* szmask)
    {
        for (size_t i = 0; i < size; ++i)
            if (DataCompare(reinterpret_cast<const char*>(base + i), bmask, szmask))
                return base + i;
        return 0;
    }

    uintptr_t FindPatternPageKm(const char* szmodule, const char* szsection, const char* bmask, const char* szmask)
    {
        if (!szmodule || !szsection || !bmask || !szmask) return 0;
        const auto* pldr_entry = GetLdrDataByName(szmodule);
        if (!pldr_entry) return 0;

        const auto module_base = reinterpret_cast<uintptr_t>(pldr_entry->DllBase);
        const auto* psection = GetSectionHeader(module_base, szsection);
        return psection ? FindPattern(module_base + psection->VirtualAddress, psection->Misc.VirtualSize, bmask, szmask) : 0;
    }

    NTSTATUS Init()
    {
        KLOG("[GreProtect] Scanning for GreProtectSpriteContent...");
        auto address = reinterpret_cast<PVOID>(FindPatternPageKm("win32kfull.sys", ".text",
            "\xE8\x00\x00\x00\x00\x8B\xF8\x85\xC0\x75\x0E", "x????xxxxxx"));

        // Windows 11 24H2 check
        if (!address) {
            address = reinterpret_cast<PVOID>(FindPatternPageKm("win32kfull.sys", ".text",
                "\xE8\x00\x00\x00\x00\x8B\xD8\x85\xC0\x75\x0E", "x????xxxxxx"));
        }

        if (!address) {
            KLOG("[GreProtect] FAILED: Pattern not found.");
            return STATUS_INVALID_ADDRESS;
        }

        // Resolve RVA to get the actual function address
        address = reinterpret_cast<PVOID>(to_rva(reinterpret_cast<uintptr_t>(address), 1));
        GreProtectSpriteContent = (gre_protect_sprite_content_t)address;

        KLOG("[GreProtect] SUCCESS: GreProtectSpriteContent found at %p", address);
        return STATUS_SUCCESS;
    }

    NTSTATUS ProtectFn(pprotect_sprite_content req)
    {
        // [V-LOGGING] HERE IS THE LOGGING YOU REQUESTED
        if (!req) return STATUS_INVALID_PARAMETER;

        KLOG("[GreProtect] IOCTL Request Received.");
        KLOG("   [-] Flags (Value): 0x%X", req->value);
        KLOG("   [-] Window Handle: 0x%llX", req->window_handle);

        if (!GreProtectSpriteContent) {
            KLOG("[GreProtect] FATAL: Function not initialized/found!");
            return STATUS_DEVICE_NOT_READY;
        }

        // Check if the handle looks reasonable (User pointers are usually 0x000000...)
        if (req->window_handle > 0xFFFFFF0000000000) {
            KLOG("[GreProtect] WARNING: Handle looks like a Kernel pointer? Check alignment.");
        }

        // 1 = WDA_EXCLUDEFROMCAPTURE (implied)
        // Passing 'req->value' as the last argument
        int64_t result = GreProtectSpriteContent(0, req->window_handle, 1, req->value);

        if (result) {
            KLOG("[GreProtect] SUCCESS: Window cloaked.");
            return STATUS_SUCCESS;
        }
        else {
            KLOG("[GreProtect] FAILED: Internal function returned false.");
            return STATUS_UNSUCCESSFUL;
        }
    }
}


// Helper to print legible logs
void LogCall(int Index, ULONG rop4) {
//
}

// --- [FIX] SEPARATE HOOKS ---

int __fastcall Hook_Index0(PVOID a1, PVOID a2, PVOID a3, PVOID a4, PVOID a5, PVOID a6, PVOID a7, PVOID a8, PVOID a9, ULONG a10)
{
    // TextOut - Silent
    if (Original_Func0) return Original_Func0(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    return 0;
}

int __fastcall Hook_Index2(PVOID a1, PVOID a2, PVOID a3, PVOID a4, PVOID a5, PVOID a6, PVOID a7, PVOID a8, PVOID a9, PVOID a10, ULONG a11)
{
    // BitBlt - CRITICAL HOT PATH - NO LOGGING
    if (Original_Func2) return Original_Func2(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
    return 0;
}

int __fastcall Hook_Index3(PVOID a1, PVOID a2, PVOID a3, PVOID a4, PVOID a5, PVOID a6, PVOID a7, PVOID a8, PVOID a9, PVOID a10, ULONG a11)
{
    if (Original_Func3) return Original_Func3(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
    return 0;
}

int __fastcall Hook_Index4(PVOID a1, PVOID a2, PVOID a3, PVOID a4, PVOID a5, PVOID a6, PVOID a7, PVOID a8, PVOID a9, PVOID a10, ULONG a11)
{
    if (Original_Func4) return Original_Func4(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
    return 0;
}

int __fastcall Hook_Index5(PVOID a1, PVOID a2, PVOID a3, PVOID a4, PVOID a5, PVOID a6, PVOID a7, PVOID a8, PVOID a9, PVOID a10, ULONG a11)
{
    if (Original_Func5) return Original_Func5(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
    return 0;
}

int __fastcall Hook_Index6(PVOID a1, PVOID a2, PVOID a3, PVOID a4, PVOID a5, PVOID a6, PVOID a7, PVOID a8, PVOID a9, PVOID a10, ULONG a11)
{
    if (Original_Func6) return Original_Func6(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
    return 0;
}


// Function signature for the original BitBlt (Using 'int' instead of 'BOOL' to fix errors)
typedef int (*BitBlt_t)(
    PVOID psoTrg, PVOID psoSrc, PVOID psoMask, PVOID pco, PVOID pxlo,
    PVOID prclTrg, PVOID pptlSrc, PVOID pptlMask, PVOID pbo, PVOID pptlBrush,
    ULONG rop4
    );

// FIX: Changed PCHAR -> const char* for Pattern and Mask
BOOLEAN CheckMask(PUCHAR Base, const char* Pattern, const char* Mask) {
    for (; *Mask; ++Base, ++Pattern, ++Mask) {
        if (*Mask == 'x' && *Base != *Pattern) return FALSE;
    }
    return TRUE;
}

// FIX: Changed PCHAR -> const char* for Pattern and Mask
PVOID FindPattern(PVOID Base, ULONG Size, const char* Pattern, const char* Mask) {
    ULONG SizeOfPattern = (ULONG)strlen(Mask);
    for (ULONG i = 0; i < Size - SizeOfPattern; i++) {
        if (CheckMask((PUCHAR)Base + i, Pattern, Mask)) {
            return (PVOID)((PUCHAR)Base + i);
        }
    }
    return NULL;
}

// Resolves a Relative Instruction (RIP-relative) to the actual address
PVOID ResolveRelativeAddress(PVOID Instruction, ULONG OffsetOffset, ULONG InstructionSize) {
    ULONG_PTR Instr = (ULONG_PTR)Instruction;
    LONG RipOffset = *(PLONG)(Instr + OffsetOffset);
    PVOID Target = (PVOID)(Instr + InstructionSize + RipOffset);
    return Target;
}

// Reuse your existing GetWdfModuleBounds logic but generic
NTSTATUS GetModuleBounds(const char* ModuleName, PVOID* Start, PVOID* End) {
    ULONG size = 0;
    ZwQuerySystemInformation(11, NULL, 0, &size); // 11 = SystemModuleInformation
    if (size == 0) return STATUS_UNSUCCESSFUL;

    PVOID buffer = ExAllocatePoolWithTag(NonPagedPool, size, 'scnm');
    if (!buffer) return STATUS_INSUFFICIENT_RESOURCES;

    NTSTATUS status = ZwQuerySystemInformation(11, buffer, size, &size);
    if (NT_SUCCESS(status)) {
        PSYSTEM_MODULE_INFORMATION mods = (PSYSTEM_MODULE_INFORMATION)buffer;
        *Start = NULL; *End = NULL;
        for (ULONG i = 0; i < mods->ulModuleCount; i++) {
            if (strstr(mods->Modules[i].ImageName, ModuleName)) {
                *Start = mods->Modules[i].Base;
                *End = (PUCHAR)mods->Modules[i].Base + mods->Modules[i].Size;
                break;
            }
        }
    }
    ExFreePoolWithTag(buffer, 'scnm');
    return (*Start && *End) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

// Helper 1: Verify the ENTIRE range is safe to read
BOOLEAN IsRangeReadable(PVOID Address, SIZE_T Size) {
    if (!Address) return FALSE;

    // Check start
    if (!MmIsAddressValid(Address)) return FALSE;

    // Check end (last byte)
    // If the read crosses a page boundary to an invalid page, this catches it.
    PUCHAR EndAddress = (PUCHAR)Address + Size - 1;
    if (!MmIsAddressValid(EndAddress)) return FALSE;

    return TRUE;
}

// Helper 2: Check module bounds
BOOLEAN IsAddressInModule(PVOID Address, PVOID ModBase, PVOID ModEnd) {
    return ((ULONG_PTR)Address >= (ULONG_PTR)ModBase && (ULONG_PTR)Address < (ULONG_PTR)ModEnd);
}

// Helper: Resolve RIP Relative Address (e.g., MOV RAX, [RIP+Offset])
PVOID ResolveRip(PUCHAR Instruction, ULONG OffsetOffset, ULONG InstrSize) {
    if (!Instruction || !MmIsAddressValid(Instruction)) return NULL;
    LONG RipOffset = *(PLONG)(Instruction + OffsetOffset);
    return (PVOID)(Instruction + InstrSize + RipOffset);
}

// -------------------------------------------------------------------------
// THE "STRICT UNIVERSAL" SCANNER
// Rejects weak tables (Score < 6) to avoid Data/Decoys.
// -------------------------------------------------------------------------
PVOID FindPdevAutomatically() {
    PVOID Base = NULL, End = NULL;
    if (!NT_SUCCESS(GetModuleBounds("win32kbase.sys", &Base, &End))) {
        DbgPrint("[DETECTOR] Could not find win32kbase.sys\n");
        return NULL;
    }

    DbgPrint("[DETECTOR] Strict Universal Scan Started. Base: %p\n", Base);

    PUCHAR ScanEnd = (PUCHAR)End - 8;

    for (PUCHAR ptr = (PUCHAR)Base; ptr < ScanEnd; ptr += 8) {
        if (!IsRangeReadable(ptr, sizeof(PVOID))) continue;

        PVOID CandidateGlobal = *(PVOID*)ptr;

        if ((ULONG_PTR)CandidateGlobal > 0xFFFF800000000000 &&
            !IsAddressInModule(CandidateGlobal, Base, End))
        {
            if (IsRangeReadable(CandidateGlobal, 0x3000)) {

                int BestScore = 0;
                int BestOffset = 0;

                // Scan starting at 0x800
                for (int offset = 0x800; offset < 0x2500; offset += 8) {

                    int currentScore = 0;
                    for (int k = 0; k < 8; k++) {
                        PVOID* slot = (PVOID*)((PUCHAR)CandidateGlobal + offset + (k * 8));
                        if (MmIsAddressValid(slot)) {
                            ULONG_PTR val = (ULONG_PTR)*slot;
                            // Check for Kernel Code (FFFFF8...)
                            if (val > 0xFFFFF80000000000) {
                                currentScore++;
                            }
                        }
                    }

                    if (currentScore > BestScore) {
                        BestScore = currentScore;
                        BestOffset = offset;
                    }
                }

                // [FIX] REQUIRE SCORE >= 6
                // This filters out the 4/8 decoy at 0x920
                if (BestScore >= 6) {
                    DbgPrint("[DETECTOR] !!! CHAMPION !!! Found Dense Table at 0x%X (Score: %d/8)\n", BestOffset, BestScore);
                    return CandidateGlobal;
                }
            }
        }
    }

    DbgPrint("[DETECTOR] Scan failed.\n");
    return NULL;
}

// -------------------------------------------------------------------------
// CR0 WRITE PROTECT BYPASS
// -------------------------------------------------------------------------
KIRQL WPOff()
{
    KIRQL irql = KeRaiseIrqlToDpcLevel();
    UINT64 cr0 = __readcr0();
    cr0 &= 0xfffffffffffeffff; // Clear WP bit
    __writecr0(cr0);
    _disable();
    return irql;
}

void WPOn(KIRQL irql)
{
    UINT64 cr0 = __readcr0();
    cr0 |= 0x10000; // Restore WP bit
    _enable();
    __writecr0(cr0);
    KeLowerIrql(irql);
}

// -------------------------------------------------------------------------
// [FINAL] HYBRID INSTALLER
// Hooks Index 0 (TextOut) Safely + Indices 2-6 (BitBlt)
// -------------------------------------------------------------------------
NTSTATUS InstallBitBltHook(PVOID InputAddress)
{
    INTERNAL_PDEV* pDev = (INTERNAL_PDEV*)InputAddress;
    if (!pDev || !MmIsAddressValid(pDev)) return STATUS_INVALID_PARAMETER;

    PVOID* pTableBase = NULL;
    int BestScore = 0;
    int BestOffset = 0;

    DbgPrint("[DETECTOR] Scanning for GDI Table...\n");

    // 1. Find the Table (Trusting Score 8)
    for (int offset = 0x800; offset < 0x3000; offset += 8) {
        int currentScore = 0;
        for (int k = 0; k < 10; k++) {
            PVOID* slot = (PVOID*)((PUCHAR)pDev + offset + (k * 8));
            if (MmIsAddressValid(slot)) {
                ULONG_PTR val = (ULONG_PTR)*slot;
                if (val > 0xFFFFF80000000000 ||
                    (PVOID)val == (PVOID)Hook_Index0 || // Check new hook
                    (PVOID)val == (PVOID)Hook_Index2 ||
                    (PVOID)val == (PVOID)Hook_Index3 ||
                    (PVOID)val == (PVOID)Hook_Index4 ||
                    (PVOID)val == (PVOID)Hook_Index5 ||
                    (PVOID)val == (PVOID)Hook_Index6)
                {
                    currentScore++;
                }
            }
        }
        if (currentScore >= 8) {
            if (currentScore > BestScore) {
                BestScore = currentScore;
                BestOffset = offset;
            }
        }
    }

    if (BestScore < 8) {
        DbgPrint("[DETECTOR] Failed to find Dense Table.\n");
        return STATUS_NOT_FOUND;
    }

    DbgPrint("[DETECTOR] Table found at 0x%X (Score: %d). Deploying Hybrid Hooks.\n", BestOffset, BestScore);
    pTableBase = (PVOID*)((PUCHAR)pDev + BestOffset);

    // --- HOOK INDEX 0 (TextOut - 10 Args) ---
    if (MmIsAddressValid(pTableBase + 0)) {
        PVOID Val = *(pTableBase + 0);
        if ((ULONG_PTR)Val > 0xFFFFF80000000000 && Val != Hook_Index0) {
            Original_Func0 = (TextOut_t)Val; // Cast to correct type
            InterlockedExchangePointer(pTableBase + 0, (PVOID)Hook_Index0);
            DbgPrint("    [+] Hooked Index 0 (TextOut) -> %p\n", Val);
        }
    }

    // --- HOOK INDEX 2 (BitBlt - 11 Args) ---
    if (MmIsAddressValid(pTableBase + 2)) {
        PVOID Val = *(pTableBase + 2);
        if ((ULONG_PTR)Val > 0xFFFFF80000000000 && Val != Hook_Index2) {
            Original_Func2 = (BitBlt_t)Val;
            InterlockedExchangePointer(pTableBase + 2, (PVOID)Hook_Index2);
            DbgPrint("    [+] Hooked Index 2 -> %p\n", Val);
        }
    }
    // ... Index 3 ...
    if (MmIsAddressValid(pTableBase + 3)) {
        PVOID Val = *(pTableBase + 3);
        if ((ULONG_PTR)Val > 0xFFFFF80000000000 && Val != Hook_Index3) {
            Original_Func3 = (BitBlt_t)Val;
            InterlockedExchangePointer(pTableBase + 3, (PVOID)Hook_Index3);
            DbgPrint("    [+] Hooked Index 3 -> %p\n", Val);
        }
    }
    // ... Index 4 ...
    if (MmIsAddressValid(pTableBase + 4)) {
        PVOID Val = *(pTableBase + 4);
        if ((ULONG_PTR)Val > 0xFFFFF80000000000 && Val != Hook_Index4) {
            Original_Func4 = (BitBlt_t)Val;
            InterlockedExchangePointer(pTableBase + 4, (PVOID)Hook_Index4);
            DbgPrint("    [+] Hooked Index 4 -> %p\n", Val);
        }
    }
    // ... Index 5 ...
    if (MmIsAddressValid(pTableBase + 5)) {
        PVOID Val = *(pTableBase + 5);
        if ((ULONG_PTR)Val > 0xFFFFF80000000000 && Val != Hook_Index5) {
            Original_Func5 = (BitBlt_t)Val;
            InterlockedExchangePointer(pTableBase + 5, (PVOID)Hook_Index5);
            DbgPrint("    [+] Hooked Index 5 -> %p\n", Val);
        }
    }
    // ... Index 6 ...
    if (MmIsAddressValid(pTableBase + 6)) {
        PVOID Val = *(pTableBase + 6);
        if ((ULONG_PTR)Val > 0xFFFFF80000000000 && Val != Hook_Index6) {
            Original_Func6 = (BitBlt_t)Val;
            InterlockedExchangePointer(pTableBase + 6, (PVOID)Hook_Index6);
            DbgPrint("    [+] Hooked Index 6 -> %p\n", Val);
        }
    }

    return STATUS_SUCCESS;
}

// 2. HELPER: Safe List Removal (Manual Unlink)
// Returns NULL if the list is empty or CORRUPTED (offsets wrong)
PLIST_ENTRY MyRemoveHeadList(PLIST_ENTRY ListHead) {
    if (!ListHead) return NULL;
    PLIST_ENTRY Entry = ListHead->Flink;

    // [FIX] Validate the pointer address before dereferencing it
    if (!MmIsAddressValid(Entry)) {
        KLOG("[V-FIX] Entry pointer is invalid memory: %p", Entry);
        return NULL;
    }

    if (Entry == ListHead) return NULL;

    // Now it is safe to check Blink
    if (Entry->Blink != ListHead || Entry->Flink->Blink != Entry) {
        KLOG("[V-FIX] CRITICAL: List Corruption Detected!");
        return NULL;
    }

    // 3. Manual Unlink (No standard RemoveEntryList macro)
    PLIST_ENTRY Next = Entry->Flink;
    ListHead->Flink = Next;
    Next->Blink = ListHead;

    return Entry;
}

// ------------------------------------------------------------------

// GLOBALS
ULONG g_XBox_SpinLock = 0x120;
ULONG g_XBox_Queue = 0x124;
PDEVICE_OBJECT g_ClassDevice = NULL;
PVOID g_ServiceCallback = NULL;

// Undocumented struct for the module list
typedef struct _KLDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    PVOID ExceptionTable;
    ULONG ExceptionTableSize;
    PVOID GpValue;
    PVOID NonPagedDebugInfo;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    // ... there is more, but we only need up to here
} KLDR_DATA_TABLE_ENTRY, * PKLDR_DATA_TABLE_ENTRY;

// Access to the global module list
extern "C" PLIST_ENTRY PsLoadedModuleList;

// 1. Get Base Address with DEBUG LOGGING
PVOID GetKernelModuleBase(const char* moduleName) {
    if (!moduleName) return NULL;

    // Safety check for list access
    if (!MmIsAddressValid(PsLoadedModuleList)) {
        KLOG("[V-XBOX] ERROR: PsLoadedModuleList is not valid!");
        return NULL;
    }

    PLIST_ENTRY listHead = PsLoadedModuleList;
    PLIST_ENTRY entry = listHead->Flink;

    // Loop through drivers
    int limit = 500; // Safety break
    while (entry != listHead && limit > 0) {
        PKLDR_DATA_TABLE_ENTRY data = CONTAINING_RECORD(entry, KLDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        if (data->BaseDllName.Buffer) {
            char currentName[256] = { 0 };
            WCHAR* src = data->BaseDllName.Buffer;
            int i = 0;
            while (src[i] && i < 255) {
                currentName[i] = (char)src[i];
                i++;
            }

            // LOG EVERYTHING so we can see what's loaded
            KLOG("[V-MOD] Scanned: %s", currentName); // Uncomment if you want to see ALL drivers

            if (_stricmp(currentName, moduleName) == 0) {
                KLOG("[V-XBOX] FOUND TARGET: %s at 0x%p", currentName, data->DllBase);
                return data->DllBase;
            }
        }
        entry = entry->Flink;
        limit--;
    }
    return NULL;
}

// 2. Get Export Address from a Driver Base (Manual EAT Parsing)
PVOID GetKernelExport(PVOID moduleBase, const char* routineName) {
    if (!moduleBase || !routineName) return NULL;

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)moduleBase;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return NULL;

    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((PUCHAR)moduleBase + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return NULL;

    IMAGE_DATA_DIRECTORY exportDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exportDir.VirtualAddress == 0) return NULL;

    PIMAGE_EXPORT_DIRECTORY exports = (PIMAGE_EXPORT_DIRECTORY)((PUCHAR)moduleBase + exportDir.VirtualAddress);

    PULONG functions = (PULONG)((PUCHAR)moduleBase + exports->AddressOfFunctions);
    PULONG names = (PULONG)((PUCHAR)moduleBase + exports->AddressOfNames);
    PUSHORT ordinals = (PUSHORT)((PUCHAR)moduleBase + exports->AddressOfNameOrdinals);

    for (ULONG i = 0; i < exports->NumberOfNames; i++) {
        char* name = (char*)((PUCHAR)moduleBase + names[i]);
        if (_stricmp(name, routineName) == 0) {
            ULONG functionRVA = functions[ordinals[i]];
            return (PVOID)((PUCHAR)moduleBase + functionRVA);
        }
    }
    return NULL;
}

// Need this to find the driver object
extern "C" NTSTATUS ObReferenceObjectByName(
    PUNICODE_STRING ObjectName,
    ULONG Attributes,
    PACCESS_STATE AccessState,
    ACCESS_MASK DesiredAccess,
    POBJECT_TYPE ObjectType,
    KPROCESSOR_MODE AccessMode,
    PVOID ParseContext,
    PVOID* Object
);
extern "C" POBJECT_TYPE* IoDriverObjectType;

// Offsets
#define KBDHID_OFFSET_CLASS_DEVICE  0x88
#define KBDHID_OFFSET_SERVICE_CB    0x90

// Helper Pointers
PPS_LOOKUP_PROCESS_BY_PROCESS_ID PsLookupProcessByProcessIdPtr = NULL;
PMM_GET_VIRTUAL_FOR_PHYSICAL MmGetVirtualForPhysicalPtr = NULL;
PDEVICE_OBJECT g_KbdDeviceObject = NULL;
ULONG g_DpcOffset = 0;
RTL_OSVERSIONINFOW Version;

// ==================================================================================
//                              CONTROLLER INJECTION LOGIC
// ==================================================================================

// Access to the global module list
extern "C" PLIST_ENTRY PsLoadedModuleList;
// [V-SCAN] Helper to fingerprint a WDF Queue
// Returns TRUE if the address looks exactly like a WDF Queue

// Ensure ZwQuerySystemInformation is visible
extern "C" NTSTATUS ZwQuerySystemInformation(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

// -------------------------------------------------------------------------
// HELPER: Find Wdf01000.sys bounds to identify the snitch
// -------------------------------------------------------------------------
NTSTATUS GetWdfModuleBounds(PVOID* Start, PVOID* End) {
    ULONG size = 0;
    // SystemModuleInformation is Class 11
    ZwQuerySystemInformation(11, NULL, 0, &size);

    if (size == 0) return STATUS_UNSUCCESSFUL;

    PVOID buffer = ExAllocatePoolWithTag(NonPagedPool, size, 'mods');
    if (!buffer) return STATUS_INSUFFICIENT_RESOURCES;

    NTSTATUS status = ZwQuerySystemInformation(11, buffer, size, &size);
    if (NT_SUCCESS(status)) {
        // V: Casting to YOUR existing struct from helpers.h
        PSYSTEM_MODULE_INFORMATION mods = (PSYSTEM_MODULE_INFORMATION)buffer;
        *Start = NULL; *End = NULL;

        // V: Using 'ulModuleCount' because that is what your header uses
        for (ULONG i = 0; i < mods->ulModuleCount; i++) {

            // V: Accessing 'ImageName' directly from your SYSTEM_MODULE struct
            // Your struct has: CHAR ImageName[256];
            PCHAR name = mods->Modules[i].ImageName;

            // Check for the Framework driver (case insensitive-ish)
            if (strstr(name, "Wdf01000") || strstr(name, "wdf01000") || strstr(name, "WDF01000")) {

                // V: Using 'Base' and 'Size' from your struct
                *Start = mods->Modules[i].Base;
                *End = (PUCHAR)mods->Modules[i].Base + mods->Modules[i].Size;

                // KLOG("[*] WDF Framework found at: %p - %p", *Start, *End);
                break;
            }
        }
    }

    ExFreePoolWithTag(buffer, 'mods');
    return (*Start && *End) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

// -------------------------------------------------------------------------
// HELPER: The Shredder - Removes WDF Completion Routines
// -------------------------------------------------------------------------
void RemoveWdfCompletionRoutine(PIRP Irp) {
    static PVOID WdfStart = NULL;
    static PVOID WdfEnd = NULL;

    // Cache the module bounds so we aren't scanning every time
    if (!WdfStart) {
        GetWdfModuleBounds(&WdfStart, &WdfEnd);
    }

    if (!WdfStart || !WdfEnd) return;

    PIO_STACK_LOCATION currentStack = IoGetCurrentIrpStackLocation(Irp);

    // Walk the stack from current location up to the top
    for (CCHAR i = Irp->CurrentLocation; i <= Irp->StackCount; i++) {
        PIO_STACK_LOCATION stack = currentStack + (i - Irp->CurrentLocation);

        if (stack->CompletionRoutine) {
            // If the routine belongs to WDF, nuke it.
            if ((PVOID)stack->CompletionRoutine >= WdfStart && (PVOID)stack->CompletionRoutine < WdfEnd) {
                // KLOG("[V-FIX] SHREDDING WDF CompletionRoutine at Stack Index %d", i);

                stack->CompletionRoutine = NULL;
                stack->Context = NULL;
                stack->Control = 0; // Clear SL_INVOKE_ON_SUCCESS flags
            }
        }
    }
}

// =========================================================================================
// [V-FIX] IRON-CLAD IRP VALIDATOR
// Rejects any pointer that isn't a 100% valid IRP with a buffer.
// =========================================================================================
BOOLEAN IsValidIrp(PVOID Ptr) {
    if (!MmIsAddressValid(Ptr)) return FALSE;
    if ((ULONG64)Ptr < 0xFFFF000000000000) return FALSE;
    if ((ULONG64)Ptr & 0xF) return FALSE; // Must be 16-byte aligned

    // 1. Check Header (Type 6, Size reasonable)
    CSHORT type = *(CSHORT*)Ptr;
    USHORT size = *((USHORT*)Ptr + 1);
    if (type != 0x06) return FALSE;
    if (size < 0x40 || size > 0x500) return FALSE;

    PIRP pIrp = (PIRP)Ptr;

    // 2. BUFFER CHECK (The Filter)
    // If it doesn't have a place to put data, it's not our IRP (or it's garbage).
    // Valid IRPs must have EITHER a SystemBuffer OR an MdlAddress.
    if (pIrp->AssociatedIrp.SystemBuffer == NULL && pIrp->MdlAddress == NULL) {
        return FALSE;
    }

    // If it has a buffer, the buffer pointer must be valid kernel memory
    if (pIrp->AssociatedIrp.SystemBuffer && !MmIsAddressValid(pIrp->AssociatedIrp.SystemBuffer)) return FALSE;
    if (pIrp->MdlAddress && !MmIsAddressValid(pIrp->MdlAddress)) return FALSE;

    // 3. Thread Check
    // A pending Read IRP usually has a thread.
    if (pIrp->Tail.Overlay.Thread != NULL && !MmIsAddressValid(pIrp->Tail.Overlay.Thread)) return FALSE;

    return TRUE;
}

PIRP FindIrpFromWdfRequest(PVOID wdfReq) {
    if (!wdfReq || !MmIsAddressValid(wdfReq)) return NULL;

    // Scan 0x00 to 0x60 for the IRP pointer
    for (int offset = 0x0; offset <= 0x60; offset += sizeof(PVOID)) {
        PVOID* ptrToCandidate = (PVOID*)((PUCHAR)wdfReq + offset);

        if (MmIsAddressValid(ptrToCandidate)) {
            PVOID candidateIrp = *ptrToCandidate;

            // Use the SURGICAL validator
            if (IsValidIrp(candidateIrp)) {
                PIRP pIrp = (PIRP)candidateIrp;
                PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(pIrp);

                if (MmIsAddressValid(stack)) {
                    // Accept READ or INTERNAL_DEVICE_CONTROL (Common for HID)
                    if (stack->MajorFunction == IRP_MJ_READ ||
                        stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL ||
                        stack->MajorFunction == IRP_MJ_DEVICE_CONTROL) {
                        return pIrp;
                    }
                }
            }
        }
    }
    return NULL;
}

// =========================================================================================
// [V-FIX] "Deep Search" Scanner
// Scans ALL lists and prioritizes the one that actually has IRPs (Non-Empty).
// =========================================================================================
PLIST_ENTRY FindWdfQueueList(PDEVICE_OBJECT DeviceObject) {
    if (!DeviceObject || !DeviceObject->DeviceExtension) return NULL;

    ULONG_PTR* extension = (ULONG_PTR*)DeviceObject->DeviceExtension;
    PLIST_ENTRY bestCandidate = NULL;

    // Scan the Device Extension (up to 2KB deep)
    for (int i = 0; i < 512; i++) {
        PVOID candidateObj = (PVOID)extension[i];

        if (MmIsAddressValid(candidateObj) && (ULONG64)candidateObj > 0xFFFF000000000000) {

            // Scan inside the object for a ListHead
            for (int offset = 0; offset < 0x200; offset += 8) {
                PLIST_ENTRY pList = (PLIST_ENTRY)((PUCHAR)candidateObj + offset);

                if (MmIsAddressValid(pList) && MmIsAddressValid(pList->Flink) && MmIsAddressValid(pList->Blink)) {

                    // Integrity Check: Flink->Blink must point to us
                    if (pList->Flink->Blink == pList && pList->Blink->Flink == pList) {

                        // We found a valid list.
                        // Now, check if it's empty.
                        if (pList->Flink != pList) {
                            // JACKPOT: This list has items! 
                            // It is almost certainly the active I/O queue.
                            KLOG("[V-SCAN] >>> FOUND ACTIVE QUEUE! Obj: %p Offset: 0x%X", candidateObj, offset);
                            return pList;
                        }
                        else {
                            // It's a valid list, but it's empty. 
                            // Save it as a backup in case we don't find any active ones.
                            if (!bestCandidate) {
                                // Only save the first empty one we find as a fallback
                                bestCandidate = pList;
                                KLOG("[V-SCAN] Found Empty Candidate. Keeping searching...");
                            }
                        }
                    }
                }
            }
        }
    }

    if (bestCandidate) {
       KLOG("[V-SCAN] No active queue found. Returning empty candidate.");
    }

    return bestCandidate;
}

// =========================================================================================
// [V-FIX] SNIPER IRP CHECKER
// Checks if 'Entry' is embedded inside an IRP at a specific offset.
// Safe memory checks prevent 0x1E BSOD.
// =========================================================================================
PIRP CheckIrpAtOffset(PLIST_ENTRY Entry, int NegativeOffset) {
    // Calculate where the IRP Start would be
    PUCHAR pIrpStart = (PUCHAR)Entry - NegativeOffset;

    // 1. IS THE MEMORY VALID? (Prevents 0x1E)
    // We check the first 8 bytes (Header) and the Thread Pointer location (Offset 0x40)
    if (!MmIsAddressValid(pIrpStart) || !MmIsAddressValid(pIrpStart + 0x40)) {
        return NULL;
    }

    // 2. IS IT ALIGNED? (IRPs are always 16-byte aligned, usually 0x10 aligned)
    if ((ULONG64)pIrpStart & 0xF) return NULL;

    // 3. IS IT AN IRP? (Type 0x06)
    CSHORT type = *(CSHORT*)pIrpStart;
    if (type != 0x06) return NULL;

    // 4. CHECK THREAD POINTER (Heuristic)
    // Active Read IRPs usually have a thread.
    PVOID threadPtr = *(PVOID*)(pIrpStart + 0x40);
    if (threadPtr && !MmIsAddressValid(threadPtr)) return NULL;

    // 5. CHECK STACK LOCATION
    PIRP pIrp = (PIRP)pIrpStart;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(pIrp);

    if (MmIsAddressValid(stack)) {
        if (stack->MajorFunction == IRP_MJ_READ ||
            stack->MajorFunction == IRP_MJ_DEVICE_CONTROL ||
            stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) {
            return pIrp;
        }
    }

    return NULL;
}

// =========================================================================================
// [V-FIX] CIRCLE CHECKER (Type-Cast Fixed)
// Verifies if a candidate IRP owns the specific ListEntry we are looking at.
// =========================================================================================
BOOLEAN IsIrpOwningEntry(PIRP Irp, PLIST_ENTRY TargetEntry) {
    if (!MmIsAddressValid(Irp)) return FALSE;

    // Check 1: Tail.Overlay.ListEntry (Standard WDM)
    // This is already a LIST_ENTRY, so no cast needed usually, but being safe doesn't hurt.
    if (&Irp->Tail.Overlay.ListEntry == TargetEntry) return TRUE;

    // Check 2: Tail.Overlay.DeviceQueueEntry (Standard StartIo)
    // [FIX] Cast PKDEVICE_QUEUE_ENTRY to PLIST_ENTRY to fix compilation error
    if ((PLIST_ENTRY)&Irp->Tail.Overlay.DeviceQueueEntry == TargetEntry) return TRUE;

    // Check 3: DriverContext (Common Custom List)
    // Cast the void* array slot to PLIST_ENTRY
    if ((PLIST_ENTRY)&Irp->Tail.Overlay.DriverContext[0] == TargetEntry) return TRUE;

    return FALSE;
}

// =========================================================================================
// [V-FIX] PAGE SAFETY CALCULATOR
// Returns the safe scan limits for a given address to avoid 0x1E BSOD.
// =========================================================================================
void GetSafeScanLimits(PUCHAR Address, ULONG ScanRange, PUCHAR* OutStart, PUCHAR* OutEnd) {
    ULONG_PTR addr = (ULONG_PTR)Address;
    ULONG_PTR pageSize = PAGE_SIZE;
    ULONG_PTR pageStart = addr & ~(pageSize - 1);
    ULONG_PTR pageEnd = pageStart + pageSize;
    ULONG_PTR desiredStart = addr - ScanRange;
    ULONG_PTR desiredEnd = addr + ScanRange;
    if (desiredStart < pageStart) desiredStart = pageStart;
    if (desiredEnd > pageEnd) desiredEnd = pageEnd;
    *OutStart = (PUCHAR)desiredStart;
    *OutEnd = (PUCHAR)desiredEnd;
}

// =========================================================================================
// [V-FIX] MEMORY SAFETY CHECK
// Verifies that the ENTIRE range [Address, Address+Size] is valid.
// =========================================================================================
BOOLEAN IsSafeToRead(PVOID Address, ULONG Size) {
    PUCHAR start = (PUCHAR)Address;
    PUCHAR end = start + Size;

    // Check Start and End. 
    // MmIsAddressValid checks if the page is mapped.
    if (!MmIsAddressValid(start)) return FALSE;
    if (!MmIsAddressValid(end - 1)) return FALSE; // Check last byte

    // If crossing a page boundary, check the middle too.
    if (((ULONG_PTR)start & ~(PAGE_SIZE - 1)) != ((ULONG_PTR)(end - 1) & ~(PAGE_SIZE - 1))) {
        // We crossed a page. Be paranoid and fail.
        // It is rare for an IRP to straddle pages in a way that risks a crash if start/end are valid,
        // but rejecting straddlers is the safest option.
        return FALSE;
    }

    return TRUE;
}

// =========================================================================================
// [V-FIX] WDM OMNI-SCANNER
// Scans BACKWARDS from a ListEntry to find the IRP Header (Type 0x06).
// Handles custom list offsets in DriverContext.
// =========================================================================================
PIRP FindIrpBackedByListEntry(PLIST_ENTRY Entry) {
    if (!MmIsAddressValid(Entry)) return NULL;

    PUCHAR scanStart, scanEnd;

    // Scan backwards up to 0x150 bytes (covers standard + DriverContext)
    // "ScanEnd" here is actually the Entry address (we scan up to it)
    // "ScanStart" is the lowest memory address
    GetSafeScanLimits((PUCHAR)Entry, 0x150, &scanStart, &scanEnd);

    // Scan every 8 bytes looking for IRP Header
    // We iterate from the Entry BACKWARDS to the limit
    for (PUCHAR ptr = (PUCHAR)Entry; ptr >= scanStart; ptr -= 8) {

        if (MmIsAddressValid(ptr)) {
            // Check for IRP Signature
            // Type (offset 0) == 0x06
            // Size (offset 2) >= 0x40
            CSHORT type = *(CSHORT*)ptr;
            USHORT size = *((USHORT*)ptr + 1);

            if (type == 0x06 && size >= 0x40 && size < 0x800) {
                // Potential IRP found at 'ptr'
                PIRP pCandidate = (PIRP)ptr;

                // Validate Stack
                // If this is truly the IRP owning this list entry, it should be valid.
                PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(pCandidate);
                if (MmIsAddressValid(stack)) {
                    // hidusb uses INTERNAL_DEVICE_CONTROL for USB submissions
                    if (stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL ||
                        stack->MajorFunction == IRP_MJ_READ ||
                        stack->MajorFunction == IRP_MJ_DEVICE_CONTROL) {

                        // Thread check (Optional but good)
                        PVOID thread = pCandidate->Tail.Overlay.Thread;
                        if (thread == NULL || MmIsAddressValid(thread)) {
                            return pCandidate;
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

// =========================================================================================
// [V-FIX] OMNI-SCANNER (Page Safe)
// Scans +/- 512 bytes around the packet, but stops at page borders.
// =========================================================================================
PIRP FindIrpInPacket_Omni(PLIST_ENTRY Packet) {
    if (!Packet || !MmIsAddressValid(Packet)) return NULL;

    PUCHAR scanStart, scanEnd;

    // Scan huge range (512 bytes) safely
    GetSafeScanLimits((PUCHAR)Packet, 512, &scanStart, &scanEnd);

    // Scan every 8 bytes
    for (PUCHAR ptr = scanStart; ptr < scanEnd; ptr += 8) {

        // Paranoid Read Check
        if (MmIsAddressValid(ptr)) {
            PVOID val = *(PVOID*)ptr;

            // Is it a kernel pointer?
            if (val && (ULONG64)val > 0xFFFF000000000000) {

                // CHECK 1: Is it a WDFREQUEST Handle? -> IRP
                PIRP irp = FindIrpFromWdfRequest(val);
                if (irp && !irp->Cancel) return irp;

                // CHECK 2: Is it a Raw IRP? (WDM Style fallback)
                // Use strict validation on the pointer itself
                if (IsValidIrp(val)) {
                    PIRP pRawIrp = (PIRP)val;
                    if (!pRawIrp->Cancel) {
                        PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(pRawIrp);
                        if (MmIsAddressValid(stack)) {
                            if (stack->MajorFunction == IRP_MJ_READ ||
                                stack->MajorFunction == IRP_MJ_DEVICE_CONTROL ||
                                stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) {
                                return pRawIrp;
                            }
                        }
                    }
                }
            }
        }
    }
    return NULL;
}


// =========================================================================================
// [V-FIX] PARANOID PACKET SCANNER
// Scans forward (0 to 0x40), checking every address before reading.
// =========================================================================================
PIRP FindIrpInPacket(PLIST_ENTRY Packet) {
    if (!Packet || !MmIsAddressValid(Packet)) return NULL;

    // Scan forward 0x00 to 0x40
    PUCHAR start = (PUCHAR)Packet;
    PUCHAR end = start + 0x400;

    for (PUCHAR ptr = start; ptr < end; ptr += 8) {
        // 1. Is the address of the potential pointer valid?
        if (MmIsAddressValid(ptr)) {
            PVOID candidateHandle = *(PVOID*)ptr;

            // 2. Is the pointer value itself a kernel address?
            if (candidateHandle && (ULONG64)candidateHandle > 0xFFFF000000000000) {

                // 3. Try to resolve IRP (using the paranoid finder)
                // (Ensure you are using the PARANOID version of FindIrpFromWdfRequest from my previous message)
                PIRP irp = FindIrpFromWdfRequest(candidateHandle);
                if (irp) return irp;
            }
        }
    }
    return NULL;
}

// =========================================================================================
// [V-FIX] PAGE CLAMP
// Returns the start/end of the valid 4KB page for a given address.
// =========================================================================================
void GetPageLimits(PUCHAR Address, PUCHAR* PageStart, PUCHAR* PageEnd) {
    ULONG_PTR addr = (ULONG_PTR)Address;
    // Round down to nearest 4096 bytes (Page Start)
    *PageStart = (PUCHAR)(addr & ~(PAGE_SIZE - 1));
    // Page End is Start + 4096
    *PageEnd = *PageStart + PAGE_SIZE;
}


// =========================================================================================
// [V-FIX] BRUTE FORCE DECAPSULATOR
// Scans 1KB forward to find the IRP pointer inside the WDFREQUEST.
// =========================================================================================
PIRP FindIrpBruteForce(PLIST_ENTRY Entry) {
    if (!MmIsAddressValid(Entry)) return NULL;

    PUCHAR pageStart, pageEnd;
    GetPageLimits((PUCHAR)Entry, &pageStart, &pageEnd);

    // Scan forward from Entry to Entry + 0x400 (Deep Scan)
    // But CLAMP to page end to prevent 0x1E BSOD
    PUCHAR scanStart = (PUCHAR)Entry;
    PUCHAR scanEnd = (PUCHAR)Entry + 0x400;

    // Stop 8 bytes before page end so we don't straddle the boundary
    if (scanEnd > (pageEnd - 8)) scanEnd = pageEnd - 8;

    for (PUCHAR ptr = scanStart; ptr < scanEnd; ptr += 8) {
        if (MmIsAddressValid(ptr)) {
            PVOID val = *(PVOID*)ptr;
            if (val && (ULONG64)val > 0xFFFF000000000000) {
                if (IsValidIrp(val)) {
                    PIRP pCandidate = (PIRP)val;
                    if (!pCandidate->Cancel) {
                        PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(pCandidate);
                        if (MmIsAddressValid(stack)) {
                            // Accept READ or CONTROL IRPs
                            if (stack->MajorFunction == IRP_MJ_READ ||
                                stack->MajorFunction == IRP_MJ_DEVICE_CONTROL ||
                                stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) {
                                return pCandidate;
                            }
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

// =========================================================================================
// [V-FIX] IRP VALIDATOR (Extension Sweeper)
// Checks if a pointer found in the extension is a valid, pending HID IRP.
// =========================================================================================
BOOLEAN IsValidPendingIrp(PVOID Ptr) {
    if (!MmIsAddressValid(Ptr)) return FALSE;
    if ((ULONG64)Ptr < 0xFFFF000000000000) return FALSE;
    if ((ULONG64)Ptr & 0xF) return FALSE; // Must be 16-byte aligned

    // 1. Check Header
    if (*(CSHORT*)Ptr != 0x06) return FALSE; // Type must be IRP (6)

    PIRP pIrp = (PIRP)Ptr;

    // 2. Check Allocation Size (Relaxed)
    if (pIrp->Size < 0x30 || pIrp->Size > 0x800) return FALSE;

    // 3. Check Stack State
    // A pending IRP usually has a valid CurrentLocation
    if (pIrp->CurrentLocation <= 0 || pIrp->CurrentLocation > pIrp->StackCount + 1) return FALSE;

    // 4. Check Major Function
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(pIrp);
    if (!MmIsAddressValid(stack)) return FALSE;

    // We want READ (Input) or INTERNAL_DEVICE_CONTROL (USB URB)
    if (stack->MajorFunction == IRP_MJ_READ ||
        stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) {
        return TRUE;
    }

    return FALSE;
}


// =========================================================================================
// [V-FIX] DEVICE QUEUE EXTRACTOR
// Extracts an IRP from a KDEVICE_QUEUE_ENTRY (Standard StartIo Queue)
// =========================================================================================
PIRP ExtractIrpFromDeviceQueueEntry(PKDEVICE_QUEUE_ENTRY Entry) {
    if (!Entry || !MmIsAddressValid(Entry)) return NULL;

    // Scan backwards from Entry to find the IRP Header.
    // The entry is usually at Tail.Overlay.DeviceQueueEntry (Offset 0x78 on x64)
    // We verify this by finding the 0x06 signature.

    PUCHAR scanStart = (PUCHAR)Entry - 0x100;

    // Clamp to page start to prevent 0x1E
    ULONG_PTR pageStart = (ULONG_PTR)Entry & ~(PAGE_SIZE - 1);
    if ((ULONG64)scanStart < pageStart) scanStart = (PUCHAR)pageStart;

    for (PUCHAR ptr = (PUCHAR)Entry; ptr >= scanStart; ptr -= 8) {
        if (MmIsAddressValid(ptr)) {
            if (*(CSHORT*)ptr == 0x06) { // IRP Signature
                PIRP pCandidate = (PIRP)ptr;

                // Verify this IRP actually owns this Queue Entry
                if ((PVOID)&pCandidate->Tail.Overlay.DeviceQueueEntry == (PVOID)Entry) {
                    return pCandidate;
                }
            }
        }
    }
    return NULL;
}

// =========================================================================================
// [V-FIX] DISCRETE PROBE (No Loops, No Scanning)
// Checks only specific offsets to find the IRP.
// =========================================================================================
PIRP ProbeIrpAtOffset(PLIST_ENTRY Entry, int Offset) {
    PUCHAR addr = (PUCHAR)Entry + Offset;

    // 1. PAGE SAFETY CHECK
    // Ensure the 8 bytes we want to read are fully inside the same page as Entry.
    // (Optimization: We assume Entry is valid, so we just check if addr crosses page boundary)
    ULONG_PTR entryPage = (ULONG_PTR)Entry & ~(PAGE_SIZE - 1);
    ULONG_PTR targetPage = (ULONG_PTR)addr & ~(PAGE_SIZE - 1);
    ULONG_PTR targetEndPage = ((ULONG_PTR)addr + 7) & ~(PAGE_SIZE - 1);

    if (targetPage != entryPage || targetEndPage != entryPage) {
        return NULL; // Unsafe to read
    }

    // 2. READ POINTER
    PVOID candidate = *(PVOID*)addr;

    // 3. VALIDATE
    // Use the Iron-Clad Validator logic inline for speed
    if (candidate && (ULONG64)candidate > 0xFFFF000000000000 && ((ULONG64)candidate & 0xF) == 0) {
        if (*(CSHORT*)candidate == 0x06) { // Type check
            PIRP pIrp = (PIRP)candidate;

            // Check Buffer Presence (Stop 0xC2)
            if (pIrp->AssociatedIrp.SystemBuffer || pIrp->MdlAddress) {
                if (!pIrp->Cancel) {
                    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(pIrp);
                    if (MmIsAddressValid(stack)) {
                        if (stack->MajorFunction == IRP_MJ_READ ||
                            stack->MajorFunction == IRP_MJ_DEVICE_CONTROL ||
                            stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) {
                            return pIrp;
                        }
                    }
                }
            }
        }
    }
    return NULL;
}
// =========================================================================================
// [V-FIX] TOUCH-FREE VALIDATOR
// Checks ONLY the header. Does NOT chase pointers. 100% Crash Proof.
// =========================================================================================
BOOLEAN IsValidIrpHeaderOnly(PVOID Ptr) {
    // 1. Basic Validity
    if (!Ptr || (ULONG64)Ptr < 0xFFFF000000000000) return FALSE;
    if ((ULONG64)Ptr & 0xF) return FALSE; // Alignment

    // 2. Safe Read Check (Header Only)
    // We only need to read the first 4 bytes safely.
    if (!MmIsAddressValid(Ptr)) return FALSE;

    // 3. Type Check (0x06)
    CSHORT type = *(CSHORT*)Ptr;
    if (type != 0x06) return FALSE;

    // 4. Size Check
    USHORT size = *((USHORT*)Ptr + 1);
    if (size < 0x40 || size > 0x800) return FALSE;

    return TRUE;
}

// =========================================================================================
// [V-FIX] BULLETPROOF IRP VALIDATOR
// Prevents 0x1E by verifying memory range before reading fields.
// =========================================================================================
BOOLEAN IsValidIrp_Safe(PVOID Ptr) {
    // 1. Alignment & Basic Range
    if ((ULONG64)Ptr < 0xFFFF000000000000) return FALSE;
    if ((ULONG64)Ptr & 0xF) return FALSE; // IRPs are 16-byte aligned

    // 2. CRITICAL: Check if the Header is safe to read (First 0x10 bytes)
    if (!IsSafeToRead(Ptr, 0x10)) return FALSE;

    // 3. Check Header (Type 0x06)
    if (*(CSHORT*)Ptr != 0x06) return FALSE;

    // 4. Check Size (Offset 0x02)
    USHORT size = *((USHORT*)Ptr + 1);
    if (size < 0x40 || size > 0x800) return FALSE;

    // 5. CRITICAL: Check if the whole IRP body is safe to read
    // We rely on the size from the header.
    if (!IsSafeToRead(Ptr, size)) return FALSE;

    PIRP pIrp = (PIRP)Ptr;

    // 6. Buffer Check (Prevent 0xC2)
    // We can safely read these now because we validated the range above.
    if (pIrp->AssociatedIrp.SystemBuffer == NULL && pIrp->MdlAddress == NULL) {
        return FALSE;
    }

    // If has SystemBuffer, is the Buffer pointer valid?
    if (pIrp->AssociatedIrp.SystemBuffer && !MmIsAddressValid(pIrp->AssociatedIrp.SystemBuffer)) return FALSE;

    // If has MDL, is the MDL pointer valid?
    if (pIrp->MdlAddress && !MmIsAddressValid(pIrp->MdlAddress)) return FALSE;

    return TRUE;
}

// =========================================================================================
// [V-FIX] PAGE-LOCKED SAFETY CHECK
// Returns TRUE only if [Address, Address+Size] falls strictly inside [PageStart, PageEnd].
// =========================================================================================
BOOLEAN IsRangeSafeLocked(PVOID Address, SIZE_T Size, ULONG_PTR PageStart, ULONG_PTR PageEnd) {
    ULONG_PTR start = (ULONG_PTR)Address;
    ULONG_PTR end = start + Size;

    // Prevent integer overflow
    if (end < start) return FALSE;

    // Check bounds
    if (start < PageStart || end > PageEnd) return FALSE;

    return TRUE;
}

// =========================================================================================
// [V-FIX] ULTRA-SAFE VALIDATOR
// Checks header only. Does NOT read SystemBuffer/MdlAddress to avoid 0x1E.
// =========================================================================================
BOOLEAN IsValidIrp_UltraSafe(PVOID Ptr) {
    // 1. Alignment & Location
    if ((ULONG64)Ptr < 0xFFFF000000000000) return FALSE;
    if ((ULONG64)Ptr & 0x7) return FALSE; // 8-byte aligned minimum

    // 2. Safe Read (Header) - Only at PASSIVE_LEVEL
    if (KeGetCurrentIrql() > APC_LEVEL) return FALSE;

    if (!MmIsAddressValid(Ptr)) return FALSE;

    __try {
        // 3. Basic validation - less strict
        CSHORT type = *(CSHORT*)Ptr;
        // Common IRP types: IoSb I/O Status Block = 0x06, others may vary
        if (type != 0x06 && type != 0x08 && type != 0x18) {
            return FALSE;
        }

        // 4. Size Sanity
        USHORT size = *((USHORT*)Ptr + 1);
        if (size < 0x40 || size > 0x1000) {
            return FALSE;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }

    return TRUE;
}

// =========================================================================================
// [V-FIX] Enhanced Queue Validation
// =========================================================================================
BOOLEAN IsLikelyWdfQueue(PVOID Ptr) {
    if (KeGetCurrentIrql() > APC_LEVEL) return FALSE;
    if (!MmIsAddressValid(Ptr)) return FALSE;

    __try {
        PLIST_ENTRY list = (PLIST_ENTRY)Ptr;

        // Basic list validation
        if (!MmIsAddressValid(list->Flink) || !MmIsAddressValid(list->Blink)) {
            return FALSE;
        }

        // Circular list check
        if (list->Flink->Blink != list || list->Blink->Flink != list) {
            return FALSE;
        }

        return TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}


// =========================================================================================
// [V-FIX] SAFE PAGE SCANNER
// Scans forward from StartAddr looking for an IRP pointer.
// STOPS exactly at the 4KB page boundary to prevent 0x1E BSOD.
// =========================================================================================
PIRP ScanPageForIrp(PUCHAR StartAddr) {
    if (!MmIsAddressValid(StartAddr)) return NULL;

    ULONG_PTR current = (ULONG_PTR)StartAddr;

    // 1. Calculate the Hard Limit (End of the current 4KB page)
    // Page Size is 4096 (0x1000). Mask off the lower bits to get start, add 0x1000 to get end.
    ULONG_PTR pageEnd = (current & ~(PAGE_SIZE - 1)) + PAGE_SIZE;

    // 2. Define a reasonable scan limit (e.g., 512 bytes forward)
    // But CLAMP it to the pageEnd.
    ULONG_PTR scanLimit = current + 0x200;
    if (scanLimit > pageEnd) scanLimit = pageEnd;

    // 3. Iterate 8-byte aligned
    // CRITICAL: Ensure (current + 8) does not exceed the limit before reading.
    while (current + 8 <= scanLimit) {

        PVOID* ptrToVal = (PVOID*)current;

        // Paranoid validity check
        if (MmIsAddressValid(ptrToVal)) {
            PVOID val = *ptrToVal;

            // Fast Filter: Is it a kernel pointer?
            if (val && (ULONG64)val > 0xFFFF000000000000 && ((ULONG64)val & 0xF) == 0) {

                // Use the Iron-Clad Validator (Must have Buffer/MDL)
                if (IsValidIrp(val)) {
                    PIRP pCandidate = (PIRP)val;
                    if (!pCandidate->Cancel) {
                        PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(pCandidate);
                        if (MmIsAddressValid(stack)) {
                            // Accept READ or CONTROL IRPs
                            if (stack->MajorFunction == IRP_MJ_READ ||
                                stack->MajorFunction == IRP_MJ_DEVICE_CONTROL ||
                                stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) {
                                return pCandidate;
                            }
                        }
                    }
                }
            }
        }

        current += 8;
    }

    return NULL;
}


// =========================================================================================
// [V-FIX] PAGE SAFE SCANNER (Prevents Page Straddle Crash)
// =========================================================================================
PIRP FindIrpInRequestSafe(PLIST_ENTRY Entry) {
    if (!MmIsAddressValid(Entry)) return NULL;

    ULONG_PTR addr = (ULONG_PTR)Entry;
    // Calculate Page Limits
    ULONG_PTR pageStart = addr & ~(PAGE_SIZE - 1);
    ULONG_PTR pageEnd = pageStart + PAGE_SIZE;

    // Define Scan Range (Entry +/- 0x80)
    ULONG_PTR scanStart = addr - 0x80;
    ULONG_PTR scanEnd = addr + 0x80;

    // CLAMP TO PAGE
    if (scanStart < pageStart) scanStart = pageStart;
    if (scanEnd > pageEnd)     scanEnd = pageEnd;

    // [CRITICAL FIX] Ensure we don't read past the end of the page
    // We need 8 bytes for a pointer. So stop 8 bytes before the end.
    if (scanEnd > (pageEnd - 8)) scanEnd = pageEnd - 8;

    // Safe Loop
    for (ULONG_PTR ptr = scanStart; ptr <= scanEnd; ptr += 8) {

        // Paranoid: Re-verify address is valid before read
        if (MmIsAddressValid((PVOID)ptr)) {

            PVOID val = *(PVOID*)ptr;

            // Is it a kernel pointer?
            if (val && (ULONG64)val > 0xFFFF000000000000) {
                // Is it an IRP?
                if (IsValidIrp(val)) {
                    PIRP pCandidate = (PIRP)val;
                    if (!pCandidate->Cancel) {
                        // Check Stack
                        PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(pCandidate);
                        if (MmIsAddressValid(stack)) {
                            if (stack->MajorFunction == IRP_MJ_READ ||
                                stack->MajorFunction == IRP_MJ_DEVICE_CONTROL ||
                                stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) {
                                return pCandidate;
                            }
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

// =========================================================================================
// WORK ITEM STRUCTURE FOR DEFERRED PROCESSING
// =========================================================================================
typedef struct _INJECT_WORK_ITEM {
    WORK_QUEUE_ITEM WorkItem;
    PIRP TargetIrp;
    XBOX_INPUT_REPORT Report;
} INJECT_WORK_ITEM, * PINJECT_WORK_ITEM;

// =========================================================================================
// WORKER ROUTINE FOR BUFFER INJECTION AT PASSIVE_LEVEL
// =========================================================================================
VOID InjectWorkRoutine(PVOID Parameter) {
    PINJECT_WORK_ITEM workItem = (PINJECT_WORK_ITEM)Parameter;

    __try {
        PIRP targetIrp = workItem->TargetIrp;
        PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(targetIrp);
        PVOID buffer = NULL;
        ULONG len = 0;

        // Safe to read buffer at PASSIVE_LEVEL
        if (targetIrp->MdlAddress) {
            buffer = MmGetSystemAddressForMdlSafe(targetIrp->MdlAddress, NormalPagePriority);
        }
        else {
            buffer = targetIrp->AssociatedIrp.SystemBuffer;
        }

        if (stack->MajorFunction == IRP_MJ_READ) {
            len = stack->Parameters.Read.Length;
        }
        else {
            len = stack->Parameters.DeviceIoControl.OutputBufferLength;
        }

        if (buffer && len >= sizeof(XBOX_INPUT_REPORT)) {
            RtlZeroMemory(buffer, len);
            RtlCopyMemory(buffer, &workItem->Report, sizeof(XBOX_INPUT_REPORT));

            targetIrp->IoStatus.Information = sizeof(XBOX_INPUT_REPORT);
            targetIrp->IoStatus.Status = STATUS_SUCCESS;

            // Clear completion routine
            if (stack->CompletionRoutine) {
                stack->CompletionRoutine = NULL;
                stack->Control = 0;
            }

            IoCompleteRequest(targetIrp, IO_NO_INCREMENT);
        }
        else {
            targetIrp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
            targetIrp->IoStatus.Information = 0;
            IoCompleteRequest(targetIrp, IO_NO_INCREMENT);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Handle any exceptions gracefully
        if (workItem->TargetIrp) {
            workItem->TargetIrp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            workItem->TargetIrp->IoStatus.Information = 0;
            IoCompleteRequest(workItem->TargetIrp, IO_NO_INCREMENT);
        }
    }

    // Clean up work item
    ExFreePoolWithTag(workItem, 'kniX');
}

// =========================================================================================
// SAFE IRP VALIDATION AT DISPATCH_LEVEL
// =========================================================================================
BOOLEAN IsIrpValidAtDispatch(PVOID Ptr) {
    // Quick validation without MmIsAddressValid
    if ((ULONG64)Ptr < 0xFFFF000000000000) return FALSE;
    if ((ULONG64)Ptr & 0x7) return FALSE;

    __try {
        CSHORT type = *(CSHORT*)Ptr;
        if (type != 0x06 && type != 0x08 && type != 0x18) return FALSE;

        USHORT size = *((USHORT*)Ptr + 1);
        if (size < 0x40 || size > 0x1000) return FALSE;

        return TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

// =========================================================================================
// MAIN FUNCTION: InjectXboxWdf (IRQL-Safe Edition)
// Strategy: Pointer Hunter -> Queue Detection -> Deferred Injection
// =========================================================================================
NTSTATUS InjectXboxWdf(PDEVICE_OBJECT DeviceObject, XBOX_INPUT_REPORT* NewReport, ULONG ListOffset, ULONG LockOffset) {
    // Validate initial parameters at PASSIVE_LEVEL
    if (KeGetCurrentIrql() > APC_LEVEL) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (!DeviceObject || !NewReport) return STATUS_INVALID_PARAMETER;
    if (!DeviceObject->DeviceExtension) {
        return STATUS_NOT_FOUND;
    }

    ULONG_PTR* extension = (ULONG_PTR*)DeviceObject->DeviceExtension;
    int queuesChecked = 0;

    // Scan at PASSIVE_LEVEL first to find candidates
    for (int i = 0; i < 1024; i++) {
        ULONG_PTR candidatePtr = extension[i];

        if (candidatePtr > 0xFFFF000000000000 && (candidatePtr & 0x7) == 0) {
            if (MmIsAddressValid((PVOID)candidatePtr)) {
                PUCHAR objectBase = (PUCHAR)candidatePtr;

                // Scan for queue structures
                for (int offset = 0; offset < 0x400; offset += 8) {
                    PLIST_ENTRY pQueueHead = (PLIST_ENTRY)(objectBase + offset);

                    if (MmIsAddressValid(pQueueHead) &&
                        MmIsAddressValid(pQueueHead->Flink) &&
                        MmIsAddressValid(pQueueHead->Blink)) {

                        if (pQueueHead->Flink->Blink == pQueueHead &&
                            pQueueHead->Blink->Flink == pQueueHead) {

                            if (pQueueHead->Flink != pQueueHead) {

                                // Find spinlock (try multiple offsets)
                                for (int lockOffsetTry = 0; lockOffsetTry <= 0x20; lockOffsetTry += 8) {
                                    PUCHAR tryLockAddr = (PUCHAR)pQueueHead - lockOffsetTry;

                                    if (MmIsAddressValid(tryLockAddr)) {
                                        __try {
                                            ULONG_PTR lockValue = *(ULONG_PTR*)tryLockAddr;

                                            if (lockValue == 0 || (lockValue > 0xFFFF000000000000 && (lockValue & 0x7) == 0)) {
                                                queuesChecked++;
                                                PKSPIN_LOCK pSpinLock = (PKSPIN_LOCK)tryLockAddr;

                                                // ACQUIRE SPINLOCK - NOW AT DISPATCH_LEVEL
                                                KIRQL oldIrql;
                                                KeAcquireSpinLock(pSpinLock, &oldIrql);

                                                // Search for IRP at DISPATCH_LEVEL
                                                PLIST_ENTRY current = pQueueHead->Flink;
                                                PIRP targetIrp = NULL;
                                                PLIST_ENTRY targetPacket = NULL;
                                                int iterations = 0;

                                                // Walk the queue list
                                                while (current != pQueueHead && iterations < 16 && !targetIrp) {
                                                    if (MmIsAddressValid(current)) {
                                                        // Try to find IRP pointer near this list entry
                                                        for (int probeOffset = -0x20; probeOffset <= 0x20; probeOffset += 8) {
                                                            PUCHAR probeAddr = (PUCHAR)current + probeOffset;

                                                            if (MmIsAddressValid(probeAddr)) {
                                                                __try {
                                                                    PVOID val = *(PVOID*)probeAddr;

                                                                    if (val && IsIrpValidAtDispatch(val)) {
                                                                        PIRP pCand = (PIRP)val;

                                                                        // Quick IRP validation
                                                                        if (!pCand->Cancel) {
                                                                            // Get stack location pointer (approximate)
                                                                            PIO_STACK_LOCATION st = (PIO_STACK_LOCATION)((PUCHAR)pCand + sizeof(IRP) + (pCand->CurrentLocation * sizeof(IO_STACK_LOCATION)));

                                                                            if ((ULONG64)st > 0xFFFF000000000000) {
                                                                                UCHAR majorFunction = *(PUCHAR)st;
                                                                                if (majorFunction == IRP_MJ_READ ||
                                                                                    majorFunction == IRP_MJ_DEVICE_CONTROL ||
                                                                                    majorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) {

                                                                                    targetIrp = pCand;
                                                                                    targetPacket = current;
                                                                                    break;
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                                __except (EXCEPTION_EXECUTE_HANDLER) {
                                                                    continue;
                                                                }
                                                            }
                                                        }
                                                    }

                                                    current = current->Flink;
                                                    iterations++;
                                                }

                                                // If found, prepare for deferred processing
                                                if (targetIrp && targetPacket) {
                                                    // Prevent cancellation
                                                    if (InterlockedCompareExchangePointer(
                                                        (PVOID*)&targetIrp->CancelRoutine,
                                                        NULL,
                                                        targetIrp->CancelRoutine) == NULL) {

                                                        KeReleaseSpinLock(pSpinLock, oldIrql);
                                                        continue; // Already cancelled
                                                    }

                                                    // Remove from queue
                                                    RemoveEntryList(targetPacket);
                                                    KeReleaseSpinLock(pSpinLock, oldIrql);

                                                    // Create work item for PASSIVE_LEVEL processing
                                                    PINJECT_WORK_ITEM workItem = (PINJECT_WORK_ITEM)ExAllocatePoolWithTag(
                                                        NonPagedPool,
                                                        sizeof(INJECT_WORK_ITEM),
                                                        'kniX'
                                                    );

                                                    if (workItem) {
                                                        workItem->TargetIrp = targetIrp;
                                                        RtlCopyMemory(&workItem->Report, NewReport, sizeof(XBOX_INPUT_REPORT));

                                                        // Initialize work item
                                                        ExInitializeWorkItem(&workItem->WorkItem, InjectWorkRoutine, workItem);

                                                        // Queue work item to system worker thread
                                                        ExQueueWorkItem(&workItem->WorkItem, DelayedWorkQueue);

                                                        return STATUS_SUCCESS;
                                                    }
                                                    else {
                                                        // Failed to allocate work item, complete IRP with error
                                                        targetIrp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                                                        targetIrp->IoStatus.Information = 0;
                                                        IoCompleteRequest(targetIrp, IO_NO_INCREMENT);
                                                        return STATUS_INSUFFICIENT_RESOURCES;
                                                    }
                                                }
                                                else {
                                                    KeReleaseSpinLock(pSpinLock, oldIrql);
                                                }
                                            }
                                        }
                                        __except (EXCEPTION_EXECUTE_HANDLER) {
                                            // Continue searching
                                            continue;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return STATUS_NOT_FOUND;
}
// ==================================================================================
//                              MOUCLASS INJECTION LOGIC
// ==================================================================================

typedef VOID(*PMOUSE_SERVICE_CALLBACK)(
    PDEVICE_OBJECT DeviceObject,
    PMOUSE_INPUT_DATA InputDataStart,
    PMOUSE_INPUT_DATA InputDataEnd,
    PULONG InputDataConsumed
    );

typedef struct _MOUCLASS_CONTEXT {
    PDRIVER_OBJECT          MouDriverObject;
    BOOLEAN                 Initialized;

    // Cache
    PDEVICE_OBJECT          CachedDevice;
    PMOUSE_SERVICE_CALLBACK CachedCallback;

    // Identity Theft: Valid UnitId
    USHORT                  CachedUnitId;
} MOUCLASS_CONTEXT, * PMOUCLASS_CONTEXT;

MOUCLASS_CONTEXT g_Mou = { 0 };

PMOUSE_SERVICE_CALLBACK ResolveCallback(PDEVICE_OBJECT devObj) {
    if (!devObj || !devObj->DeviceExtension) return NULL;
    if (!MmIsAddressValid(devObj->DeviceExtension)) return NULL;

    // 1. POINTER CHECK (Method 1)
    // Check if the pointer at 0xD0 looks like a kernel address
    ULONG64 ptrAtD0 = *(PULONG64)((PUCHAR)devObj->DeviceExtension + 0xD0);
    if (ptrAtD0 < 0xFFFF000000000000) return NULL;

    // 2. RESOLVE CALLBACK
    ULONG64 mouClassBase = ptrAtD0 - 0x58F0;
    PMOUSE_SERVICE_CALLBACK callback = (PMOUSE_SERVICE_CALLBACK)(mouClassBase + 0x18A0);

    if ((ULONG64)callback < 0xFFFF000000000000) return NULL;

    // 3. STEAL THE UNIT ID (Your Reversing Work)
    // Offset +0xB4 is a DWORD (4 bytes), but the packet struct needs a USHORT.
    // We read 4 bytes, then cast it down. 
    // Usually 0, 1, or 2, so data loss isn't an issue.
    if (MmIsAddressValid((PUCHAR)devObj->DeviceExtension + 0xB4)) {
        ULONG rawId = *(PULONG)((PUCHAR)devObj->DeviceExtension + 0xB4);
        g_Mou.CachedUnitId = (USHORT)rawId;

        // Optional Debug Print to confirm you got it
        // KLOG("[MouClass] Snagged UnitId: %d from Device %p", rawId, devObj);
    }

    return callback;
}

// --- SAFETY SCANNER ---
// Scans a device extension to find the offset of the Pending IRP Queue
void ScanForQueueValues(PDEVICE_OBJECT DeviceObject) {
    if (!DeviceObject) return;

    ULONG_PTR* rawExt = (ULONG_PTR*)DeviceObject->DeviceExtension;
    if (!MmIsAddressValid(rawExt)) {
        KLOG("[V-SCAN] Device Extension is invalid!");
        return;
    }

    KLOG("[V-SCAN] Scanning DevExt: 0x%p (Size unknown, scanning 0x1000 bytes)...", rawExt);

    // Scan the first 4096 bytes (0x1000)
    for (ULONG i = 0; i < 0x200; i++) { // 0x200 * 8 bytes = 0x1000
        // We are looking for a LIST_ENTRY (2 pointers)
        // 1. Is the memory valid?
        if (!MmIsAddressValid(&rawExt[i]) || !MmIsAddressValid(&rawExt[i + 1])) continue;

        ULONG_PTR flink = rawExt[i];
        ULONG_PTR blink = rawExt[i + 1];
        ULONG_PTR currentAddr = (ULONG_PTR)&rawExt[i];

        // 2. CHECK: EMPTY LIST Signature (Flink points to Self)
        if (flink == currentAddr && blink == currentAddr) {
            // Found a list head that points to itself! 
            // This is a strong candidate for an empty queue.

            // In WDF, the SpinLock is usually immediately BEFORE or AFTER the list.
            // Let's check the previous 8 bytes for a Lock (usually 0)
            ULONG_PTR possibleLock = 0;
            if (i > 0 && MmIsAddressValid(&rawExt[i - 1])) {
                possibleLock = rawExt[i - 1];
            }

            KLOG("[V-SCAN] CANDIDATE FOUND @ Offset 0x%X", i * 8);
            KLOG("        [-] Type: Empty List (Self-Ref)");
            KLOG("        [-] Prev Qword (Possible Lock): 0x%p", (void*)possibleLock);
        }

        // 3. CHECK: POPULATED LIST Signature
        // Flink points to valid kernel pool memory, Blink points to valid kernel pool
        else if (MmIsAddressValid((PVOID)flink) && MmIsAddressValid((PVOID)blink)) {
            // Heuristic: Pointers usually look like 0xFFFF...
            if ((flink & 0xFFFF000000000000) && (blink & 0xFFFF000000000000)) {

                // If Flink points back to us (Blink of next item is us), it's definitely a list
                PLIST_ENTRY nextItem = (PLIST_ENTRY)flink;
                if (MmIsAddressValid(nextItem) && nextItem->Blink == (PLIST_ENTRY)currentAddr) {
                    KLOG("[V-SCAN] CANDIDATE FOUND @ Offset 0x%X", i * 8);
                    KLOG("        [-] Type: Active Queue (Linked)");
                    KLOG("        [-] Flink: 0x%p", (void*)flink);
                }
            }
        }
    }
    KLOG("[V-SCAN] Scan complete.");
}

NTSTATUS ResolveServiceCallback() {
    NTSTATUS status;
    UNICODE_STRING driverName;
    PDRIVER_OBJECT kbdHidDriver = NULL;
    PDEVICE_OBJECT currentDevice = NULL;

    KLOG("[V] Resolving ServiceCallback via kbdhid.sys offsets...");

    // 1. Get a reference to the kbdhid driver object
    RtlInitUnicodeString(&driverName, L"\\Driver\\kbdhid");
    status = ObReferenceObjectByName(
        &driverName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        0,
        *IoDriverObjectType,
        KernelMode,
        NULL,
        (PVOID*)&kbdHidDriver
    );

    if (!NT_SUCCESS(status)) {
        KLOG("[V] Failed to find \\Driver\\kbdhid (Status: 0x%X). Are you sure it's loaded?", status);
        return status;
    }

    // 2. Iterate through all devices owned by kbdhid
    // We are looking for the one that is connected to OUR Class Device (g_ClassDevice)
    currentDevice = kbdHidDriver->DeviceObject;
    while (currentDevice) {
        // Check the extension for the signature
        char* ext = (char*)currentDevice->DeviceExtension;

        if (ext) {
            // Check Offset 0x88 for our Class Device Object pointer
            PDEVICE_OBJECT* pClassDevStored = (PDEVICE_OBJECT*)(ext + KBDHID_OFFSET_CLASS_DEVICE);

            if (*pClassDevStored == g_ClassDevice) {
                KLOG("[V] MATCH FOUND! Port Device: 0x%p", currentDevice);

                // 3. Extract the Callback from Offset 0x90
                PVOID* pCallback = (PVOID*)(ext + KBDHID_OFFSET_SERVICE_CB);
                g_ServiceCallback = *pCallback;

                KLOG("[V] ServiceCallback Stolen: 0x%p", g_ServiceCallback);

                ObDereferenceObject(kbdHidDriver);
                return STATUS_SUCCESS;
            }
        }

        currentDevice = currentDevice->NextDevice;
    }

    KLOG("[V] Failed to find matching Port Device in kbdhid chain.");
    ObDereferenceObject(kbdHidDriver);
    return STATUS_NOT_FOUND;
}


// Helper to check if a pointer is valid kernel memory (Basic check)
BOOLEAN IsKernelAddress(PVOID Address) {
    return ((ULONG_PTR)Address > 0xFFFF000000000000);
}

// ================= SCANNER =================
// Scans a specific Port Driver (e.g., kbdhid or i8042prt) to find who owns our Class Device
PVOID ScanDriverForCallback(PDRIVER_OBJECT DriverObj, PDEVICE_OBJECT ClassDevice) {
    PDEVICE_OBJECT currentPortDev = DriverObj->DeviceObject;

    // Walk through every device this driver owns
    while (currentPortDev) {
        if (currentPortDev->DeviceExtension) {
            // SCAN THE DEVICE EXTENSION MEMORY
            // We look for a pointer to OUR ClassDevice.
            // If we find it, the pointer immediately following it is the ServiceCallback.
            // struct CONNECT_DATA { PDEVICE_OBJECT ClassDev; PVOID ServiceCB; };

            PVOID* start = (PVOID*)currentPortDev->DeviceExtension;
            PVOID* end = (PVOID*)((PUCHAR)start + 4096); // Scan 4KB (plenty)

            while (start < end) {
                // Check for the signature: [Pointer to ClassDevice] [Pointer to Function]
                if (*start == ClassDevice) {
                    PVOID possibleCallback = *(start + 1);

                    // Sanity check: Is the next value a valid kernel address?
                    if (IsKernelAddress(possibleCallback)) {
                        KLOG("[V] MATCH: Found ClassDevice at offset 0x%X in %wZ",
                            (PUCHAR)start - (PUCHAR)currentPortDev->DeviceExtension,
                            &DriverObj->DriverName);
                        return possibleCallback;
                    }
                }
                start++;
            }
        }
        currentPortDev = currentPortDev->NextDevice;
    }
    return NULL;
}

// ================= INITIALIZER =================
NTSTATUS InitializeGhostWriter() {
    NTSTATUS status;
    UNICODE_STRING driverName;
    PDRIVER_OBJECT portDriver = NULL;
    PFILE_OBJECT fileObj;

    KLOG("[V] ::InitializeGhostWriter - Starting Universal Scan...");

    // STEP 1: Get the Class Device (Try Class1 first, then Class0)
    // We prefer Class1 (USB), but if you are on PS/2, we need Class0.
    UNICODE_STRING className;
    RtlInitUnicodeString(&className, L"\\Device\\KeyboardClass1");
    status = IoGetDeviceObjectPointer(&className, FILE_READ_ATTRIBUTES, &fileObj, &g_ClassDevice);

    if (!NT_SUCCESS(status)) {
        KLOG("[V] Class1 not found. Trying Class0...");
        RtlInitUnicodeString(&className, L"\\Device\\KeyboardClass0");
        status = IoGetDeviceObjectPointer(&className, FILE_READ_ATTRIBUTES, &fileObj, &g_ClassDevice);
    }

    if (!NT_SUCCESS(status)) {
        KLOG("[V] CRITICAL: Could not find ANY KeyboardClass device!");
        return STATUS_NOT_FOUND;
    }

    ObDereferenceObject(fileObj); // We have the pointer in g_ClassDevice
    KLOG("[V] Target Class Device: 0x%p (%wZ)", g_ClassDevice, &className);


    // STEP 2: Scan 'kbdhid' (USB Driver)
    RtlInitUnicodeString(&driverName, L"\\Driver\\kbdhid");
    status = ObReferenceObjectByName(&driverName, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, (PVOID*)&portDriver);

    if (NT_SUCCESS(status)) {
        KLOG("[V] Scanning kbdhid...");
        g_ServiceCallback = ScanDriverForCallback(portDriver, g_ClassDevice);
        ObDereferenceObject(portDriver);
        if (g_ServiceCallback) goto SUCCESS;
    }


    // STEP 3: Scan 'i8042prt' (PS/2 Driver)
    // If the first scan failed, this is our backup.
    RtlInitUnicodeString(&driverName, L"\\Driver\\i8042prt");
    status = ObReferenceObjectByName(&driverName, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, (PVOID*)&portDriver);

    if (NT_SUCCESS(status)) {
        KLOG("[V] Scanning i8042prt...");
        g_ServiceCallback = ScanDriverForCallback(portDriver, g_ClassDevice);
        ObDereferenceObject(portDriver);
        if (g_ServiceCallback) goto SUCCESS;
    }

    // FAILURE
    KLOG("[V] FATAL: ServiceCallback not found in kbdhid OR i8042prt.");
    g_ClassDevice = NULL; // Reset to prevent usage
    return STATUS_UNSUCCESSFUL;

SUCCESS:
    KLOG("[V] SUCCESS: Callback resolved at 0x%p. System is hooked.", g_ServiceCallback);
    return STATUS_SUCCESS;
}

typedef VOID(NTAPI* PKEYBOARD_CLASS_SERVICE_CALLBACK)(
    IN PDEVICE_OBJECT ClassDeviceObject,
    IN PKEYBOARD_INPUT_DATA InputDataStart,
    IN PKEYBOARD_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
    );

NTSTATUS InjectKeyGhost(USHORT makeCode, USHORT flags) {
    if (!g_ServiceCallback || !g_ClassDevice) {
        return STATUS_DEVICE_NOT_READY;
    }

    KEYBOARD_INPUT_DATA input;
    input.UnitId = 0;
    input.MakeCode = makeCode;
    input.Flags = flags;
    input.Reserved = 0;
    input.ExtraInformation = 0;

    ULONG consumed = 0;

    // CAST IT
    PKEYBOARD_CLASS_SERVICE_CALLBACK callback = (PKEYBOARD_CLASS_SERVICE_CALLBACK)g_ServiceCallback;

    // CALL IT
    // This runs immediately. No waiting for IRQL, no waiting for interrupts.
    callback(g_ClassDevice, &input, (&input) + 1, &consumed);

    return STATUS_SUCCESS;
}

NTSTATUS InitializeMouClass() {
    UNICODE_STRING deviceName;
    PFILE_OBJECT fileObject;
    PDEVICE_OBJECT tempDevice;
    NTSTATUS status;

    // We only use this to grab the DriverObject. 
    // We don't keep the device object itself.
    RtlInitUnicodeString(&deviceName, L"\\Device\\PointerClass0");
    status = IoGetDeviceObjectPointer(&deviceName, FILE_READ_ATTRIBUTES, &fileObject, &tempDevice);

    if (!NT_SUCCESS(status)) {
        RtlInitUnicodeString(&deviceName, L"\\Device\\PointerClass1");
        status = IoGetDeviceObjectPointer(&deviceName, FILE_READ_ATTRIBUTES, &fileObject, &tempDevice);
    }

    if (NT_SUCCESS(status)) {
        g_Mou.MouDriverObject = tempDevice->DriverObject;
        ObDereferenceObject(fileObject);
        g_Mou.Initialized = TRUE;
        KLOG("[MouClass] Injection Ready. DriverObject: %p", g_Mou.MouDriverObject);
        return STATUS_SUCCESS;
    }

    return status;
}

// Point 4: Batching Support. Pass an array of inputs to reduce IRQL toggling.
NTSTATUS InjectMouseInputBatch(MOUSE_INPUT_DATA* inputs, ULONG count) {
    if (!g_Mou.Initialized || !g_Mou.MouDriverObject || !inputs || count == 0) {
        return STATUS_DEVICE_NOT_READY;
    }

    ULONG consumed = 0;
    KIRQL oldIrql;
    PDEVICE_OBJECT targetDevice = NULL;
    PMOUSE_SERVICE_CALLBACK targetCallback = NULL;
    BOOLEAN useCache = FALSE;

    // 1. Try Cache First
    if (g_Mou.CachedDevice && g_Mou.CachedCallback) {
        targetDevice = g_Mou.CachedDevice;
        targetCallback = g_Mou.CachedCallback;
        useCache = TRUE;
    }
    else {
        // Cache miss? Scan the list (Slow Path)
        for (PDEVICE_OBJECT devObj = g_Mou.MouDriverObject->DeviceObject; devObj != NULL; devObj = devObj->NextDevice) {
            PMOUSE_SERVICE_CALLBACK cb = ResolveCallback(devObj);
            if (cb) {
                targetDevice = devObj;
                targetCallback = cb;
                break;
            }
        }
    }

    if (!targetDevice || !targetCallback) {
        return STATUS_DEVICE_NOT_READY;
    }

    // Point 2: Apply UnitId
    // We update the inputs to match the cached UnitId (or 0 if we haven't found it yet)
    for (ULONG i = 0; i < count; i++) {
        inputs[i].UnitId = g_Mou.CachedUnitId;

        // Safety check on flags (Point 2 Pro-tip)
        // Ensure we aren't sending flags that scream "INJECTED"
        if (inputs[i].Flags & MOUSE_VIRTUAL_DESKTOP) {
            inputs[i].Flags &= ~MOUSE_VIRTUAL_DESKTOP;
        }
    }

    // Point 4: Raise IRQL ONCE for the whole batch
    KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);

    // Point 1: The "Condom" (SEH Wrapper)
    __try {
        // Loop through the batch
        for (ULONG i = 0; i < count; i++) {

            // Point 3: "Ghost" Return Address (Concept)
            // If we had a gadget like "jmp rax" inside mouhid.sys, we would jump to it.
            // For now, we call directly, but safely.

            /* // SPOOFING LOGIC (Requires Assembly Gadget)
             // void CallViaGadget(PVOID gadget, PVOID function, ...);
             // CallViaGadget(g_Mou.GadgetJmpRax, targetCallback, targetDevice, &inputs[i], &inputs[i] + 1, &consumed);
            */

            // Standard Call
            targetCallback(targetDevice, &inputs[i], &inputs[i] + 1, &consumed);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // CRASH PREVENTED
        // If we get here, the device pointer is dead.
        consumed = 0;

        // Invalidate cache immediately
        g_Mou.CachedDevice = NULL;
        g_Mou.CachedCallback = NULL;

        // Lower IRQL before returning!
        KeLowerIrql(oldIrql);
        return STATUS_UNSUCCESSFUL;
    }

    KeLowerIrql(oldIrql);

    // Cache Management
    if (consumed > 0) {
        // Success! Update cache if we weren't using it
        if (!useCache) {
            g_Mou.CachedDevice = targetDevice;
            g_Mou.CachedCallback = targetCallback;
        }
    }
    else {
        // If we sent data but nobody ate it, the device might be stale.
        // Clear cache to force a rescan next time.
        g_Mou.CachedDevice = NULL;
        g_Mou.CachedCallback = NULL;
    }

    return STATUS_SUCCESS;
}

// Wrapper for single input (Backward Compatibility)
NTSTATUS InjectMouseInput(LONG x, LONG y, USHORT buttonFlags) {
    MOUSE_INPUT_DATA mid = { 0 };
    mid.Flags = MOUSE_MOVE_RELATIVE;
    mid.ButtonFlags = buttonFlags;
    mid.LastX = x;
    mid.LastY = y;

    // UnitId is handled inside the Batch function now

    return InjectMouseInputBatch(&mid, 1);
}

// Helper struct for IOCTL payload
typedef struct _MOUSE_REQUEST {
    LONG x;
    LONG y;
    USHORT button_flags;
} MOUSE_REQUEST, * PMOUSE_REQUEST;

// ==================================================================================

// =====================================================================
// [DRIVER DUMP] Request struct — shared with usermode
// =====================================================================
#pragma pack(push, 1)
typedef struct _DRIVER_DUMP_REQUEST {
    WCHAR  target_name[256]; // Service name OR filename (e.g. L"atvi-randgrid_sr" or L"randgrid.sys")
    UINT64 out_image_size;   // [OUT] Kernel fills this with the actual image size in bytes
} DRIVER_DUMP_REQUEST, *PDRIVER_DUMP_REQUEST;
#pragma pack(pop)

// =====================================================================
// [DRIVER DUMP] Stealth kernel image reader — walks PsLoadedModuleList,
// finds the target driver by service name or filename (case-insensitive),
// and copies its image directly into the caller's locked output buffer.
// Uses METHOD_OUT_DIRECT so no double-buffering in nonpaged pool.
// =====================================================================
static NTSTATUS handle_dump_kernel_driver(PIRP irp, PIO_STACK_LOCATION stack_irp, SIZE_T* information) {
    *information = 0;

    // --- Validate input buffer ---
    ULONG in_len  = stack_irp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG out_len = stack_irp->Parameters.DeviceIoControl.OutputBufferLength;

    if (in_len < sizeof(DRIVER_DUMP_REQUEST)) {
        KLOG("[KDump] Input buffer too small: %lu", in_len);
        return STATUS_BUFFER_TOO_SMALL;
    }

    PDRIVER_DUMP_REQUEST req = (PDRIVER_DUMP_REQUEST)irp->AssociatedIrp.SystemBuffer;
    if (!req) return STATUS_INVALID_PARAMETER;

    // Ensure the target_name is null-terminated so RtlInitUnicodeString is safe
    req->target_name[255] = L'\0';

    UNICODE_STRING search_name;
    RtlInitUnicodeString(&search_name, req->target_name);

    KLOG("[KDump] Looking for driver: %wZ", &search_name);

    // --- Walk PsLoadedModuleList to find the target ---
    PVOID  image_base  = NULL;
    SIZE_T image_size  = 0;

    // PsLoadedModuleList is a LIST_ENTRY; each node is an LDR_DATA_TABLE_ENTRY
    // We need to walk it under a try/except because some entries may be partially
    // unmapped (e.g. manually-mapped drivers that wiped their header).
    __try {
        PLIST_ENTRY list_head = PsLoadedModuleList;
        PLIST_ENTRY entry     = list_head->Flink;

        while (entry && entry != list_head) {
            PLDR_DATA_TABLE_ENTRY mod = CONTAINING_RECORD(entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
            entry = entry->Flink;

            if (!mod || !MmIsAddressValid(mod)) continue;
            if (!mod->DllBase || !mod->SizeOfImage) continue;

            // mod->BaseDllName is the filename (e.g. "randgrid.sys")
            // mod->FullDllName is the full NT path
            // We match against either.
            if (RtlEqualUnicodeString(&mod->BaseDllName, &search_name, TRUE)) {
                image_base = mod->DllBase;
                image_size = mod->SizeOfImage;
                KLOG("[KDump] Matched by BaseDllName. Base=%p Size=0x%IX", image_base, image_size);
                break;
            }

            // Also try stripping the .sys extension from BaseDllName for service-name matching
            // e.g. input "atvi-randgrid_sr" should match BaseDllName "atvi-randgrid_sr.sys"
            if (mod->BaseDllName.Length > 8) { // more than ".sys" + 1 char
                UNICODE_STRING stem = mod->BaseDllName;
                // Check if it ends in .sys (case-insensitive)
                WCHAR* buf = stem.Buffer;
                USHORT len = stem.Length / sizeof(WCHAR);
                if (len > 4 &&
                    (buf[len-4] == L'.' || buf[len-4] == L'.') &&
                    (buf[len-3] == L's' || buf[len-3] == L'S') &&
                    (buf[len-2] == L'y' || buf[len-2] == L'Y') &&
                    (buf[len-1] == L's' || buf[len-1] == L'S')) {
                    // Temporarily shorten to strip .sys
                    stem.Length -= (4 * sizeof(WCHAR));
                    stem.MaximumLength = stem.Length;
                    if (RtlEqualUnicodeString(&stem, &search_name, TRUE)) {
                        image_base = mod->DllBase;
                        image_size = mod->SizeOfImage;
                        KLOG("[KDump] Matched by stem. Base=%p Size=0x%IX", image_base, image_size);
                        break;
                    }
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        KLOG("[KDump] Exception while walking PsLoadedModuleList: 0x%X", GetExceptionCode());
        return STATUS_UNSUCCESSFUL;
    }

    if (!image_base || !image_size) {
        KLOG("[KDump] Driver not found: %wZ", &search_name);
        return STATUS_NOT_FOUND;
    }

    // With METHOD_OUT_DIRECT the kernel writes to the MDL output buffer,
    // NOT back into SystemBuffer. So we always go through MdlAddress.
    PMDL mdl = irp->MdlAddress;
    if (!mdl) {
        // No output buffer at all — caller just wants to know if the driver exists.
        // Fill out_image_size in SystemBuffer as a best-effort (won't reach usermode
        // reliably with OUT_DIRECT, but harmless).
        req->out_image_size = (UINT64)image_size;
        *information = 0;
        return STATUS_BUFFER_TOO_SMALL; // Signals: found it, but you need a buffer
    }

    PVOID dest = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);
    if (!dest) {
        KLOG("[KDump] Failed to map MDL pages");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // --- Phase 1: Size query ---
    // Usermode sends an 8-byte output buffer. We write the image size as a
    // UINT64 into the MDL buffer so it comes back reliably via OUT_DIRECT.
    if (out_len == sizeof(UINT64)) {
        KLOG("[KDump] Size query: image_size=0x%IX", image_size);
        *(UINT64*)dest = (UINT64)image_size;
        *information = sizeof(UINT64);
        return STATUS_SUCCESS;
    }

    // --- Phase 2: Full image dump ---
    SIZE_T copy_size = min(image_size, (SIZE_T)out_len);

    __try {
        RtlCopyMemory(dest, image_base, copy_size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        KLOG("[KDump] Exception during image copy: 0x%X", GetExceptionCode());
        // Partial copy is still useful — return however far we got
    }

    *information = copy_size;
    KLOG("[KDump] Dump complete. Copied 0x%IX bytes from %p", copy_size, image_base);
    return STATUS_SUCCESS;
}

namespace driver {
    namespace codes {
        // Obfuscated IOCTL codes
#define IOCTL_BASE 0x9C0
        constexpr ULONG attach = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG get_module = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG get_pid = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG batch_read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG get_module_by_peb = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG get_image_base = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG get_peb = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG write = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x809, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG nt_read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80A, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG get_logs = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG NT_READ_PM_KERNEL_IOCTL = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80C, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG page_walk_read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG page_walk_write = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x903, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG spoof_enable = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x904, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG register_ss_hook = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x910, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG allocate_virtual_memory = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x911, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG protect_virtual_memory = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x912, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG free_virtual_memory = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x913, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG get_kernel_module_info = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x920, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG read_kernel_memory = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x921, METHOD_OUT_DIRECT, FILE_SPECIAL_ACCESS);
        constexpr ULONG kernel_inject = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x930, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG kernel_manual_map = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x931, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG spoof_disk_serial = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x940, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG mouse_event = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x950, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG keyboard_event = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x960, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG controller_event = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x951, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG install_screenshot_hook = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x990, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG dump_kernel_driver = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x980, METHOD_OUT_DIRECT, FILE_SPECIAL_ACCESS);
    }

    NTSTATUS create(PDEVICE_OBJECT device_object, PIRP irp) {
        UNREFERENCED_PARAMETER(device_object);
        irp->IoStatus.Status = STATUS_SUCCESS;
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    NTSTATUS close(PDEVICE_OBJECT device_object, PIRP irp) {
        UNREFERENCED_PARAMETER(device_object);
        irp->IoStatus.Status = STATUS_SUCCESS;
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        clear_screenshot_hook_objects();
        return STATUS_SUCCESS;
    }

    NTSTATUS device_control(PDEVICE_OBJECT device_object, PIRP irp) {
        UNREFERENCED_PARAMETER(device_object);

        if ((irp->Type == IO_TYPE_IRP && KeGetCurrentIrql() <= APC_LEVEL))
        {
            NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
            SIZE_T information = 0;
            PIO_STACK_LOCATION stack_irp = IoGetCurrentIrpStackLocation(irp);

            if (!stack_irp || !irp->AssociatedIrp.SystemBuffer) {
                irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                irp->IoStatus.Information = 0;
                IoCompleteRequest(irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            ULONG ioctl = stack_irp->Parameters.DeviceIoControl.IoControlCode;
            // KLOG("[device_control] Received Ioctl=0x%08X", ioctl); // Comment out to reduce spam

            switch (ioctl) {
            case codes::attach:
                status = handle_attach(irp, &information);
                break;

            case codes::get_pid:
                status = handle_get_pid(irp, &information);
                break;

            case codes::read:
                status = handle_read(irp, &information);
                break;

            case codes::batch_read:
                status = handle_batch_read(irp, stack_irp, &information);
                break;

            case codes::get_module_by_peb:
                status = handle_get_module_by_peb(irp, &information);
                break;

            case codes::get_image_base:
                status = handle_get_image_base(irp, &information);
                break;

            case codes::get_peb:
                status = handle_get_peb(irp, &information);
                break;

            case codes::write:
                status = handle_write(irp, &information);
                break;
            case codes::nt_read:
                status = handle_nt_read(irp, &information);
                break;
            case codes::page_walk_read:
                status = handle_page_walk_read(irp, &information);
                break;
            case codes::spoof_enable: {
                PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
                ULONG output_len = stack->Parameters.DeviceIoControl.OutputBufferLength;

                if (output_len < 18) {
                    status = STATUS_BUFFER_TOO_SMALL;
                    information = 0;
                    break;
                }

                char* buffer = (char*)irp->AssociatedIrp.SystemBuffer;
                RtlCopyMemory(buffer, "SPOOFED-HWID-1234", 17);
                buffer[17] = '\0';

                information = 18;
                status = STATUS_SUCCESS;
                break;
            }
            case codes::register_ss_hook:
                status = handle_register_screenshot_hook(irp, &information);
                break;
            case codes::allocate_virtual_memory: {
                // ... (Kept your exact allocate code here) ...
                status = STATUS_UNSUCCESSFUL;
                pk_alloc_mem_request in = (pk_alloc_mem_request)irp->AssociatedIrp.SystemBuffer;
                PEPROCESS target_proc = NULL;

                if (!PsLookupProcessByProcessIdPtr) {
                    status = STATUS_FAILED_DRIVER_ENTRY;
                }
                else {
                    status = PsLookupProcessByProcessIdPtr((HANDLE)in->pid, &target_proc);
                }

                if (NT_SUCCESS(status)) {
                    KAPC_STATE apc;
                    KeStackAttachProcess(target_proc, &apc);
                    PVOID base_addr = (PVOID)in->addr;
                    status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &base_addr, 0, &in->size,
                        in->allocation_type, in->protect);

                    if (NT_SUCCESS(status) && base_addr) {
                        // Memory returned by ZwAllocateVirtualMemory is already zeroed by the OS.
                        // Touching user-mode memory directly from kernel mode can trigger SMAP violations.
                    }
                    KeUnstackDetachProcess(&apc);
                    ObDereferenceObject(target_proc);
                    in->addr = (ULONGLONG)base_addr;
                }
                information = sizeof(k_alloc_mem_request);
                break;
            }
            case codes::protect_virtual_memory: {
                pk_protect_mem_request in = (pk_protect_mem_request)irp->AssociatedIrp.SystemBuffer;
                PEPROCESS target_proc;
                status = PsLookupProcessByProcessIdPtr((HANDLE)in->pid, &target_proc);
                if (NT_SUCCESS(status)) {
                    KAPC_STATE apc;
                    ULONG old_protection;
                    PVOID base_addr = (PVOID)in->addr;
                    KeStackAttachProcess(target_proc, &apc);
                    status = ZwProtectVirtualMemory(ZwCurrentProcess(), &base_addr, &in->size, in->protect, &old_protection);
                    KeUnstackDetachProcess(&apc);
                    in->protect = old_protection;
                    ObDereferenceObject(target_proc);
                }
                information = sizeof(k_protect_mem_request);
                break;
            }
            case codes::free_virtual_memory: {
                status = handle_free_virtual_memory(irp, &information);
                break;
            }
            case codes::kernel_inject:
                status = handle_kernel_inject_apc(irp, &information);
                break;
            case codes::kernel_manual_map:
                status = KernelManualMapDll(irp, &information);
                break;
            case codes::get_logs:
                status = handle_get_logs(irp, &information);
                break;
            case codes::page_walk_write:
                status = handle_page_walk_write(irp, &information);
                break;
            case codes::keyboard_event: {
                PKEY_REQUEST req = (PKEY_REQUEST)irp->AssociatedIrp.SystemBuffer;
                // V: THIS CALLS THE GHOST WRITER WE JUST BUILT
                status = InjectKeyGhost(req->MakeCode, req->Flags);
                information = sizeof(KEY_REQUEST);
                break;
            }

            case codes::mouse_event: {
                PMOUSE_REQUEST req = (PMOUSE_REQUEST)irp->AssociatedIrp.SystemBuffer;
                if (req) {
                    status = InjectMouseInput(req->x, req->y, req->button_flags);
                    information = sizeof(MOUSE_REQUEST);
                }
                else {
                    status = STATUS_INVALID_PARAMETER;
                }
                break;
            }
                                   // [NEW] SCREENSHOT HOOK INSTALLER
// [UPDATED] AUTO-FINDER IOCTL
            case codes::install_screenshot_hook: {

                KLOG("[DETECTOR] Auto-Find started...");

                // 1. Find PDEV internally
                PVOID pdev_address = FindPdevAutomatically();

                if (!pdev_address) {
                    KLOG("[DETECTOR] Failed to find PDEV automatically.");
                    status = STATUS_NOT_FOUND;
                }
                else {
                    // 2. Install Hook directly
                    status = InstallBitBltHook(pdev_address);

                    if (NT_SUCCESS(status)) {
                        KLOG("[DETECTOR] Hook installed via Auto-Scan!");
                    }
                }

                information = 0;
                break;
            }
            // =====================================================================
            // [NEW] SPRITE PROTECTION IOCTL
            // =====================================================================
            // Inside switch(ioctl) ...
            case ioctl_protect_sprite_content: {
                // Use the namespace struct
                auto req = (GreProtect::pprotect_sprite_content)irp->AssociatedIrp.SystemBuffer;

                // Call the fixed function
                status = GreProtect::ProtectFn(req);
                information = 0;
                break;
            }
            case codes::controller_event: {
                status = HandleControllerEvent(irp, stack_irp);
                information = sizeof(CTRL_REQUEST);
                break;
            }
            case CTL_CODE(FILE_DEVICE_UNKNOWN, 0x953, METHOD_BUFFERED, FILE_SPECIAL_ACCESS): {
                status = HandleSelectController(irp, stack_irp);
                information = sizeof(SELECT_REQUEST);
                break;
            }

            case codes::dump_kernel_driver:
                status = handle_dump_kernel_driver(irp, stack_irp, &information);
                break;

            default:
                status = STATUS_INVALID_DEVICE_REQUEST;
                information = 0;
                break;
            }

            irp->IoStatus.Status = status;
            irp->IoStatus.Information = information;
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            return status;
        }
        else
        {
            irp->IoStatus.Status = STATUS_ACCESS_DENIED;
            irp->IoStatus.Information = 0;
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            return STATUS_ACCESS_DENIED;
        }
    }
}

PVOID g_arg1 = NULL;
PVOID g_arg2 = NULL;

NTSTATUS driver_main(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path) {
    UNREFERENCED_PARAMETER(registry_path);
    KeInitializeSpinLock(&g_log_lock);

    UNICODE_STRING s_PsLookupProcessByProcessId;
    RtlInitUnicodeString(&s_PsLookupProcessByProcessId, L"PsLookupProcessByProcessId");
    PsLookupProcessByProcessIdPtr = (PPS_LOOKUP_PROCESS_BY_PROCESS_ID)MmGetSystemRoutineAddress(&s_PsLookupProcessByProcessId);
    if (!PsLookupProcessByProcessIdPtr) {
        KLOG("Failed to resolve PsLookupProcessByProcessIdPtr");
        return STATUS_FAILED_DRIVER_ENTRY;
    }

    UNICODE_STRING s_MmGetVirtualForPhysical;
    RtlInitUnicodeString(&s_MmGetVirtualForPhysical, L"MmGetVirtualForPhysical");
    MmGetVirtualForPhysicalPtr = (PMM_GET_VIRTUAL_FOR_PHYSICAL)MmGetSystemRoutineAddress(&s_MmGetVirtualForPhysical);
    if (!MmGetVirtualForPhysicalPtr) {
        KLOG("Failed to resolve MmGetVirtualForPhysicalPtr");
        return STATUS_FAILED_DRIVER_ENTRY;
    }

    RtlGetVersion(&Version);

    UNICODE_STRING device_name;
    RtlInitUnicodeString(&device_name, L"\\Device\\coochie");

    PDEVICE_OBJECT device_object = nullptr;
    NTSTATUS status = IoCreateDevice(driver_object, 0, &device_name, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &device_object);
    if (!NT_SUCCESS(status)) {
        KLOG("IoCreateDevice failed: 0x%x", status);
        return status;
    }

    UNICODE_STRING symbolic_link;
    RtlInitUnicodeString(&symbolic_link, L"\\DosDevices\\coochie");
    status = IoCreateSymbolicLink(&symbolic_link, &device_name);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(device_object);
        KLOG("IoCreateSymbolicLink failed: 0x%x", status);
        return status;
    }

    if (ReadPageMemory::InitializeMemoryManagement() == STATUS_PROCEDURE_NOT_FOUND) {
        KLOG("Failed to initialize spy page memory management!");
        IoDeleteSymbolicLink(&symbolic_link);
        IoDeleteDevice(device_object);
        return STATUS_PROCEDURE_NOT_FOUND;
    }
    KLOG("Spy page memory initialized.");

    status = InitializeMouClass();
    if (!NT_SUCCESS(status)) {
        KLOG("Failed to initialize MouClass injection: 0x%X", status);
    }

    // =====================================================================
    // [NEW] INITIALIZE SPRITE PROTECTION
    // =====================================================================
    NTSTATUS greStatus = GreProtect::Init();
    if (!NT_SUCCESS(greStatus)) {
        KLOG("[GreProtect] Warning: Failed to find win32kfull export (0x%X). Sprite protection will fail.", greStatus);
    }

    // V: You were missing this block right here.
    // Call the function to setup the keyboard device object.
    status = InitializeGhostWriter();
    if (!NT_SUCCESS(status)) {
        KLOG("Failed to initialize GhostWriter (Keyboard): 0x%X", status);
        // You might want to return status here if keyboard is critical
    }

    InitControllerLock();
    KLOG("[V-INIT] Driver loaded. Controller injection ready.");

    // Find xinputhid or hidusb to scan
    UNICODE_STRING drvName;
    RtlInitUnicodeString(&drvName, L"\\Driver\\xinputhid");
    PDRIVER_OBJECT pDrvObj = NULL;

    SetFlag(device_object->Flags, DO_BUFFERED_IO);
    driver_object->MajorFunction[IRP_MJ_CREATE] = driver::create;
    driver_object->MajorFunction[IRP_MJ_CLOSE] = driver::close;
    driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = driver::device_control;
    ClearFlag(device_object->Flags, DO_DEVICE_INITIALIZING);

    // If kdmapper doesn't pass the base in args and wipes the MZ header, we can just use the address of our driver_main function 
    // as an anchor point inside our module. Any usermode tool just needs an address *inside* the module to do its math.
    ULONG_PTR rip_addr = (ULONG_PTR)&driver_main;
    ULONG_PTR base_guess = rip_addr & ~(PAGE_SIZE - 1);
    PVOID final_base = (PVOID)base_guess;

    for (int i = 0; i < 0x2000; i++) {
        if (!MmIsAddressValid((PVOID)base_guess)) {
            break;
        }
        final_base = (PVOID)base_guess; // The last valid page before hitting unmapped memory is likely our base
        base_guess -= PAGE_SIZE;
    }

    KLOG("Driver loaded! My internal function address is: %p | Guessed Base: %p", &driver_main, final_base);
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS DriverEntry(PVOID arg1, PVOID arg2) {
    extern PVOID g_arg1;
    extern PVOID g_arg2;
    g_arg1 = arg1;
    g_arg2 = arg2;

    UNICODE_STRING driver_name;
    RtlInitUnicodeString(&driver_name, L"\\Driver\\coochie");
    return IoCreateDriver(&driver_name, &driver_main);
}










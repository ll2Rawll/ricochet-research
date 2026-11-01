#pragma once
#include <ntddk.h>
#include <intrin.h>

//
// ─── IOCTLs (matches the km-side definitions) ────────────────────────────────
//
#define HV_DEVICE_TYPE   0x8000u
#define HV_IOCTL(n)      CTL_CODE(HV_DEVICE_TYPE, 0x800 + (n), METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_HV_READ_PHYS      HV_IOCTL(0)   // read  guest-physical memory
#define IOCTL_HV_WRITE_PHYS     HV_IOCTL(1)   // write guest-physical memory
#define IOCTL_HV_READ_VIRT      HV_IOCTL(2)   // read  guest-virtual  memory
#define IOCTL_HV_WRITE_VIRT     HV_IOCTL(3)   // write guest-virtual  memory
#define IOCTL_HV_TRANSLATE_VA   HV_IOCTL(4)   // GVA → GPA
#define IOCTL_HV_ADD_HOOK       HV_IOCTL(5)   // add  EPT/NPT shadow-page hook
#define IOCTL_HV_REM_HOOK       HV_IOCTL(6)   // remove EPT/NPT hook
#define IOCTL_HV_HIDE_PAGE      HV_IOCTL(7)   // hide a GPA from guest
#define IOCTL_HV_GET_CR3        HV_IOCTL(8)   // get guest CR3 of calling process

//
// ─── Shared request / response structures ────────────────────────────────────
//
#pragma pack(push, 1)

struct HV_PHYS_RW {
    UINT64 PhysicalAddress;
    UINT32 Size;
    UINT8  Buffer[1];                   // variable-length tail
};

struct HV_VIRT_RW {
    UINT64 VirtualAddress;
    UINT64 Cr3;
    UINT32 Size;
    UINT8  Buffer[1];
};

struct HV_TRANSLATE_REQ {
    UINT64 VirtualAddress;
    UINT64 Cr3;
};

struct HV_TRANSLATE_RES {
    UINT64 PhysicalAddress;
};

struct HV_HOOK_REQ {
    UINT64 TargetGpa;                   // page-aligned guest physical address
    UINT64 ShadowHpa;                   // page-aligned host physical of the shadow copy
};

#pragma pack(pop)

//
// ─── CPU vendor ──────────────────────────────────────────────────────────────
//
enum class CpuVendor : UINT8 {
    Unknown = 0,
    Intel   = 1,
    AMD     = 2,
};

//
// ─── General-purpose register save frame ─────────────────────────────────────
// Layout must match the push order in the ASM stubs.
//
#pragma pack(push, 8)
struct GpRegs {
    UINT64 R15, R14, R13, R12, R11, R10, R9, R8;
    UINT64 Rdi, Rsi, Rbp, Rbx, Rdx, Rcx, Rax;
    // RSP is NOT saved here – it comes from VMCS/VMCB
};
#pragma pack(pop)

//
// ─── Per-vCPU context ────────────────────────────────────────────────────────
//
#define HOST_STACK_PAGES    4           // 16 KB host stack per vCPU
#define HOST_STACK_SIZE     (HOST_STACK_PAGES * PAGE_SIZE)

struct VcpuContext {
    // VMX (Intel)
    UINT64          VmxonPhys;
    void*           VmxonVa;
    UINT64          VmcsPhys;
    void*           VmcsVa;

    // SVM (AMD)
    UINT64          VmcbPhys;
    void*           VmcbVa;
    UINT64          HostSaveAreaPhys;
    void*           HostSaveAreaVa;

    // Common
    UINT64          MsrBitmapPhys;
    void*           MsrBitmapVa;
    void*           HostStackBase;      // bottom (low address) of host stack
    UINT64          HostStackTop;       // RSP we load into VMCS/VMCB HOST_RSP

    UINT32          Index;              // logical processor index
    bool            Launched;
};

//
// ─── Global hypervisor state ─────────────────────────────────────────────────
//
#define MAX_VCPUS  64

struct HvGlobal {
    CpuVendor   Vendor;
    UINT32      CpuCount;
    VcpuContext Vcpus[MAX_VCPUS];
    bool        Active;

    // SLAT root (EPT PML4 / NPT PML4)
    UINT64      SlatRootPhys;
    void*       SlatRootVa;

    // AMD only: hook-nested-CR3 root
    UINT64      HookNcrPhys;
    void*       HookNcrVa;
};

extern HvGlobal g_Hv;

//
// ─── Logging helper ──────────────────────────────────────────────────────────
//
#define HV_LOG(fmt, ...)  DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, \
                                     "[HV] " fmt "\n", ##__VA_ARGS__)

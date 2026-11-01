#include <ntddk.h>
#include <intrin.h>

#include "src/hv.h"
#include "src/cpu/cpu.h"
#include "src/common/memory.h"
#include "src/ept/ept.h"
#include "src/npt/npt.h"
#include "src/vmx/vmx.h"
#include "src/vmx/exit_handler.h"
#include "src/svm/svm.h"
#include "src/svm/exit_handler.h"
#include "src/hv_hook/hv_hook.h"   // hyperjack mode

// ─────────────────────────────────────────────────────────────────────────────
// Global hypervisor state
// ─────────────────────────────────────────────────────────────────────────────
HvGlobal g_Hv = {};

#define DEVICE_NAME  L"\\Device\\LKHypervisor"
#define SYMLINK_NAME L"\\DosDevices\\LKHypervisor"

// ─────────────────────────────────────────────────────────────────────────────
// DPC callback: runs hypervisor init on a single logical processor
// ─────────────────────────────────────────────────────────────────────────────
struct LaunchCtx {
    LONG   SuccessCount;
    LONG   FailCount;
    KEVENT Done;
};

static void LaunchVcpuDpc(KDPC* Dpc, void* Ctx, void* SystemArg1, void* SystemArg2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArg1);
    UNREFERENCED_PARAMETER(SystemArg2);

    auto* ctx = static_cast<LaunchCtx*>(Ctx);
    const UINT32 idx = KeGetCurrentProcessorNumberEx(nullptr);
    VcpuContext* vcpu = &g_Hv.Vcpus[idx];
    vcpu->Index = idx;

    NTSTATUS st = STATUS_UNSUCCESSFUL;

    if (g_Hv.Vendor == CpuVendor::Intel) {
        st = Vmx::Initialize(vcpu);
        if (NT_SUCCESS(st))
            st = Vmx::LaunchVcpu(vcpu);
    } else if (g_Hv.Vendor == CpuVendor::AMD) {
        st = Svm::Initialize(vcpu);
        if (NT_SUCCESS(st))
            st = Svm::LaunchVcpu(vcpu);
    }

    if (NT_SUCCESS(st)) {
        InterlockedIncrement(&ctx->SuccessCount);
        HV_LOG("vCPU %u launched", idx);
    } else {
        InterlockedIncrement(&ctx->FailCount);
        HV_LOG("vCPU %u FAILED (0x%X)", idx, st);
    }

    // Signal completion when all CPUs have checked in
    if ((ULONG)(ctx->SuccessCount + ctx->FailCount) == g_Hv.CpuCount)
        KeSetEvent(&ctx->Done, IO_NO_INCREMENT, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown DPC: VMXOFF / SVM disable on each CPU
// ─────────────────────────────────────────────────────────────────────────────
static LONG g_ShutdownDone = 0;
static KEVENT g_ShutdownEvent;

static void ShutdownVcpuDpc(KDPC* Dpc, void* Ctx, void* A1, void* A2)
{
    UNREFERENCED_PARAMETER(Dpc); UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(A1);  UNREFERENCED_PARAMETER(A2);

    const UINT32 idx = KeGetCurrentProcessorNumberEx(nullptr);
    VcpuContext* vcpu = &g_Hv.Vcpus[idx];

    if (g_Hv.Vendor == CpuVendor::Intel)
        Vmx::Shutdown(vcpu);
    else if (g_Hv.Vendor == CpuVendor::AMD)
        Svm::Shutdown(vcpu);

    if ((ULONG)InterlockedIncrement(&g_ShutdownDone) == g_Hv.CpuCount)
        KeSetEvent(&g_ShutdownEvent, IO_NO_INCREMENT, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize hypervisor on all CPUs
// ─────────────────────────────────────────────────────────────────────────────
static NTSTATUS HvInitialize()
{
    g_Hv.Vendor   = Cpu::DetectVendor();
    g_Hv.CpuCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    HV_LOG("Vendor=%s  CPUs=%u",
           g_Hv.Vendor == CpuVendor::Intel ? "Intel" :
           g_Hv.Vendor == CpuVendor::AMD   ? "AMD"   : "Unknown",
           g_Hv.CpuCount);

    // Capability check
    if (g_Hv.Vendor == CpuVendor::Intel) {
        if (!Cpu::VmxSupported()) { HV_LOG("VMX not supported"); return STATUS_NOT_SUPPORTED; }
    } else if (g_Hv.Vendor == CpuVendor::AMD) {
        if (!Cpu::SvmSupported()) { HV_LOG("SVM not supported"); return STATUS_NOT_SUPPORTED; }
    } else {
        HV_LOG("Unknown CPU vendor"); return STATUS_NOT_SUPPORTED;
    }

    // ── Attempt hyperjack mode first ──────────────────────────────────────────
    // If Hyper-V is running (e.g. WSL2, VBS, or explicit Hyper-V enabled) we
    // parasitize hvix64's exit handler instead of launching our own hypervisor.
    // This survives HVCI-off Win11 systems and avoids nested VMX depth limits.
    {
        NTSTATUS hjSt = HvHook::Initialize();
        if (NT_SUCCESS(hjSt)) {
            // Hyperjack active.  EPT tables are NOT set up in this path
            // (we're operating on Hyper-V's EPT, not our own).  The device
            // IOCTL still works for physical read/write/translate.
            g_Hv.Active = true;
            HV_LOG("[HJ] Running in HYPERJACK mode — hvix64.sys parasitized");
            return STATUS_SUCCESS;
        }
        HV_LOG("[HJ] Hyperjack unavailable (0x%X) — falling back to standalone", hjSt);
    }

    // ── Standalone mode: build our own SLAT and launch a hypervisor ───────────
    NTSTATUS st;
    if (g_Hv.Vendor == CpuVendor::Intel) {
        st = Ept::Initialize(&g_Hv.SlatRootPhys, &g_Hv.SlatRootVa);
    } else {
        st = Npt::Initialize(&g_Hv.SlatRootPhys, &g_Hv.SlatRootVa,
                             &g_Hv.HookNcrPhys,  &g_Hv.HookNcrVa);
    }
    if (!NT_SUCCESS(st)) { HV_LOG("SLAT init failed 0x%X", st); return st; }

    // Dispatch DPC to every logical processor to launch the hypervisor
    LaunchCtx ctx = {};
    KeInitializeEvent(&ctx.Done, NotificationEvent, FALSE);

    auto* dpcs = static_cast<KDPC*>(ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                     sizeof(KDPC) * g_Hv.CpuCount, 'pDvH'));
    if (!dpcs) return STATUS_INSUFFICIENT_RESOURCES;

    for (UINT32 i = 0; i < g_Hv.CpuCount; ++i) {
        KeInitializeDpc(&dpcs[i], LaunchVcpuDpc, &ctx);
        KeSetTargetProcessorDpcEx(&dpcs[i], reinterpret_cast<PROCESSOR_NUMBER*>(&i));
        KeInsertQueueDpc(&dpcs[i], nullptr, nullptr);
    }

    LARGE_INTEGER timeout;
    timeout.QuadPart = -100000000LL;   // 10 seconds
    KeWaitForSingleObject(&ctx.Done, Executive, KernelMode, FALSE, &timeout);
    ExFreePoolWithTag(dpcs, 'pDvH');

    if (ctx.FailCount > 0) {
        HV_LOG("Launch partial failure: %d/%d failed", ctx.FailCount, g_Hv.CpuCount);
        return STATUS_UNSUCCESSFUL;
    }

    g_Hv.Active = true;
    HV_LOG("Standalone hypervisor active on all %u CPUs", g_Hv.CpuCount);
    return STATUS_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tear down the hypervisor
// ─────────────────────────────────────────────────────────────────────────────
static void HvShutdown()
{
    if (!g_Hv.Active) return;

    // ── Hyperjack mode teardown ───────────────────────────────────────────────
    if (HvHook::Active) {
        HvHook::Shutdown();
        g_Hv.Active = false;
        HV_LOG("Hyperjack unloaded");
        return;
    }

    // ── Standalone mode teardown ──────────────────────────────────────────────
    g_ShutdownDone = 0;
    KeInitializeEvent(&g_ShutdownEvent, NotificationEvent, FALSE);

    auto* dpcs = static_cast<KDPC*>(ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                     sizeof(KDPC) * g_Hv.CpuCount, 'pDvH'));
    if (dpcs) {
        for (UINT32 i = 0; i < g_Hv.CpuCount; ++i) {
            KeInitializeDpc(&dpcs[i], ShutdownVcpuDpc, nullptr);
            KeSetTargetProcessorDpcEx(&dpcs[i], reinterpret_cast<PROCESSOR_NUMBER*>(&i));
            KeInsertQueueDpc(&dpcs[i], nullptr, nullptr);
        }
        LARGE_INTEGER timeout;
        timeout.QuadPart = -50000000LL;  // 5 s
        KeWaitForSingleObject(&g_ShutdownEvent, Executive, KernelMode, FALSE, &timeout);
        ExFreePoolWithTag(dpcs, 'pDvH');
    }

    if (g_Hv.Vendor == CpuVendor::Intel) {
        Ept::Destroy(g_Hv.SlatRootPhys, g_Hv.SlatRootVa);
    } else {
        Npt::Destroy(g_Hv.SlatRootVa, g_Hv.HookNcrVa);
    }

    g_Hv.Active = false;
    HV_LOG("Standalone hypervisor shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// IOCTL dispatch
// ─────────────────────────────────────────────────────────────────────────────
static NTSTATUS HandleIoctl(PIRP Irp, PIO_STACK_LOCATION Stack)
{
    const ULONG code = Stack->Parameters.DeviceIoControl.IoControlCode;
    void* buf        = Irp->AssociatedIrp.SystemBuffer;
    const ULONG inSz = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outSz      = 0;
    NTSTATUS st      = STATUS_SUCCESS;

    if (!g_Hv.Active && code != IOCTL_HV_GET_CR3)
        return STATUS_DEVICE_NOT_READY;

    switch (code)
    {
    case IOCTL_HV_READ_PHYS:
    {
        if (inSz < sizeof(HV_PHYS_RW)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        auto* req = static_cast<HV_PHYS_RW*>(buf);
        if (inSz < FIELD_OFFSET(HV_PHYS_RW, Buffer) + req->Size) { st = STATUS_INVALID_PARAMETER; break; }
        void* mapped = Memory::MapPhysical(req->PhysicalAddress, req->Size);
        if (!mapped) { st = STATUS_ACCESS_VIOLATION; break; }
        RtlCopyMemory(req->Buffer, mapped, req->Size);
        Memory::UnmapPhysical(mapped, req->Size);
        outSz = inSz;
        break;
    }

    case IOCTL_HV_WRITE_PHYS:
    {
        if (inSz < sizeof(HV_PHYS_RW)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        auto* req = static_cast<HV_PHYS_RW*>(buf);
        void* mapped = Memory::MapPhysical(req->PhysicalAddress, req->Size);
        if (!mapped) { st = STATUS_ACCESS_VIOLATION; break; }
        RtlCopyMemory(mapped, req->Buffer, req->Size);
        Memory::UnmapPhysical(mapped, req->Size);
        break;
    }

    case IOCTL_HV_READ_VIRT:
    {
        if (inSz < sizeof(HV_VIRT_RW)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        auto* req = static_cast<HV_VIRT_RW*>(buf);
        const UINT64 gpa = Memory::WalkPageTable(req->Cr3, req->VirtualAddress);
        if (!gpa) { st = STATUS_ACCESS_VIOLATION; break; }
        void* mapped = Memory::MapPhysical(gpa, req->Size);
        if (!mapped) { st = STATUS_ACCESS_VIOLATION; break; }
        RtlCopyMemory(req->Buffer, mapped, req->Size);
        Memory::UnmapPhysical(mapped, req->Size);
        outSz = inSz;
        break;
    }

    case IOCTL_HV_WRITE_VIRT:
    {
        if (inSz < sizeof(HV_VIRT_RW)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        auto* req = static_cast<HV_VIRT_RW*>(buf);
        const UINT64 gpa = Memory::WalkPageTable(req->Cr3, req->VirtualAddress);
        if (!gpa) { st = STATUS_ACCESS_VIOLATION; break; }
        void* mapped = Memory::MapPhysical(gpa, req->Size);
        if (!mapped) { st = STATUS_ACCESS_VIOLATION; break; }
        RtlCopyMemory(mapped, req->Buffer, req->Size);
        Memory::UnmapPhysical(mapped, req->Size);
        break;
    }

    case IOCTL_HV_TRANSLATE_VA:
    {
        if (inSz < sizeof(HV_TRANSLATE_REQ) ||
            Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(HV_TRANSLATE_RES)) {
            st = STATUS_BUFFER_TOO_SMALL; break;
        }
        auto* req = static_cast<HV_TRANSLATE_REQ*>(buf);
        auto* res = static_cast<HV_TRANSLATE_RES*>(buf);
        res->PhysicalAddress = Memory::WalkPageTable(req->Cr3, req->VirtualAddress);
        outSz = sizeof(HV_TRANSLATE_RES);
        break;
    }

    case IOCTL_HV_ADD_HOOK:
    {
        if (inSz < sizeof(HV_HOOK_REQ)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        auto* req = static_cast<HV_HOOK_REQ*>(buf);
        if (g_Hv.Vendor == CpuVendor::Intel)
            st = Ept::AddHook(g_Hv.SlatRootVa, req->TargetGpa, req->ShadowHpa);
        else
            st = Npt::AddHook(g_Hv.SlatRootVa, g_Hv.HookNcrVa, req->TargetGpa, req->ShadowHpa);
        break;
    }

    case IOCTL_HV_REM_HOOK:
    {
        if (inSz < sizeof(HV_HOOK_REQ)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        auto* req = static_cast<HV_HOOK_REQ*>(buf);
        if (g_Hv.Vendor == CpuVendor::Intel)
            st = Ept::RemoveHook(g_Hv.SlatRootVa, req->TargetGpa);
        else
            st = Npt::RemoveHook(g_Hv.SlatRootVa, g_Hv.HookNcrVa, req->TargetGpa);
        break;
    }

    case IOCTL_HV_HIDE_PAGE:
    {
        if (inSz < sizeof(UINT64)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        const UINT64 gpa = *static_cast<UINT64*>(buf);
        if (g_Hv.Vendor == CpuVendor::Intel)
            st = Ept::HidePage(g_Hv.SlatRootVa, gpa);
        else
            st = Npt::HidePage(g_Hv.SlatRootVa, g_Hv.HookNcrVa, gpa);
        break;
    }

    case IOCTL_HV_GET_CR3:
    {
        if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(UINT64)) {
            st = STATUS_BUFFER_TOO_SMALL; break;
        }
        *static_cast<UINT64*>(buf) = __readcr3();
        outSz = sizeof(UINT64);
        break;
    }

    default:
        st = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status      = st;
    Irp->IoStatus.Information = outSz;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return st;
}

// ─────────────────────────────────────────────────────────────────────────────
// IRP handlers
// ─────────────────────────────────────────────────────────────────────────────
static NTSTATUS CreateClose(PDEVICE_OBJECT, PIRP Irp)
{
    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS DeviceControl(PDEVICE_OBJECT, PIRP Irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    return HandleIoctl(Irp, stack);
}

// ─────────────────────────────────────────────────────────────────────────────
// Unload
// ─────────────────────────────────────────────────────────────────────────────
static void DriverUnload(PDRIVER_OBJECT DriverObject)
{
    HvShutdown();

    UNICODE_STRING sym;
    RtlInitUnicodeString(&sym, SYMLINK_NAME);
    IoDeleteSymbolicLink(&sym);

    if (DriverObject->DeviceObject)
        IoDeleteDevice(DriverObject->DeviceObject);

    HV_LOG("Driver unloaded");
}

// ─────────────────────────────────────────────────────────────────────────────
// DriverEntry
// ─────────────────────────────────────────────────────────────────────────────
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    HV_LOG("Loading — build " __DATE__ " " __TIME__);

    // Create device
    UNICODE_STRING devName, symName;
    RtlInitUnicodeString(&devName, DEVICE_NAME);
    RtlInitUnicodeString(&symName, SYMLINK_NAME);

    PDEVICE_OBJECT devObj = nullptr;
    NTSTATUS st = IoCreateDevice(DriverObject, 0, &devName,
                                  FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
                                  FALSE, &devObj);
    if (!NT_SUCCESS(st)) { HV_LOG("IoCreateDevice failed 0x%X", st); return st; }

    st = IoCreateSymbolicLink(&symName, &devName);
    if (!NT_SUCCESS(st)) {
        IoDeleteDevice(devObj);
        HV_LOG("IoCreateSymbolicLink failed 0x%X", st);
        return st;
    }

    devObj->Flags |= DO_BUFFERED_IO;

    DriverObject->MajorFunction[IRP_MJ_CREATE]         = CreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = CreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;
    DriverObject->DriverUnload                          = DriverUnload;

    // Launch hypervisor
    st = HvInitialize();
    if (!NT_SUCCESS(st)) {
        HV_LOG("HvInitialize failed 0x%X — driver remains loaded (device available)", st);
        // Don't fail DriverEntry — let the device be opened for diagnostics
    }

    return STATUS_SUCCESS;
}

/* SPDX-License-Identifier: MIT
 * Original, telemetry-only WDM reference. No proprietary RICOCHET code.
 * Windows/WDK compilation and kernel runtime validation are still required.
 */
#include <ntifs.h> /* Includes the IoGetRequestorProcess contract. */
#include <wdmsec.h>
#include "protocol.h"

/* Unique device security-class GUID; not a Microsoft or game vendor class. */
static const GUID RL_DEVICE_CLASS = {
    0xc5b45541, 0x6a11, 0x4e28,
    {0xa6, 0xc8, 0x74, 0x73, 0x6d, 0x2f, 0xe2, 0x53}
};

typedef struct RL_STATE {
    KSPIN_LOCK lock;
    PFILE_OBJECT owner_file;
    PEPROCESS owner_process; /* Referenced from CREATE until CLOSE. */
    HANDLE owner_pid;
    BOOLEAN active;
    BOOLEAN cleaned_up;
    BOOLEAN unloading;
    BOOLEAN process_registered;
    BOOLEAN thread_registered;
    BOOLEAN image_registered;
    ULONG head;
    ULONG count;
    uint64_t session_id;
    uint64_t next_session_id;
    uint64_t next_sequence;
    uint64_t dropped_events;
    RL_EVENT ring[RL_RING_CAPACITY];
} RL_STATE;

static PDEVICE_OBJECT g_device;
static UNICODE_STRING g_link = RTL_CONSTANT_STRING(L"\\DosDevices\\RicochetReference");

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD RlUnload;
DRIVER_DISPATCH RlCreate;
DRIVER_DISPATCH RlClose;
DRIVER_DISPATCH RlCleanup;
DRIVER_DISPATCH RlControl;
DRIVER_DISPATCH RlUnsupported;

static NTSTATUS RlComplete(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

/* Called with state->lock held. Contains no blocking/allocating operations. */
static VOID RlEmitLocked(
    RL_STATE *state, uint32_t kind, HANDLE process_id, HANDLE thread_id,
    HANDLE related_process_id, SIZE_T image_size)
{
    RL_EVENT event;
    LARGE_INTEGER now;
    ULONG tail;

    if (!state->active || state->unloading) return;
    if (state->next_sequence == UINT64_MAX) {
        state->active = FALSE; /* Never wrap a sequence number. */
        return;
    }
    RtlZeroMemory(&event, sizeof(event));
    KeQuerySystemTimePrecise(&now);
    event.sequence = state->next_sequence++;
    event.time_100ns = (uint64_t)now.QuadPart;
    event.process_id = (uint64_t)(ULONG_PTR)process_id;
    event.thread_id = (uint64_t)(ULONG_PTR)thread_id;
    event.related_process_id = (uint64_t)(ULONG_PTR)related_process_id;
    event.image_size = (uint64_t)image_size;
    event.kind = kind;

    if (state->count == RL_RING_CAPACITY) {
        if (state->dropped_events != UINT64_MAX) ++state->dropped_events;
        return; /* Drop newest; next_sequence exposes the discontinuity. */
    }
    tail = (state->head + state->count) % RL_RING_CAPACITY;
    state->ring[tail] = event;
    ++state->count;
}

static VOID RlProcessNotify(
    PEPROCESS process, HANDLE process_id, PPS_CREATE_NOTIFY_INFO create_info)
{
    RL_STATE *state = (RL_STATE *)g_device->DeviceExtension;
    HANDLE parent_id = create_info != NULL ? create_info->ParentProcessId : NULL;
    KIRQL old_irql;
    KeAcquireSpinLock(&state->lock, &old_irql);
    if (state->active) {
        if (create_info != NULL && parent_id == state->owner_pid) {
            RlEmitLocked(state, RL_EVENT_PROCESS_CREATE, process_id, NULL,
                         parent_id, 0);
        } else if (create_info == NULL && process == state->owner_process) {
            RlEmitLocked(state, RL_EVENT_PROCESS_EXIT, process_id, NULL, NULL, 0);
            state->active = FALSE;
        }
    }
    KeReleaseSpinLock(&state->lock, old_irql);
    /* Never inspect/alter CreateInfo->CreationStatus or any process memory. */
}

static VOID RlThreadNotify(HANDLE process_id, HANDLE thread_id, BOOLEAN create)
{
    RL_STATE *state = (RL_STATE *)g_device->DeviceExtension;
    KIRQL old_irql;
    KeAcquireSpinLock(&state->lock, &old_irql);
    if (process_id == state->owner_pid) {
        RlEmitLocked(state, create ? RL_EVENT_THREAD_CREATE : RL_EVENT_THREAD_EXIT,
                     process_id, thread_id, NULL, 0);
    }
    KeReleaseSpinLock(&state->lock, old_irql);
}

static VOID RlImageNotify(PUNICODE_STRING image_name, HANDLE process_id, PIMAGE_INFO info)
{
    RL_STATE *state = (RL_STATE *)g_device->DeviceExtension;
    SIZE_T image_size = info->ImageSize;
    KIRQL old_irql;
    UNREFERENCED_PARAMETER(image_name); /* No paths, hashes or bases collected. */
    KeAcquireSpinLock(&state->lock, &old_irql);
    if (process_id != NULL && process_id == state->owner_pid) {
        RlEmitLocked(state, RL_EVENT_IMAGE_LOAD, process_id, NULL, NULL, image_size);
    }
    KeReleaseSpinLock(&state->lock, old_irql);
}

NTSTATUS RlCreate(PDEVICE_OBJECT device, PIRP irp)
{
    RL_STATE *state = (RL_STATE *)device->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PEPROCESS process = IoGetRequestorProcess(irp);
    KIRQL old_irql;
    NTSTATUS status = STATUS_SUCCESS;
    if (irp->RequestorMode != UserMode || process == NULL ||
        stack->FileObject->FileName.Length != 0) {
        return RlComplete(irp, STATUS_ACCESS_DENIED, 0);
    }
    ObReferenceObject(process);
    KeAcquireSpinLock(&state->lock, &old_irql);
    if (state->unloading) status = STATUS_DELETE_PENDING;
    else if (state->owner_file != NULL) status = STATUS_SHARING_VIOLATION;
    else {
        state->owner_file = stack->FileObject;
        state->owner_process = process;
        state->owner_pid = PsGetProcessId(process);
        state->cleaned_up = FALSE;
        state->active = FALSE;
        state->session_id = 0;
        state->head = state->count = 0;
        state->dropped_events = 0;
        state->next_sequence = 0;
    }
    KeReleaseSpinLock(&state->lock, old_irql);
    if (!NT_SUCCESS(status)) ObDereferenceObject(process);
    return RlComplete(irp, status, 0);
}

NTSTATUS RlCleanup(PDEVICE_OBJECT device, PIRP irp)
{
    RL_STATE *state = (RL_STATE *)device->DeviceExtension;
    KIRQL old_irql;
    KeAcquireSpinLock(&state->lock, &old_irql);
    if (state->owner_file == IoGetCurrentIrpStackLocation(irp)->FileObject) {
        state->cleaned_up = TRUE;
        state->active = FALSE;
        state->head = state->count = 0;
        RtlZeroMemory(state->ring, sizeof(state->ring));
    }
    KeReleaseSpinLock(&state->lock, old_irql);
    return RlComplete(irp, STATUS_SUCCESS, 0);
}

NTSTATUS RlClose(PDEVICE_OBJECT device, PIRP irp)
{
    RL_STATE *state = (RL_STATE *)device->DeviceExtension;
    PEPROCESS process = NULL;
    KIRQL old_irql;
    KeAcquireSpinLock(&state->lock, &old_irql);
    if (state->owner_file == IoGetCurrentIrpStackLocation(irp)->FileObject) {
        state->active = FALSE;
        state->head = state->count = 0;
        process = state->owner_process;
        state->owner_process = NULL;
        state->owner_pid = NULL;
        state->owner_file = NULL;
    }
    KeReleaseSpinLock(&state->lock, old_irql);
    if (process != NULL) ObDereferenceObject(process);
    return RlComplete(irp, STATUS_SUCCESS, 0);
}

NTSTATUS RlControl(PDEVICE_OBJECT device, PIRP irp)
{
    RL_STATE *state = (RL_STATE *)device->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG in_len = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID buffer = irp->AssociatedIrp.SystemBuffer;
    RL_REQUEST request;
    RL_READ_REQUEST read_request;
    KIRQL old_irql;
    ULONG_PTR information = 0;
    NTSTATUS status = STATUS_SUCCESS;
    uint32_t max_events = 0;

    /* Dispatch on all 32 bits, before touching any supplied buffer. */
    switch (code) {
    case RL_IOCTL_START:
        if (in_len != sizeof(RL_REQUEST) || out_len != sizeof(RL_SESSION))
            return RlComplete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        break;
    case RL_IOCTL_STOP:
        if (in_len != sizeof(RL_REQUEST) || out_len != 0)
            return RlComplete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        break;
    case RL_IOCTL_READ:
        if (in_len != sizeof(RL_READ_REQUEST) || out_len != sizeof(RL_BATCH))
            return RlComplete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        break;
    default:
        return RlComplete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
    if (buffer == NULL || irp->RequestorMode != UserMode)
        return RlComplete(irp, STATUS_INVALID_PARAMETER, 0);

    /* METHOD_BUFFERED aliases input/output. Copy input before clearing output. */
    RtlCopyMemory(&request, buffer, sizeof(request));
    if (request.version != RL_PROTOCOL_VERSION || request.size != in_len)
        return RlComplete(irp, STATUS_REVISION_MISMATCH, 0);
    if (code == RL_IOCTL_READ) {
        RtlCopyMemory(&read_request, buffer, sizeof(read_request));
        if (read_request.reserved != 0 || read_request.max_events == 0 ||
            read_request.max_events > RL_MAX_BATCH_EVENTS)
            return RlComplete(irp, STATUS_INVALID_PARAMETER, 0);
        max_events = read_request.max_events;
    }
    if (out_len != 0) RtlZeroMemory(buffer, out_len);

    KeAcquireSpinLock(&state->lock, &old_irql);
    if (state->unloading) status = STATUS_DELETE_PENDING;
    else if (state->cleaned_up) status = STATUS_FILE_CLOSED;
    else if (state->owner_file != stack->FileObject ||
             state->owner_process != IoGetRequestorProcess(irp))
        status = STATUS_ACCESS_DENIED;
    else if (code == RL_IOCTL_START) {
        if (state->active) status = STATUS_DEVICE_BUSY;
        else if (state->next_session_id == UINT64_MAX) status = STATUS_INTEGER_OVERFLOW;
        else {
            RL_SESSION *session = (RL_SESSION *)buffer;
            state->head = state->count = 0;
            state->dropped_events = 0;
            state->next_sequence = 1;
            state->session_id = ++state->next_session_id;
            state->active = TRUE;
            RlEmitLocked(state, RL_EVENT_SESSION_START, state->owner_pid, NULL, NULL, 0);
            session->version = RL_PROTOCOL_VERSION;
            session->size = sizeof(*session);
            session->session_id = state->session_id;
            session->target_process_id = (uint64_t)(ULONG_PTR)state->owner_pid;
            information = sizeof(*session);
        }
    } else if (code == RL_IOCTL_STOP) {
        state->active = FALSE;
        state->head = state->count = 0;
    } else {
        RL_BATCH *batch = (RL_BATCH *)buffer;
        ULONG index;
        if (state->session_id == 0) status = STATUS_INVALID_DEVICE_STATE;
        else {
            batch->version = RL_PROTOCOL_VERSION;
            batch->session_id = state->session_id;
            batch->dropped_events = state->dropped_events;
            batch->next_sequence = state->next_sequence;
            batch->event_count = state->count < max_events ? state->count : max_events;
            for (index = 0; index < batch->event_count; ++index) {
                batch->events[index] = state->ring[state->head];
                RtlZeroMemory(&state->ring[state->head], sizeof(RL_EVENT));
                state->head = (state->head + 1) % RL_RING_CAPACITY;
                --state->count;
            }
            batch->size = RL_BATCH_BYTES(batch->event_count);
            information = batch->size;
        }
    }
    KeReleaseSpinLock(&state->lock, old_irql);
    return RlComplete(irp, status, information);
}

NTSTATUS RlUnsupported(PDEVICE_OBJECT device, PIRP irp)
{
    UNREFERENCED_PARAMETER(device);
    return RlComplete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
}

/* Removal APIs wait for in-flight callbacks. Never call from a callback. */
static VOID RlUnregister(RL_STATE *state)
{
    NTSTATUS status;
    if (state->image_registered) {
        status = PsRemoveLoadImageNotifyRoutine(RlImageNotify);
        NT_ASSERT(NT_SUCCESS(status));
        UNREFERENCED_PARAMETER(status);
        state->image_registered = FALSE;
    }
    if (state->thread_registered) {
        status = PsRemoveCreateThreadNotifyRoutine(RlThreadNotify);
        NT_ASSERT(NT_SUCCESS(status));
        UNREFERENCED_PARAMETER(status);
        state->thread_registered = FALSE;
    }
    if (state->process_registered) {
        status = PsSetCreateProcessNotifyRoutineEx(RlProcessNotify, TRUE);
        NT_ASSERT(NT_SUCCESS(status));
        UNREFERENCED_PARAMETER(status);
        state->process_registered = FALSE;
    }
}

VOID RlUnload(PDRIVER_OBJECT driver)
{
    RL_STATE *state = (RL_STATE *)driver->DeviceObject->DeviceExtension;
    KIRQL old_irql;
    KeAcquireSpinLock(&state->lock, &old_irql);
    state->unloading = TRUE;
    state->active = FALSE;
    KeReleaseSpinLock(&state->lock, old_irql);
    IoDeleteSymbolicLink(&g_link);
    RlUnregister(state);
    /* The I/O manager does not unload a legacy driver with open file objects. */
    NT_ASSERT(state->owner_file == NULL && state->owner_process == NULL);
    RtlZeroMemory(state->ring, sizeof(state->ring));
    IoDeleteDevice(driver->DeviceObject);
    g_device = NULL;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry_path)
{
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Device\\RicochetReference");
    UNICODE_STRING sddl = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    RL_STATE *state;
    NTSTATUS status;
    ULONG index;
    UNREFERENCED_PARAMETER(registry_path);

    for (index = 0; index <= IRP_MJ_MAXIMUM_FUNCTION; ++index)
        driver->MajorFunction[index] = RlUnsupported;
    driver->MajorFunction[IRP_MJ_CREATE] = RlCreate;
    driver->MajorFunction[IRP_MJ_CLEANUP] = RlCleanup;
    driver->MajorFunction[IRP_MJ_CLOSE] = RlClose;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = RlControl;
    driver->DriverUnload = RlUnload;

    status = IoCreateDeviceSecure(driver, sizeof(RL_STATE), &name, RL_DEVICE_TYPE,
                                  FILE_DEVICE_SECURE_OPEN, TRUE, &sddl,
                                  &RL_DEVICE_CLASS, &g_device);
    if (!NT_SUCCESS(status)) return status;
    state = (RL_STATE *)g_device->DeviceExtension;
    RtlZeroMemory(state, sizeof(*state));
    KeInitializeSpinLock(&state->lock);

    /* The project links with /INTEGRITYCHECK (FORCE_INTEGRITY). Registration
     * can return STATUS_ACCESS_DENIED without that image characteristic.
     * This flag does not replace supported Windows driver signing/deployment. */
    status = PsSetCreateProcessNotifyRoutineEx(RlProcessNotify, FALSE);
    if (!NT_SUCCESS(status)) goto fail;
    state->process_registered = TRUE;
    status = PsSetCreateThreadNotifyRoutine(RlThreadNotify);
    if (!NT_SUCCESS(status)) goto fail;
    state->thread_registered = TRUE;
    status = PsSetLoadImageNotifyRoutine(RlImageNotify);
    if (!NT_SUCCESS(status)) goto fail;
    state->image_registered = TRUE;
    status = IoCreateSymbolicLink(&g_link, &name);
    if (!NT_SUCCESS(status)) goto fail;
    g_device->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;

fail:
    RlUnregister(state);
    IoDeleteDevice(g_device);
    g_device = NULL;
    return status;
}

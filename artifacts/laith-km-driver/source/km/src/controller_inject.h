/*===========================================================================
 *  controller_inject.h  —  Drop into your km.sys kernel driver project
 *
 *  Implements USB URB hooking by targeting the lower device object (USB PDO)
 *  of \Driver\xboxgip. This intercepts the raw controller data before xboxgip
 *  processes it and pushes it to shared memory (which XInput/Gamepad API uses).
 *
 *  IOCTL: CTL_CODE(FILE_DEVICE_UNKNOWN, 0x951, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
 *===========================================================================*/

#pragma once
#include <ntddk.h>
#include <wdm.h>
#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>

//===========================================================================
// IOCTL code (must match usermode controller.cpp)
//===========================================================================
#define IOCTL_CONTROLLER_EVENT      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x951, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_ENUMERATE_CONTROLLERS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x952, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_SELECT_CONTROLLER     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x953, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

//===========================================================================
// Usermode -> Kernel request packet
//===========================================================================
#pragma pack(push, 1)
typedef struct _CTRL_REQUEST {
    SHORT  thumbLX;         // Left stick X [-32767, 32767]
    SHORT  thumbLY;         // Left stick Y [-32767, 32767]
    SHORT  thumbRX;         // Right stick X [-32767, 32767]
    SHORT  thumbRY;         // Right stick Y [-32767, 32767] (positive = up)
    BYTE   leftTrigger;     // L2/LT [0, 255]
    BYTE   rightTrigger;    // R2/RT [0, 255]
    USHORT buttons;         // Button bitmask (optional, 0 = no buttons)
} CTRL_REQUEST, *PCTRL_REQUEST;

typedef struct _SELECT_REQUEST {
    USHORT Vid;
    USHORT Pid;
    ULONG  Type; // 1 = Xbox360, 2 = XboxOne, 3 = DS4, 4 = DualSense
} SELECT_REQUEST, *PSELECT_REQUEST;

// Minimal XUSB report structure for validation
typedef struct _XUSB_REPORT {
    BYTE   Type;
    BYTE   Size;
    USHORT Buttons;
    BYTE   LeftTrigger;
    BYTE   RightTrigger;
    SHORT  ThumbLX;
    SHORT  ThumbLY;
    SHORT  ThumbRX;
    SHORT  ThumbRY;
    BYTE   Reserved[6];
} XUSB_REPORT;

// Xbox One / Elite report structure
typedef struct _XBOXONE_REPORT {
    BYTE   Type;          // 0x20
    BYTE   Unknown1;      
    BYTE   Sequence;      
    BYTE   Size;          
    BYTE   Buttons1;      // Sync, Menu, View, A, B, X, Y
    BYTE   Buttons2;      // Dpad, LB, RB, L3, R3
    USHORT LeftTrigger;   // 0-1023
    USHORT RightTrigger;  // 0-1023
    SHORT  ThumbLX;
    SHORT  ThumbLY;
    SHORT  ThumbRX;
    SHORT  ThumbRY;
} XBOXONE_REPORT;
#pragma pack(pop)

//===========================================================================
// Global State
//===========================================================================
static PDRIVER_DISPATCH g_OriginalInternalDeviceControl = nullptr;
static PDRIVER_OBJECT   g_HookedUsbDriver = nullptr;
static PDEVICE_OBJECT   g_TargetUsbPdo    = nullptr;
static BOOLEAN          g_Initialized     = FALSE;
static KMUTEX           g_InitMutex;

// Global injected state from usermode cheat
static CTRL_REQUEST g_InjectedState = {0};
static KSPIN_LOCK   g_StateLock;

// Context passed to completion routines
typedef struct _HOOK_CONTEXT {
    PIO_COMPLETION_ROUTINE OriginalCompletion;
    PVOID                  OriginalContext;
    UCHAR                  OriginalControl;
    PURB                   Urb;
} HOOK_CONTEXT, *PHOOK_CONTEXT;

//===========================================================================
// Generic Completion Routine
// Overwrites the thumbsticks/triggers in the URB
//===========================================================================
static NTSTATUS Hooked_CompletionRoutine(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context
) {
    PHOOK_CONTEXT hookCtx = (PHOOK_CONTEXT)Context;

    if (Irp->PendingReturned) {
        IoMarkIrpPending(Irp);
    }

    if (NT_SUCCESS(Irp->IoStatus.Status)) {
        PURB urb = hookCtx->Urb;
        if (urb && urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER) {
            if (urb->UrbBulkOrInterruptTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN) {
                if (urb->UrbBulkOrInterruptTransfer.TransferBufferLength >= 18) {
                    
                    // Prioritize MDL over TransferBuffer for DMA consistency
                    PUCHAR buffer = nullptr;
                    if (urb->UrbBulkOrInterruptTransfer.TransferBufferMDL) {
                        buffer = (PUCHAR)MmGetSystemAddressForMdlSafe(urb->UrbBulkOrInterruptTransfer.TransferBufferMDL, NormalPagePriority);
                    } else {
                        buffer = (PUCHAR)urb->UrbBulkOrInterruptTransfer.TransferBuffer;
                    }
                    
                    if (buffer) {
                        // --- PACKET DUMP LOGIC ---
                        // Dump the first 20 bytes of the raw USB packet so we can inspect the exact protocol structure
                        static ULONG packetCount = 0;
                        if ((packetCount++ % 60) == 0) { // Print once every ~60 packets (roughly once a second for a 125Hz polling rate)
                            DbgPrint("[CTRL-DUMP] %02X %02X %02X %02X | %02X %02X %02X %02X | %02X %02X %02X %02X | %02X %02X %02X %02X | %02X %02X %02X %02X\n",
                                buffer[0], buffer[1], buffer[2], buffer[3],
                                buffer[4], buffer[5], buffer[6], buffer[7],
                                buffer[8], buffer[9], buffer[10], buffer[11],
                                buffer[12], buffer[13], buffer[14], buffer[15],
                                buffer[16], buffer[17], buffer[18], buffer[19]);
                        }

                        // Local cache to minimize spinlock hold time
                        CTRL_REQUEST localState = {0};
                        KIRQL oldIrql;
                        KeAcquireSpinLock(&g_StateLock, &oldIrql);
                        localState = g_InjectedState; // Fast struct copy
                        KeReleaseSpinLock(&g_StateLock, oldIrql);

                        BOOLEAN injected = FALSE;

                        // Xbox 360 Report (starts with 0x00 0x14)
                        if (buffer[0] == 0x00 && buffer[1] == 0x14) {
                            XUSB_REPORT* xr = (XUSB_REPORT*)buffer;
                            
                            // ADD the injected value to physical stick so the user can fight it
                            if (localState.thumbRX != 0 || localState.thumbRY != 0 || localState.thumbLX != 0 || localState.thumbLY != 0) {
                                if (localState.thumbRX != 0) xr->ThumbRX = (SHORT)max(-32768, min(32767, (LONG)xr->ThumbRX + localState.thumbRX));
                                if (localState.thumbRY != 0) xr->ThumbRY = (SHORT)max(-32768, min(32767, (LONG)xr->ThumbRY + localState.thumbRY));
                                if (localState.thumbLX != 0) xr->ThumbLX = (SHORT)max(-32768, min(32767, (LONG)xr->ThumbLX + localState.thumbLX));
                                if (localState.thumbLY != 0) xr->ThumbLY = (SHORT)max(-32768, min(32767, (LONG)xr->ThumbLY + localState.thumbLY));
                                injected = TRUE;
                            }
                            if (localState.leftTrigger) { xr->LeftTrigger = localState.leftTrigger; injected = TRUE; }
                            if (localState.rightTrigger) { xr->RightTrigger = localState.rightTrigger; injected = TRUE; }
                            if (localState.buttons) { xr->Buttons |= localState.buttons; injected = TRUE; }

                            if (injected) {
                                // DbgPrint("[CTRL] Injected X360! RX:%d RY:%d\n", xr->ThumbRX, xr->ThumbRY);
                            }
                        } 
                        // Xbox One / Elite Report (starts with 0x20)
                        else if (buffer[0] == 0x20) {
                            XBOXONE_REPORT* xr = (XBOXONE_REPORT*)buffer;

                            // ADD the injected value to physical stick
                            if (localState.thumbRX != 0 || localState.thumbRY != 0 || localState.thumbLX != 0 || localState.thumbLY != 0) {
                                if (localState.thumbRX != 0) xr->ThumbRX = (SHORT)max(-32768, min(32767, (LONG)xr->ThumbRX + localState.thumbRX));
                                if (localState.thumbRY != 0) xr->ThumbRY = (SHORT)max(-32768, min(32767, (LONG)xr->ThumbRY + localState.thumbRY));
                                if (localState.thumbLX != 0) xr->ThumbLX = (SHORT)max(-32768, min(32767, (LONG)xr->ThumbLX + localState.thumbLX));
                                if (localState.thumbLY != 0) xr->ThumbLY = (SHORT)max(-32768, min(32767, (LONG)xr->ThumbLY + localState.thumbLY));
                                injected = TRUE;
                            }
                            if (localState.leftTrigger) { 
                                // Scale 0-255 to 0-1023
                                xr->LeftTrigger = (USHORT)localState.leftTrigger * 4; 
                                injected = TRUE; 
                            }
                            if (localState.rightTrigger) { 
                                xr->RightTrigger = (USHORT)localState.rightTrigger * 4; 
                                injected = TRUE; 
                            }
                            if (localState.buttons) { 
                                // XINPUT_GAMEPAD_* to XBOXONE_REPORT mapping
                                USHORT b = localState.buttons;
                                if (b & 0x1000) xr->Buttons1 |= 0x10; // A
                                if (b & 0x2000) xr->Buttons1 |= 0x20; // B
                                if (b & 0x4000) xr->Buttons1 |= 0x40; // X
                                if (b & 0x8000) xr->Buttons1 |= 0x80; // Y
                                if (b & 0x0010) xr->Buttons1 |= 0x04; // START (Menu)
                                if (b & 0x0020) xr->Buttons1 |= 0x08; // BACK (View)

                                if (b & 0x0001) xr->Buttons2 |= 0x01; // DPAD UP
                                if (b & 0x0002) xr->Buttons2 |= 0x02; // DPAD DOWN
                                if (b & 0x0004) xr->Buttons2 |= 0x04; // DPAD LEFT
                                if (b & 0x0008) xr->Buttons2 |= 0x08; // DPAD RIGHT
                                if (b & 0x0100) xr->Buttons2 |= 0x10; // LB
                                if (b & 0x0200) xr->Buttons2 |= 0x20; // RB
                                if (b & 0x0040) xr->Buttons2 |= 0x40; // L3
                                if (b & 0x0080) xr->Buttons2 |= 0x80; // R3
                                injected = TRUE; 
                            }

                            if (injected) {
                                // Lowered print frequency
                                // DbgPrint("[CTRL] Injected XB1! RX:%d RY:%d RT:%u\n", xr->ThumbRX, xr->ThumbRY, xr->RightTrigger);
                            }
                        }
                    }
                }
            }
        }
    }

    NTSTATUS status = STATUS_SUCCESS;
    
    // Call original completion routine if one was set
    if (hookCtx->OriginalCompletion && 
        ((NT_SUCCESS(Irp->IoStatus.Status) && (hookCtx->OriginalControl & SL_INVOKE_ON_SUCCESS)) ||
         (!NT_SUCCESS(Irp->IoStatus.Status) && (hookCtx->OriginalControl & SL_INVOKE_ON_ERROR)) ||
         (Irp->Cancel && (hookCtx->OriginalControl & SL_INVOKE_ON_CANCEL)))) 
    {
        status = hookCtx->OriginalCompletion(DeviceObject, Irp, hookCtx->OriginalContext);
    } else {
        if (Irp->PendingReturned && !hookCtx->OriginalCompletion) {
             status = STATUS_SUCCESS; 
        }
    }

    ExFreePoolWithTag(hookCtx, 'kooH');
    return status;
}

//===========================================================================
// Dispatch Hooks
//===========================================================================
static NTSTATUS Hooked_InternalDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    
    // Only intercept IRPs for our specific Xbox controller PDO
    if (DeviceObject == g_TargetUsbPdo && irpSp->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_SUBMIT_URB) {
        PURB urb = (PURB)irpSp->Parameters.Others.Argument1;
        
        // Target bulk/interrupt IN transfers (Input reports)
        if (urb && urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER && (urb->UrbBulkOrInterruptTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN)) {
            
            PHOOK_CONTEXT hookCtx = (PHOOK_CONTEXT)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(HOOK_CONTEXT), 'kooH');
            if (hookCtx) {
                hookCtx->OriginalCompletion = irpSp->CompletionRoutine;
                hookCtx->OriginalContext    = irpSp->Context;
                hookCtx->OriginalControl    = irpSp->Control;
                hookCtx->Urb                = urb;
                
                irpSp->CompletionRoutine = Hooked_CompletionRoutine;
                irpSp->Context = hookCtx;
                irpSp->Control |= SL_INVOKE_ON_SUCCESS | SL_INVOKE_ON_ERROR | SL_INVOKE_ON_CANCEL;
            }
        }
    }

    return g_OriginalInternalDeviceControl(DeviceObject, Irp);
}

//===========================================================================
// Find the USB PDO belonging to xboxgip for our controller
//===========================================================================
static NTSTATUS FindXboxPdo(USHORT TargetVid, USHORT TargetPid) {
    UNICODE_STRING driverName;
    PDRIVER_OBJECT xboxgipDriver = nullptr;
    
    RtlInitUnicodeString(&driverName, L"\\Driver\\xboxgip");
    NTSTATUS status = ObReferenceObjectByName(
        &driverName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        nullptr, 0, *IoDriverObjectType,
        KernelMode, nullptr, (PVOID*)&xboxgipDriver
    );

    if (!NT_SUCCESS(status) || !xboxgipDriver) {
        DbgPrint("[CTRL] Failed to open \\Driver\\xboxgip (Status: %08X)\n", status);
        return STATUS_NOT_FOUND;
    }

    PDEVICE_OBJECT currentFdo = xboxgipDriver->DeviceObject;
    BOOLEAN found = FALSE;

    while (currentFdo && !found) {
        DbgPrint("[CTRL] Examining xboxgip FDO: %p\n", currentFdo);
        
        PDEVICE_OBJECT pdo = IoGetLowerDeviceObject(currentFdo);
        if (pdo) {
            // Traverse to the very bottom of the stack to find the actual PDO (USB or BTH)
            PDEVICE_OBJECT next = IoGetLowerDeviceObject(pdo);
            while (next) {
                ObDereferenceObject(pdo);
                pdo = next;
                next = IoGetLowerDeviceObject(pdo);
            }

            ULONG reqSize = 0;
            IoGetDeviceProperty(pdo, DevicePropertyHardwareID, 0, NULL, &reqSize);
            
            if (reqSize > 0 && reqSize < 2048) {
                PWCHAR hwId = (PWCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, reqSize, 'DIwh');
                if (hwId) {
                    if (NT_SUCCESS(IoGetDeviceProperty(pdo, DevicePropertyHardwareID, reqSize, hwId, &reqSize))) {
                        
                        DbgPrint("[CTRL] Found lower PDO %p Hardware ID: %ws\n", pdo, hwId);

                        WCHAR targetVidStr[5], targetPidStr[5];
                        const WCHAR hex[] = L"0123456789ABCDEF";
                        targetVidStr[0] = hex[(TargetVid >> 12) & 0xF]; targetVidStr[1] = hex[(TargetVid >> 8) & 0xF];
                        targetVidStr[2] = hex[(TargetVid >> 4) & 0xF];  targetVidStr[3] = hex[TargetVid & 0xF]; targetVidStr[4] = L'\0';

                        targetPidStr[0] = hex[(TargetPid >> 12) & 0xF]; targetPidStr[1] = hex[(TargetPid >> 8) & 0xF];
                        targetPidStr[2] = hex[(TargetPid >> 4) & 0xF];  targetPidStr[3] = hex[TargetPid & 0xF]; targetPidStr[4] = L'\0';

                        BOOLEAN foundVid = FALSE;
                        BOOLEAN foundPid = FALSE;
                        ULONG maxChars = reqSize / sizeof(WCHAR);
                        
                        for (ULONG i = 0; i < maxChars - 4; i++) {
                            WCHAR c0 = hwId[i];   if (c0 >= L'a' && c0 <= L'z') c0 -= 32;
                            WCHAR c1 = hwId[i+1]; if (c1 >= L'a' && c1 <= L'z') c1 -= 32;
                            WCHAR c2 = hwId[i+2]; if (c2 >= L'a' && c2 <= L'z') c2 -= 32;
                            WCHAR c3 = hwId[i+3]; if (c3 >= L'a' && c3 <= L'z') c3 -= 32;

                            if (c0 == targetVidStr[0] && c1 == targetVidStr[1] && c2 == targetVidStr[2] && c3 == targetVidStr[3]) {
                                foundVid = TRUE;
                            }
                            if (c0 == targetPidStr[0] && c1 == targetPidStr[1] && c2 == targetPidStr[2] && c3 == targetPidStr[3]) {
                                foundPid = TRUE;
                            }
                        }

                        // Strictly require both VID and PID to match what was requested
                        if (foundVid && foundPid) {
                            g_TargetUsbPdo = pdo;
                            g_HookedUsbDriver = pdo->DriverObject;
                            found = TRUE;
                            DbgPrint("[CTRL] ==> SUCCESS! Target PDO %p matches VID %04X PID %04X. Driver: %wZ\n", pdo, TargetVid, TargetPid, &g_HookedUsbDriver->DriverName);
                        }
                    }
                    ExFreePoolWithTag(hwId, 'DIwh');
                }
            }
            ObDereferenceObject(pdo); 
        }
        currentFdo = currentFdo->NextDevice;
    }

    ObDereferenceObject(xboxgipDriver);
    return found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}


//===========================================================================
// IOCTL 0x951 handler -- call from your IRP_MJ_DEVICE_CONTROL switch
//===========================================================================
NTSTATUS HandleControllerEvent(PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(CTRL_REQUEST))
        return STATUS_BUFFER_TOO_SMALL;

    CTRL_REQUEST* req = (CTRL_REQUEST*)Irp->AssociatedIrp.SystemBuffer;

    if (!g_Initialized)
        return STATUS_DEVICE_NOT_CONNECTED;

    // Update global state
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_StateLock, &oldIrql);
    
    g_InjectedState.thumbLX      = req->thumbLX;
    g_InjectedState.thumbLY      = req->thumbLY;
    g_InjectedState.thumbRX      = req->thumbRX;
    g_InjectedState.thumbRY      = req->thumbRY;
    g_InjectedState.leftTrigger  = req->leftTrigger;
    g_InjectedState.rightTrigger = req->rightTrigger;
    g_InjectedState.buttons      = req->buttons;

    KeReleaseSpinLock(&g_StateLock, oldIrql);

    return STATUS_SUCCESS;
}

//===========================================================================
// IOCTL 0x953 handler -- Select Controller
//===========================================================================
NTSTATUS HandleSelectController(PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(SELECT_REQUEST))
        return STATUS_BUFFER_TOO_SMALL;

    PSELECT_REQUEST req = (PSELECT_REQUEST)Irp->AssociatedIrp.SystemBuffer;
    DbgPrint("[CTRL] HandleSelectController called for VID:%04X PID:%04X Type:%u\n", req->Vid, req->Pid, req->Type);

    KeWaitForSingleObject(&g_InitMutex, Executive, KernelMode, FALSE, NULL);

    NTSTATUS status = FindXboxPdo(req->Vid, req->Pid);
    
    if (NT_SUCCESS(status) && g_HookedUsbDriver && !g_Initialized) {
        KeInitializeSpinLock(&g_StateLock);

        g_OriginalInternalDeviceControl = (PDRIVER_DISPATCH)InterlockedExchangePointer(
            (PVOID*)&g_HookedUsbDriver->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL],
            (PVOID)Hooked_InternalDeviceControl
        );

        DbgPrint("[CTRL] SUCCESSFULLY HOOKED USB DRIVER: %wZ (IRP_MJ_INTERNAL_DEVICE_CONTROL)\n", &g_HookedUsbDriver->DriverName);
        g_Initialized = TRUE;
    } else if (!NT_SUCCESS(status)) {
        DbgPrint("[CTRL] FindXboxPdo failed with status %08X\n", status);
    }

    KeReleaseMutex(&g_InitMutex, FALSE);

    return status;
}

//===========================================================================
// Call in DriverEntry alongside your other inits
//===========================================================================
VOID InitControllerLock()
{
    KeInitializeMutex(&g_InitMutex, 0);
}

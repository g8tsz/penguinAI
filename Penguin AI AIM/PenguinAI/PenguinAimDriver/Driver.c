// PenguinAimDriver/Driver.c
#include <ntddk.h>
#include <ntdef.h>
#include <wdf.h>
#include <intrin.h>
#include "Driver.h"

// Global Variables
HANDLE g_hProcess = NULL;
PVOID g_pBaseAddress = NULL;
PVOID g_pCInput = NULL; // Store CInput pointer
AIM_DATA g_AimData = { 0 };

// Read Memory from Target Process
NTSTATUS ReadMemory(HANDLE hProcess, LPCVOID lpBaseAddress, LPVOID lpBuffer, SIZE_T nSize) {
    SIZE_T bytesRead = 0;
    return MmCopyVirtualMemory(hProcess, lpBaseAddress, PsGetCurrentProcess(), lpBuffer, nSize, KernelMode, &bytesRead);
}

// Find Apex.exe Process
NTSTATUS FindApexProcess() {
    NTSTATUS status;
    ULONG bufferSize = 0;
    PVOID buffer = NULL;
    PSYSTEM_PROCESS_INFORMATION processInfo;
    UNICODE_STRING targetName = RTL_CONSTANT_STRING(L"Apex.exe");

    status = ZwQuerySystemInformation(SystemProcessInformation, NULL, 0, &bufferSize);
    if (status == STATUS_BUFFER_TOO_SMALL) {
        buffer = ExAllocatePoolWithTag(NonPagedPool, bufferSize, 'AimA');
    } else {
        return status;
    }

    status = ZwQuerySystemInformation(SystemProcessInformation, buffer, bufferSize, NULL);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(buffer, 'AimA');
        return status;
    }

    processInfo = (PSYSTEM_PROCESS_INFORMATION)buffer;
    do {
        if (RtlCompareMemory(processInfo->ImageName.Buffer, targetName.Buffer, targetName.Length) == targetName.Length) {
            g_hProcess = (HANDLE)processInfo->UniqueProcessId;
            return STATUS_SUCCESS;
        }
        processInfo = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)processInfo + processInfo->NextEntryOffset);
    } while (processInfo->NextEntryOffset != 0);

    ExFreePoolWithTag(buffer, 'AimA');
    return STATUS_NOT_FOUND;
}

// Get Local Player
PVOID GetLocalPlayer() {
    ULONG localPlayerOffset = 0;
    ReadMemory(g_hProcess, (PVOID)((ULONG_PTR)g_pBaseAddress + OFFSET_LOCAL_PLAYER), &localPlayerOffset, sizeof(ULONG));
    return (PVOID)((ULONG_PTR)g_pBaseAddress + localPlayerOffset);
}

// Get Entity List
PVOID GetEntityList() {
    ULONG entityListOffset = 0;
    ReadMemory(g_hProcess, (PVOID)((ULONG_PTR)g_pBaseAddress + OFFSET_ENTITY_LIST), &entityListOffset, sizeof(ULONG));
    return (PVOID)((ULONG_PTR)g_pBaseAddress + entityListOffset);
}

// Get Bone Position (0 = Head)
BOOL GetBonePosition(PVOID entity, int boneId, float* position) {
    BYTE boneMatrix[256];
    // Assuming bone matrix is at offset 0x420 relative to entity base
    if (!ReadMemory(g_hProcess, (PVOID)((ULONG_PTR)entity + 0x420), boneMatrix, sizeof(boneMatrix))) return FALSE;

    if (boneId >= 8) return FALSE;

    position[0] = boneMatrix[boneId * 12 + 0];
    position[1] = boneMatrix[boneId * 12 + 1];
    position[2] = boneMatrix[boneId * 12 + 2];
    return TRUE;
}

// World to Screen Projection
BOOL WorldToScreen(float* worldPos, float* screenPos, float* viewMatrix) {
    float transform[4];
    transform[0] = viewMatrix[0] * worldPos[0] + viewMatrix[1] * worldPos[1] + viewMatrix[2] * worldPos[2] + viewMatrix[3];
    transform[1] = viewMatrix[4] * worldPos[0] + viewMatrix[5] * worldPos[1] + viewMatrix[6] * worldPos[2] + viewMatrix[7];
    transform[2] = viewMatrix[8] * worldPos[0] + viewMatrix[9] * worldPos[1] + viewMatrix[10] * worldPos[2] + viewMatrix[11];
    transform[3] = viewMatrix[12] * worldPos[0] + viewMatrix[13] * worldPos[1] + viewMatrix[14] * worldPos[2] + viewMatrix[15];

    if (transform[3] <= 0.001f) return FALSE;

    screenPos[0] = (transform[0] / transform[3]) * 0.5f + 0.5f;
    screenPos[1] = (transform[1] / transform[3]) * 0.5f + 0.5f;
    return TRUE;
}

// Aim Logic
VOID AimAtClosest() {
    PVOID localPlayer = GetLocalPlayer();
    if (!localPlayer) return;

    LONG health = 0;
    // Offset 0x440 is usually health in Apex
    ReadMemory(g_hProcess, (PVOID)((ULONG_PTR)localPlayer + 0x440), &health, sizeof(health));
    if (health <= 0) return;

    PVOID entityList = GetEntityList();
    if (!entityList) return;

    float bestScreenX = 0, bestScreenY = 0;
    float bestDist = 99999.0f;
    bool foundTarget = false;

    for (int i = 0; i < 0x10000; i++) { // Iterate handle list
        PVOID entity = (PVOID)((ULONG_PTR)entityList + (i * 16));
        
        BYTE isAlive = 0;
        ReadMemory(g_hProcess, (PVOID)((ULONG_PTR)entity + 0x254), &isAlive, sizeof(isAlive));
        if (!isAlive) continue;

        float playerPos[3];
        if (!GetBonePosition(entity, 0, playerPos)) continue;

        float viewMatrix[16];
        ReadMemory(g_hProcess, (PVOID)((ULONG_PTR)g_pBaseAddress + OFFSET_VIEW_MATRIX), viewMatrix, sizeof(viewMatrix));

        float screenPos[3];
        if (!WorldToScreen(playerPos, screenPos, viewMatrix)) continue;

        float dist = sqrt(pow(screenPos[0] - 0.5, 2) + pow(screenPos[1] - 0.5, 2));
        if (dist < bestDist && dist < 0.3f) { // FOV Check
            bestDist = dist;
            bestScreenX = screenPos[0];
            bestScreenY = screenPos[1];
            foundTarget = true;
        }
    }

    if (foundTarget) {
        g_AimData.TargetX = bestScreenX;
        g_AimData.TargetY = bestScreenY;
        g_AimData.Active = TRUE;
    } else {
        g_AimData.Active = FALSE;
    }
}

// Driver Entry
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    DbgPrint("Penguin AI AIM Driver Loading...\n");

    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, DEVICE_NAME);

    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        NULL
    );

    if (!NT_SUCCESS(status)) return status;

    UNICODE_STRING symbolicLink;
    RtlInitUnicodeString(&symbolicLink, SYMLINK_NAME);
    status = IoCreateSymbolicLink(&symbolicLink, &deviceName);

    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(DriverObject->DeviceObject);
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = MyDeviceControl;
    DriverObject->DriverUnload = DriverUnload;

    status = FindApexProcess();
    if (NT_SUCCESS(status)) {
        DbgPrint("Found Apex.exe at PID: %d\n", (ULONG)g_hProcess);
        // Store CInput pointer
        ULONG cInputVal = 0;
        ReadMemory(g_hProcess, (PVOID)((ULONG_PTR)g_pBaseAddress + OFFSET_CINPUT), &cInputVal, sizeof(ULONG));
        g_pCInput = (PVOID)((ULONG_PTR)g_pBaseAddress + cInputVal);
    } else {
        DbgPrint("Apex.exe not found.\n");
    }

    return STATUS_SUCCESS;
}

// Device Control Handler
NTSTATUS MyDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    NTSTATUS status = STATUS_SUCCESS;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);

    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_AIM_START:
        AimAtClosest();
        break;
    case IOCTL_AIM_UPDATE:
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &g_AimData, sizeof(AIM_DATA));
        Irp->IoStatus.Information = sizeof(AIM_DATA);
        break;
    case IOCTL_AIM_STOP:
        g_AimData.Active = FALSE;
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

// Driver Unload
VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING symbolicLink;
    RtlInitUnicodeString(&symbolicLink, SYMLINK_NAME);
    IoDeleteSymbolicLink(&symbolicLink);
    IoDeleteDevice(DriverObject->DeviceObject);
    DbgPrint("Penguin AI AIM Driver Unloaded.\n");
}
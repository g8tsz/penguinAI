// PenguinAimDriver/Driver.h
#pragma once

#include <ntddk.h>

// Device & Symbolic Link
#define DEVICE_NAME L"\\Device\\PenguinAimDriver"
#define SYMLINK_NAME L"\\??\\PenguinAimDriver"

// IOCTL Codes
#define IOCTL_AIM_START  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AIM_STOP   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AIM_UPDATE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Shared Data Structure
typedef struct _AIM_DATA {
    float TargetX;
    float TargetY;
    BOOL Active;
} AIM_DATA, *PAIM_DATA;

// --- MISC OFFSETS (User Provided) ---
#define OFFSET_ENTITY_LIST  0x61e51c8
#define OFFSET_LOCAL_PLAYER 0x26bcda8
#define OFFSET_VIEW_MATRIX  0x11a350
#define OFFSET_CAMERA_ORIGIN 0x1fac    // CPlayer!camera_origin
#define OFFSET_CAMERA_ANGLES 0x1fb8    // CPlayer!camera_angles
#define OFFSET_CINPUT        0x2584cf0 // CInput
#define OFFSET_NET_CHANNEL   0x1ba3a20 // NetChannel

// Function Prototypes
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
VOID DriverUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS MyDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ReadMemory(HANDLE hProcess, LPCVOID lpBaseAddress, LPVOID lpBuffer, SIZE_T nSize);
NTSTATUS FindApexProcess();
PVOID GetLocalPlayer();
PVOID GetEntityList();
BOOL GetBonePosition(PVOID entity, int boneId, float* position);
BOOL WorldToScreen(float* worldPos, float* screenPos, float* viewMatrix);
VOID AimAtClosest();
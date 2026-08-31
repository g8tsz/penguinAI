// PenguinAimClient/main.cpp
#include <windows.h>
#include <iostream>
#include <thread>
#include <TlHelp32.h>

// --- MISC OFFSETS (User Provided) ---
#define OFFSET_ENTITY_LIST  0x61e51c8
#define OFFSET_LOCAL_PLAYER 0x26bcda8
#define OFFSET_VIEW_MATRIX  0x11a350
#define OFFSET_CINPUT        0x2584cf0
#define OFFSET_IN_ATTACK     0x03cbceb8 // in_attack
#define OFFSET_IN_FORWARD    0x03cbcde8 // in_forward

// IOCTL Codes
#define IOCTL_AIM_START  0x800
#define IOCTL_AIM_STOP   0x801
#define IOCTL_AIM_UPDATE 0x802

// Shared Data Structure
typedef struct _AIM_DATA {
    float TargetX;
    float TargetY;
    BOOL Active;
} AIM_DATA;

// Global Variables
HANDLE hDevice = NULL;
bool g_AimActive = false;

// Smooth Mouse Movement
void MoveMouseSmooth(int dx, int dy) {
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

// Set Button Input (Write to CInput)
void SetButton(DWORD64 cInputAddr, DWORD offset, BOOL state) {
    BYTE currentVal = 0;
    ReadProcessMemory(GetCurrentProcess(), (LPCVOID)cInputAddr, &currentVal, sizeof(BYTE), NULL);
    
    if (state) {
        currentVal |= (1 << offset);
    } else {
        currentVal &= ~(1 << offset);
    }
    
    WriteProcessMemory(GetCurrentProcess(), (LPVOID)cInputAddr, &currentVal, sizeof(BYTE), NULL);
}

// Aim Assist Thread
void AimThread() {
    AIM_DATA aimData;
    DWORD64 cInputAddr = 0;

    while (true) {
        if (g_AimActive) {
            // Get CInput address from global (passed from driver or calculated)
            // For now, we calculate it locally to ensure sync
            DWORD64 base = 0;
            if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)0x180000000, &base, sizeof(DWORD64), NULL)) {
                 cInputAddr = base + OFFSET_CINPUT;
            }

            DWORD bytesReturned;
            if (DeviceIoControl(hDevice, IOCTL_AIM_UPDATE, NULL, 0, &aimData, sizeof(AIM_DATA), &bytesReturned, NULL)) {
                if (aimData.Active) {
                    int centerX = GetSystemMetrics(SM_CXSCREEN) / 2;
                    int centerY = GetSystemMetrics(SM_CYSCREEN) / 2;
                    int targetX = (aimData.TargetX - 0.5f) * 2 * centerX;
                    int targetY = (aimData.TargetY - 0.5f) * 2 * centerY;
                    MoveMouseSmooth(targetX, targetY);
                    
                    // Optional: Auto Shoot (using in_attack)
                    SetButton(cInputAddr, 0, true); // in_attack is usually bit 0
                    Sleep(50);
                    SetButton(cInputAddr, 0, false);
                }
            }
        } else {
             DWORD64 base = 0;
             if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)0x180000000, &base, sizeof(DWORD64), NULL)) {
                 DWORD64 cInputAddr = base + OFFSET_CINPUT;
                 SetButton(cInputAddr, 0, false); // Reset attack
             }
        }
        Sleep(1); // 1ms delay
    }
}

int main() {
    std::cout << "Penguin AI AIM - Apex Legends Aim Assist" << std::endl;

    // Load Driver
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        std::cerr << "Failed to open SCM. Run as Administrator." << std::endl;
        return 1;
    }

    SC_HANDLE hService = CreateService(
        hSCM, "PenguinAimService", "Penguin AI AIM Driver",
        SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        "C:\\path\\to\\PenguinAimDriver.sys", NULL, NULL, NULL, NULL, NULL
    );

    if (!hService) {
        if (GetLastError() == ERROR_SERVICE_EXISTS) {
            hService = OpenService(hSCM, "PenguinAimService", SERVICE_ALL_ACCESS);
        } else {
            std::cerr << "Failed to create service." << std::endl;
            return 1;
        }
    }

    if (!StartService(hService, 0, NULL)) {
        if (GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
            std::cerr << "Failed to start service." << std::endl;
            return 1;
        }
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);

    // Connect to Driver
    hDevice = CreateFileA("\\\\.\\PenguinAimDriver", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to connect to driver." << std::endl;
        return 1;
    }

    // Start Aim Thread
    std::thread aimThread(AimThread);
    aimThread.detach();

    std::cout << "Press F6 to toggle aim assist." << std::endl;
    std::cout << "Press F7 to toggle auto-shoot." << std::endl;

    // Toggle Loop
    bool autoShoot = false;
    while (true) {
        if (GetAsyncKeyState(VK_F6) & 0x8000) {
            g_AimActive = !g_AimActive;
            if (g_AimActive) {
                DeviceIoControl(hDevice, IOCTL_AIM_START, NULL, 0, NULL, 0, NULL, NULL);
                std::cout << "Aim Assist: ON" << std::endl;
            } else {
                DeviceIoControl(hDevice, IOCTL_AIM_STOP, NULL, 0, NULL, 0, NULL, NULL);
                std::cout << "Aim Assist: OFF" << std::endl;
            }
            Sleep(200);
        }
        
        if (GetAsyncKeyState(VK_F7) & 0x8000) {
            autoShoot = !autoShoot;
            std::cout << "Auto Shoot: " << (autoShoot ? "ON" : "OFF") << std::endl;
            Sleep(200);
        }
        Sleep(10);
    }

    return 0;
}
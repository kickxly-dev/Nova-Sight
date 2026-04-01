#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "config.h"
#include "overlay.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // Prevent multiple instances
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"NovaSightSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
                    L"Nova-Sight is already running.",
                    L"Nova-Sight", MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }

    Config config;
    if (!config.load("data/games.json")) {
        MessageBoxW(nullptr,
                    L"Could not load data/games.json.\n"
                    L"Make sure the file exists next to nova-sight.exe.",
                    L"Nova-Sight — Config Error", MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    Overlay overlay(hInstance, config);
    if (!overlay.init()) {
        MessageBoxW(nullptr,
                    L"Failed to initialise the overlay.\n"
                    L"Direct2D or DirectWrite may be unavailable.",
                    L"Nova-Sight — Init Error", MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    int result = overlay.run();
    CloseHandle(mutex);
    return result;
}

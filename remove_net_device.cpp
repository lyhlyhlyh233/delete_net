#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06010000
#endif

#include <windows.h>
#include <setupapi.h>
#include <newdev.h>

#include <iostream>

#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(linker, "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

DWORD RemoveDevice(const wchar_t* deviceId, bool* rebootRequired) {
    if (!deviceId || !*deviceId || !rebootRequired) {
        return ERROR_INVALID_PARAMETER;
    }

    *rebootRequired = false;
    HDEVINFO deviceSet = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (deviceSet == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }

    SP_DEVINFO_DATA device = {};
    device.cbSize = sizeof(device);
    DWORD error = ERROR_SUCCESS;

    if (!SetupDiOpenDeviceInfoW(deviceSet, deviceId, nullptr, 0, &device)) {
        error = GetLastError();
    } else {
        BOOL reboot = FALSE;
        if (!DiUninstallDevice(nullptr, deviceSet, &device, 0, &reboot)) {
            error = GetLastError();
        } else {
            *rebootRequired = reboot != FALSE;
        }
    }

    SetupDiDestroyDeviceInfoList(deviceSet);
    return error;
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        std::wcerr << L"Usage: " << argv[0]
                   << L" <DeviceInstanceId> [DeviceInstanceId ...]\n";
        return 2;
    }

    bool failed = false;
    bool rebootRequired = false;
    for (int index = 1; index < argc; ++index) {
        bool reboot = false;
        const DWORD error = RemoveDevice(argv[index], &reboot);
        if (error == ERROR_SUCCESS) {
            std::wcout << L"OK: " << argv[index] << L"\n";
            rebootRequired = rebootRequired || reboot;
        } else {
            std::wcerr << L"FAILED " << error << L": " << argv[index] << L"\n";
            failed = true;
        }
    }

    if (failed) {
        return 1;
    }
    return rebootRequired ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_SUCCESS;
}

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
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
#include <string>

#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(linker, "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

DWORD RemoveDevice(const std::string& deviceId) {
    if (deviceId.empty()) {
        return ERROR_INVALID_PARAMETER;
    }

    HDEVINFO deviceSet = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (deviceSet == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }

    SP_DEVINFO_DATA device = {};
    device.cbSize = sizeof(device);
    DWORD error = ERROR_SUCCESS;

    if (!SetupDiOpenDeviceInfoA(
            deviceSet, deviceId.c_str(), nullptr, 0, &device)) {
        error = GetLastError();
    } else {
        BOOL ignoredReboot = FALSE;
        if (!DiUninstallDevice(
                nullptr, deviceSet, &device, 0, &ignoredReboot)) {
            error = GetLastError();
        }
    }

    SetupDiDestroyDeviceInfoList(deviceSet);
    return error;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <DeviceInstanceId> [DeviceInstanceId ...]\n";
        return 2;
    }

    bool failed = false;
    for (int index = 1; index < argc; ++index) {
        const DWORD error = RemoveDevice(argv[index]);
        if (error == ERROR_SUCCESS) {
            std::cout << "OK: " << argv[index] << "\n";
        } else {
            std::cerr << "FAILED " << error << ": " << argv[index] << "\n";
            failed = true;
        }
    }

    return failed ? 1 : ERROR_SUCCESS;
}

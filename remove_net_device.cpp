#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

#include <cwchar>
#include <iostream>
#include <string>
#include <vector>

#include "network_device_remover.h"

#pragma comment(linker, "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

namespace {

int ListDevices() {
    std::vector<netdevice::NetworkAdapterInfo> adapters;
    DWORD error = ERROR_SUCCESS;
    if (!netdevice::EnumerateNetworkAdapters(&adapters, &error)) {
        std::wcerr << L"Failed to enumerate network devices: "
                   << error << L" ("
                   << netdevice::FormatWin32Error(error) << L")\n";
        return 4;
    }

    for (const netdevice::NetworkAdapterInfo& adapter : adapters) {
        std::wcout << L"Name       : " << adapter.name << L"\n"
                   << L"Instance ID: " << adapter.deviceInstanceId << L"\n"
                   << L"GUID       : ";
        if (!adapter.interfaceGuid.empty()) {
            std::wcout << adapter.interfaceGuid;
        } else {
            std::wcout << L"(unavailable, error "
                       << adapter.interfaceGuidError << L")";
        }
        std::wcout << L"\n\n";
    }

    std::wcout << L"Network device instance(s): "
               << adapters.size() << L"\n";
    return 0;
}

void PrintResult(const netdevice::RemoveResult& result) {
    std::wcout << L"Target ID  : " << result.target.deviceInstanceId << L"\n"
               << L"Target GUID: " << result.target.interfaceGuid << L"\n";
    if (!result.deviceName.empty()) {
        std::wcout << L"Name       : " << result.deviceName << L"\n";
    }
    if (!result.detectedInterfaceGuid.empty()) {
        std::wcout << L"Found GUID : "
                   << result.detectedInterfaceGuid << L"\n";
    }

    std::wcout << L"Status     : "
               << netdevice::RemoveStatusName(result.status) << L"\n";
    if (!result.backend.empty()) {
        std::wcout << L"API        : " << result.backend << L"\n";
    }
    if (result.error != ERROR_SUCCESS) {
        std::wcout << L"Error      : " << result.error << L" ("
                   << netdevice::FormatWin32Error(result.error) << L")\n";
    }
    if (result.status == netdevice::RemoveStatus::Removed) {
        std::wcout << L"Reboot     : "
                   << (result.rebootRequired ? L"required" : L"not required")
                   << L"\n";
    }
    std::wcout << L"\n";
}

int ExitCodeForResults(
    const std::vector<netdevice::RemoveResult>& results) {
    bool rebootRequired = false;
    bool notFound = false;
    bool mismatch = false;
    bool invalidTarget = false;
    bool failed = false;

    for (const netdevice::RemoveResult& result : results) {
        switch (result.status) {
            case netdevice::RemoveStatus::Removed:
                rebootRequired = rebootRequired || result.rebootRequired;
                break;
            case netdevice::RemoveStatus::NotFound:
                notFound = true;
                break;
            case netdevice::RemoveStatus::InterfaceGuidMismatch:
                mismatch = true;
                break;
            case netdevice::RemoveStatus::InvalidTarget:
                invalidTarget = true;
                break;
            case netdevice::RemoveStatus::InterfaceGuidUnavailable:
            case netdevice::RemoveStatus::Failed:
                failed = true;
                break;
        }
    }

    if (failed) {
        return 4;
    }
    if (invalidTarget) {
        return 2;
    }
    if (mismatch) {
        return 5;
    }
    if (notFound) {
        return 3;
    }
    return rebootRequired
        ? static_cast<int>(ERROR_SUCCESS_REBOOT_REQUIRED)
        : ERROR_SUCCESS;
}

int RemoveDevices(int argc, wchar_t* argv[]) {
    if (argc < 4 || ((argc - 2) % 2) != 0) {
        return 2;
    }

    std::vector<netdevice::NetworkAdapterTarget> targets;
    for (int index = 2; index < argc; index += 2) {
        netdevice::NetworkAdapterTarget target;
        target.deviceInstanceId = argv[index];
        target.interfaceGuid = argv[index + 1];
        targets.push_back(target);
    }

    const std::vector<netdevice::RemoveResult> results =
        netdevice::RemoveNetworkAdapters(targets);
    for (const netdevice::RemoveResult& result : results) {
        PrintResult(result);
    }
    return ExitCodeForResults(results);
}

void PrintUsage(const wchar_t* program) {
    std::wcerr
        << L"Usage:\n"
        << L"  " << program << L" --list\n"
        << L"  " << program
        << L" --remove <DeviceInstanceId> <InterfaceGuid>"
        << L" [<DeviceInstanceId> <InterfaceGuid> ...]\n";
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc == 2 && _wcsicmp(argv[1], L"--list") == 0) {
        return ListDevices();
    }
    if (argc >= 2 && _wcsicmp(argv[1], L"--remove") == 0) {
        const int result = RemoveDevices(argc, argv);
        if (result == 2) {
            PrintUsage(argv[0]);
        }
        return result;
    }

    PrintUsage(argv[0]);
    return 2;
}

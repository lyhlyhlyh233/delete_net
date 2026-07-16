#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "network_device_remover.h"

#include <setupapi.h>

#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace netdevice {
namespace {

const GUID kNetworkAdapterClass = {
    0x4d36e972, 0xe325, 0x11ce,
    {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}
};

using DiUninstallDeviceFn = BOOL(WINAPI *)(
    HWND,
    HDEVINFO,
    PSP_DEVINFO_DATA,
    DWORD,
    PBOOL);

std::wstring GetInstanceId(
    HDEVINFO deviceInfoSet,
    SP_DEVINFO_DATA* deviceInfoData) {
    DWORD required = 0;
    SetupDiGetDeviceInstanceIdW(
        deviceInfoSet, deviceInfoData, nullptr, 0, &required);
    if (required == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return L"";
    }

    std::vector<wchar_t> buffer(required);
    if (!SetupDiGetDeviceInstanceIdW(
            deviceInfoSet,
            deviceInfoData,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            nullptr)) {
        return L"";
    }
    return std::wstring(buffer.data());
}

std::wstring GetRegistryPropertyString(
    HDEVINFO deviceInfoSet,
    SP_DEVINFO_DATA* deviceInfoData,
    DWORD property) {
    DWORD type = 0;
    DWORD required = 0;
    SetupDiGetDeviceRegistryPropertyW(
        deviceInfoSet,
        deviceInfoData,
        property,
        &type,
        nullptr,
        0,
        &required);
    if (required == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return L"";
    }

    std::vector<BYTE> buffer(required + sizeof(wchar_t), 0);
    if (!SetupDiGetDeviceRegistryPropertyW(
            deviceInfoSet,
            deviceInfoData,
            property,
            &type,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            nullptr)) {
        return L"";
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        return L"";
    }
    return std::wstring(reinterpret_cast<const wchar_t*>(buffer.data()));
}

std::wstring GetDeviceName(
    HDEVINFO deviceInfoSet,
    SP_DEVINFO_DATA* deviceInfoData) {
    std::wstring name = GetRegistryPropertyString(
        deviceInfoSet, deviceInfoData, SPDRP_FRIENDLYNAME);
    if (name.empty()) {
        name = GetRegistryPropertyString(
            deviceInfoSet, deviceInfoData, SPDRP_DEVICEDESC);
    }
    return name.empty() ? L"(unnamed network adapter)" : name;
}

bool ReadStringValue(
    HKEY key,
    const wchar_t* valueName,
    std::wstring* value,
    DWORD* error) {
    DWORD type = 0;
    DWORD bytes = 0;
    LONG result = RegQueryValueExW(
        key, valueName, nullptr, &type, nullptr, &bytes);
    if (result != ERROR_SUCCESS) {
        *error = static_cast<DWORD>(result);
        return false;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        *error = ERROR_DATATYPE_MISMATCH;
        return false;
    }

    std::vector<wchar_t> buffer(
        bytes / sizeof(wchar_t) + 1, L'\0');
    result = RegQueryValueExW(
        key,
        valueName,
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(buffer.data()),
        &bytes);
    if (result != ERROR_SUCCESS) {
        *error = static_cast<DWORD>(result);
        return false;
    }

    *value = std::wstring(buffer.data());
    *error = ERROR_SUCCESS;
    return true;
}

bool GetInterfaceGuid(
    HDEVINFO deviceInfoSet,
    SP_DEVINFO_DATA* deviceInfoData,
    std::wstring* interfaceGuid,
    DWORD* error) {
    HKEY driverKey = SetupDiOpenDevRegKey(
        deviceInfoSet,
        deviceInfoData,
        DICS_FLAG_GLOBAL,
        0,
        DIREG_DRV,
        KEY_QUERY_VALUE);
    if (driverKey == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return false;
    }

    const bool read = ReadStringValue(
        driverKey, L"NetCfgInstanceId", interfaceGuid, error);
    RegCloseKey(driverKey);
    return read;
}

bool NormalizeInterfaceGuid(
    const std::wstring& input,
    std::wstring* normalized) {
    size_t first = 0;
    while (first < input.size() && iswspace(input[first])) {
        ++first;
    }
    size_t last = input.size();
    while (last > first && iswspace(input[last - 1])) {
        --last;
    }

    std::wstring value = input.substr(first, last - first);
    if (value.size() >= 2 && value.front() == L'{' &&
        value.back() == L'}') {
        value = value.substr(1, value.size() - 2);
    }
    if (value.size() != 36) {
        return false;
    }

    normalized->clear();
    normalized->reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index) {
        const bool hyphenPosition =
            index == 8 || index == 13 || index == 18 || index == 23;
        if (hyphenPosition) {
            if (value[index] != L'-') {
                return false;
            }
            normalized->push_back(L'-');
            continue;
        }
        if (!iswxdigit(value[index])) {
            return false;
        }
        normalized->push_back(
            static_cast<wchar_t>(towupper(value[index])));
    }
    return true;
}

HMODULE LoadSystemDll(const wchar_t* dllName) {
    wchar_t systemDirectory[MAX_PATH] = {};
    const UINT capacity = static_cast<UINT>(
        sizeof(systemDirectory) / sizeof(systemDirectory[0]));
    const UINT length = GetSystemDirectoryW(systemDirectory, capacity);
    if (length == 0 || length >= capacity) {
        return nullptr;
    }

    std::wstring path(systemDirectory, length);
    path += L"\\";
    path += dllName;
    return LoadLibraryW(path.c_str());
}

bool RemoveWithLegacySetupApi(
    HDEVINFO deviceInfoSet,
    SP_DEVINFO_DATA* deviceInfoData,
    bool* needReboot,
    DWORD* error) {
    SP_REMOVEDEVICE_PARAMS removeParams = {};
    removeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    removeParams.ClassInstallHeader.InstallFunction = DIF_REMOVE;
    removeParams.Scope = DI_REMOVEDEVICE_GLOBAL;
    removeParams.HwProfile = 0;

    if (!SetupDiSetClassInstallParamsW(
            deviceInfoSet,
            deviceInfoData,
            &removeParams.ClassInstallHeader,
            sizeof(removeParams))) {
        *error = GetLastError();
        return false;
    }

    if (!SetupDiCallClassInstaller(
            DIF_REMOVE, deviceInfoSet, deviceInfoData)) {
        *error = GetLastError();
        return false;
    }

    SP_DEVINSTALL_PARAMS_W installParams = {};
    installParams.cbSize = sizeof(installParams);
    if (SetupDiGetDeviceInstallParamsW(
            deviceInfoSet, deviceInfoData, &installParams)) {
        *needReboot =
            (installParams.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) != 0;
    }

    *error = ERROR_SUCCESS;
    return true;
}

bool RemoveDevice(
    HDEVINFO deviceInfoSet,
    SP_DEVINFO_DATA* deviceInfoData,
    bool* needReboot,
    DWORD* error,
    std::wstring* backend) {
    HMODULE newDev = LoadSystemDll(L"newdev.dll");
    if (newDev) {
        DiUninstallDeviceFn uninstallDevice =
            reinterpret_cast<DiUninstallDeviceFn>(
                GetProcAddress(newDev, "DiUninstallDevice"));
        if (uninstallDevice) {
            BOOL reboot = FALSE;
            const BOOL removed = uninstallDevice(
                nullptr, deviceInfoSet, deviceInfoData, 0, &reboot);
            *error = removed ? ERROR_SUCCESS : GetLastError();
            *needReboot = reboot != FALSE;
            *backend = L"DiUninstallDevice";
            FreeLibrary(newDev);
            return removed != FALSE;
        }
        FreeLibrary(newDev);
    }

    *backend = L"SetupDiCallClassInstaller(DIF_REMOVE)";
    return RemoveWithLegacySetupApi(
        deviceInfoSet, deviceInfoData, needReboot, error);
}

HDEVINFO OpenNetworkDeviceSet() {
    // Omitting DIGCF_PRESENT includes stale, non-present adapters.
    return SetupDiGetClassDevsW(
        &kNetworkAdapterClass, nullptr, nullptr, 0);
}

}  // namespace

std::wstring FormatWin32Error(DWORD error) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<LPWSTR>(&message),
        0,
        nullptr);

    std::wstring result = length && message ? message : L"Unknown error";
    if (message) {
        LocalFree(message);
    }
    while (!result.empty() &&
           (result.back() == L'\r' || result.back() == L'\n')) {
        result.pop_back();
    }
    return result;
}

bool EnumerateNetworkAdapters(
    std::vector<NetworkAdapterInfo>* adapters,
    DWORD* error) {
    if (!adapters || !error) {
        if (error) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return false;
    }
    adapters->clear();

    HDEVINFO deviceInfoSet = OpenNetworkDeviceSet();
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return false;
    }

    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA deviceInfoData = {};
        deviceInfoData.cbSize = sizeof(deviceInfoData);
        if (!SetupDiEnumDeviceInfo(
                deviceInfoSet, index, &deviceInfoData)) {
            const DWORD enumerationError = GetLastError();
            if (enumerationError != ERROR_NO_MORE_ITEMS) {
                SetupDiDestroyDeviceInfoList(deviceInfoSet);
                *error = enumerationError;
                return false;
            }
            break;
        }

        NetworkAdapterInfo adapter;
        adapter.name = GetDeviceName(deviceInfoSet, &deviceInfoData);
        adapter.deviceInstanceId = GetInstanceId(
            deviceInfoSet, &deviceInfoData);
        if (adapter.deviceInstanceId.empty()) {
            continue;
        }

        DWORD guidError = ERROR_SUCCESS;
        if (!GetInterfaceGuid(
                deviceInfoSet,
                &deviceInfoData,
                &adapter.interfaceGuid,
                &guidError)) {
            adapter.interfaceGuidError = guidError;
        }
        adapters->push_back(adapter);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    *error = ERROR_SUCCESS;
    return true;
}

RemoveResult RemoveNetworkAdapter(const NetworkAdapterTarget& target) {
    RemoveResult result;
    result.target = target;

    std::wstring expectedGuid;
    if (target.deviceInstanceId.empty() ||
        target.deviceInstanceId.find_first_of(L"*?") != std::wstring::npos ||
        !NormalizeInterfaceGuid(target.interfaceGuid, &expectedGuid)) {
        result.status = RemoveStatus::InvalidTarget;
        result.error = ERROR_INVALID_PARAMETER;
        return result;
    }

    HDEVINFO deviceInfoSet = OpenNetworkDeviceSet();
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        result.status = RemoveStatus::Failed;
        result.error = GetLastError();
        return result;
    }

    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA deviceInfoData = {};
        deviceInfoData.cbSize = sizeof(deviceInfoData);
        if (!SetupDiEnumDeviceInfo(
                deviceInfoSet, index, &deviceInfoData)) {
            const DWORD enumerationError = GetLastError();
            if (enumerationError != ERROR_NO_MORE_ITEMS) {
                result.status = RemoveStatus::Failed;
                result.error = enumerationError;
            } else {
                result.status = RemoveStatus::NotFound;
                result.error = ERROR_NOT_FOUND;
            }
            SetupDiDestroyDeviceInfoList(deviceInfoSet);
            return result;
        }

        const std::wstring instanceId = GetInstanceId(
            deviceInfoSet, &deviceInfoData);
        if (instanceId.empty() ||
            _wcsicmp(
                instanceId.c_str(), target.deviceInstanceId.c_str()) != 0) {
            continue;
        }

        result.deviceName = GetDeviceName(
            deviceInfoSet, &deviceInfoData);

        DWORD guidError = ERROR_SUCCESS;
        if (!GetInterfaceGuid(
                deviceInfoSet,
                &deviceInfoData,
                &result.detectedInterfaceGuid,
                &guidError)) {
            result.status = RemoveStatus::InterfaceGuidUnavailable;
            result.error = guidError;
            SetupDiDestroyDeviceInfoList(deviceInfoSet);
            return result;
        }

        std::wstring actualGuid;
        if (!NormalizeInterfaceGuid(
                result.detectedInterfaceGuid, &actualGuid)) {
            result.status = RemoveStatus::InterfaceGuidUnavailable;
            result.error = ERROR_INVALID_DATA;
            SetupDiDestroyDeviceInfoList(deviceInfoSet);
            return result;
        }
        if (actualGuid != expectedGuid) {
            result.status = RemoveStatus::InterfaceGuidMismatch;
            result.error = ERROR_INVALID_DATA;
            SetupDiDestroyDeviceInfoList(deviceInfoSet);
            return result;
        }

        const bool removed = RemoveDevice(
            deviceInfoSet,
            &deviceInfoData,
            &result.rebootRequired,
            &result.error,
            &result.backend);
        result.status = removed
            ? RemoveStatus::Removed
            : RemoveStatus::Failed;
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return result;
    }
}

std::vector<RemoveResult> RemoveNetworkAdapters(
    const std::vector<NetworkAdapterTarget>& targets) {
    std::vector<RemoveResult> results;
    results.reserve(targets.size());
    for (const NetworkAdapterTarget& target : targets) {
        results.push_back(RemoveNetworkAdapter(target));
    }
    return results;
}

const wchar_t* RemoveStatusName(RemoveStatus status) {
    switch (status) {
        case RemoveStatus::Removed:
            return L"removed";
        case RemoveStatus::NotFound:
            return L"not found";
        case RemoveStatus::InvalidTarget:
            return L"invalid target";
        case RemoveStatus::InterfaceGuidUnavailable:
            return L"interface GUID unavailable";
        case RemoveStatus::InterfaceGuidMismatch:
            return L"interface GUID mismatch";
        case RemoveStatus::Failed:
            return L"failed";
    }
    return L"unknown";
}

}  // namespace netdevice

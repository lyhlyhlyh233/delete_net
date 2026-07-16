#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace netdevice {

struct NetworkAdapterTarget {
    std::wstring deviceInstanceId;
    std::wstring interfaceGuid;
};

struct NetworkAdapterInfo {
    std::wstring name;
    std::wstring deviceInstanceId;
    std::wstring interfaceGuid;
    DWORD interfaceGuidError = ERROR_SUCCESS;
};

enum class RemoveStatus {
    Removed,
    NotFound,
    InvalidTarget,
    InterfaceGuidUnavailable,
    InterfaceGuidMismatch,
    Failed
};

struct RemoveResult {
    NetworkAdapterTarget target;
    RemoveStatus status = RemoveStatus::Failed;
    std::wstring deviceName;
    std::wstring detectedInterfaceGuid;
    std::wstring backend;
    DWORD error = ERROR_SUCCESS;
    bool rebootRequired = false;
};

// Includes registered network adapters that are no longer physically present.
bool EnumerateNetworkAdapters(
    std::vector<NetworkAdapterInfo>* adapters,
    DWORD* error);

// Both fields are required. The devnode is removed only when its
// NetCfgInstanceId exactly matches interfaceGuid (case and braces ignored).
RemoveResult RemoveNetworkAdapter(const NetworkAdapterTarget& target);

// Every target is processed independently. Driver packages are not deleted.
std::vector<RemoveResult> RemoveNetworkAdapters(
    const std::vector<NetworkAdapterTarget>& targets);

const wchar_t* RemoveStatusName(RemoveStatus status);
std::wstring FormatWin32Error(DWORD error);

}  // namespace netdevice

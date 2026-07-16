# Remove Network Device

Removes Windows network-adapter device instances through the same PnP removal
path used by Device Manager. It removes devnodes, not driver packages.

Before removing an adapter, the library requires two matching identifiers:

- `deviceInstanceId`: the complete PnP device instance ID, such as
  `PCI\VEN_15AD&DEV_07B0&SUBSYS_...\...`.
- `interfaceGuid`: the adapter `InterfaceGuid` / `NetCfgInstanceId`, such as
  `{01234567-89AB-CDEF-0123-456789ABCDEF}`.

Do not pass the numeric `Win32_NetworkAdapter.DeviceID` value, an interface
index, or the network class GUID `{4D36E972-E325-11CE-BFC1-08002BE10318}`.

The program enumerates the network setup class without `DIGCF_PRESENT`, so it
can find stale VMware adapters that are no longer physically present after a
VMware-to-KVM migration.

## Safety behavior

- Device instance IDs are matched exactly and case-insensitively.
- Wildcards are rejected.
- The target GUID must be syntactically valid.
- The GUID is compared with `NetCfgInstanceId` read from the matched devnode's
  driver registry key.
- A missing or different GUID stops removal for that target.
- Driver Store packages are never deleted.
- Multiple targets are processed independently, so callers must inspect every
  returned `RemoveResult`.

## Compatibility

- Windows 7 / Windows Server 2008 R2 and later use `DiUninstallDevice`.
- Windows Vista / Windows Server 2008 fall back to
  `SetupDiCallClassInstaller(DIF_REMOVE)`.
- Client and Server editions use the same executable and API selection.
- Older Windows 10 builds that lack `pnputil /remove-device` are supported by
  the underlying API path.

The program resolves `DiUninstallDevice` dynamically so that the executable can
start on systems where that API is unavailable. Whether a binary built with a
specific MSVC toolset can start on an old Windows release still depends on that
toolset's target support.

Build and run an x64 executable on x64 Windows. Build and run an x86 executable
on x86 Windows. Run as Administrator or LocalSystem.

## Build

Open the matching MSVC Native Tools Command Prompt and run:

```bat
build-msvc.cmd
```

The build uses the static C/C++ runtime and links the Windows `Setupapi.lib`
and `Advapi32.lib` import libraries.

## Command-line usage

List all registered network adapter instances, including non-present devices:

```bat
remove-net-device.exe --list
```

The list includes the exact instance ID and GUID accepted by `--remove`.

Remove one adapter after checking both identifiers:

```bat
remove-net-device.exe --remove ^
  "PCI\VEN_15AD&DEV_07B0&SUBSYS_...\..." ^
  "{01234567-89AB-CDEF-0123-456789ABCDEF}"
```

Remove multiple adapters in one call by repeating the ID/GUID pair:

```bat
remove-net-device.exe --remove ^
  "PCI\VEN_15AD&DEV_07B0&SUBSYS_...\..." ^
  "{01234567-89AB-CDEF-0123-456789ABCDEF}" ^
  "PCI\VEN_15AD&DEV_07B0&SUBSYS_...\..." ^
  "{11111111-2222-3333-4444-555555555555}"
```

Each pair is checked and processed independently. A later mismatch does not
roll back an adapter already removed earlier in the same call.

## C++ API usage

Add `network_device_remover.cpp` and `network_device_remover.h` to the calling
project and link `Setupapi.lib` plus `Advapi32.lib`.

```cpp
#include "network_device_remover.h"

#include <vector>

std::vector<netdevice::RemoveResult> RemoveOldAdapters() {
    std::vector<netdevice::NetworkAdapterTarget> targets = {
        {
            L"PCI\\VEN_15AD&DEV_07B0&SUBSYS_...\\...",
            L"{01234567-89AB-CDEF-0123-456789ABCDEF}"
        },
        {
            L"PCI\\VEN_15AD&DEV_07B0&SUBSYS_...\\...",
            L"{11111111-2222-3333-4444-555555555555}"
        }
    };

    return netdevice::RemoveNetworkAdapters(targets);
}
```

Check each returned result:

```cpp
bool rebootRequired = false;
bool allRemoved = true;

for (const netdevice::RemoveResult& result : RemoveOldAdapters()) {
    if (result.status != netdevice::RemoveStatus::Removed) {
        allRemoved = false;
        // result.error contains the Win32 error code.
        continue;
    }
    rebootRequired = rebootRequired || result.rebootRequired;
}
```

`RemoveNetworkAdapter(target)` is available when the caller needs to process
only one adapter.

## Exit codes

- `0`: every requested adapter was removed; no reboot is required.
- `3010`: every requested adapter was removed; a reboot is required.
- `2`: invalid command line or invalid target value.
- `3`: at least one device instance was not found.
- `4`: enumeration/removal failed, or a devnode GUID could not be read.
- `5`: the supplied GUID did not match the target devnode.

Windows records device-uninstallation details in:

```text
C:\Windows\INF\setupapi.dev.log
```

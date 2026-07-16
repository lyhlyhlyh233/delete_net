# Windows 网卡设备实例删除工具

该工具通过与设备管理器相同的 PnP 卸载流程删除 Windows 网卡设备实例。
它只删除设备节点（devnode），不会删除 Driver Store 中的驱动包。

删除网卡前必须提供两个能够相互对应的标识：

- `deviceInstanceId`：完整的 PnP 设备实例 ID，例如
  `PCI\VEN_15AD&DEV_07B0&SUBSYS_...\...`。
- `interfaceGuid`：网卡的 `InterfaceGuid` 或 `NetCfgInstanceId`，例如
  `{01234567-89AB-CDEF-0123-456789ABCDEF}`。

不要传入纯数字形式的 `Win32_NetworkAdapter.DeviceID`、接口索引，或者网卡类
GUID `{4D36E972-E325-11CE-BFC1-08002BE10318}`。

程序枚举网络设备类时不会指定 `DIGCF_PRESENT`，因此可以找到 VMware 迁移到
KVM 后已经不在线的旧 VMware 网卡实例。

## 安全机制

- 设备实例 ID 按完整字符串精确匹配，比较时忽略大小写。
- 拒绝包含通配符的设备实例 ID。
- 目标 GUID 必须符合标准 GUID 格式。
- 程序会从匹配设备节点的驱动注册表项读取 `NetCfgInstanceId`。
- 读取到的 GUID 与调用方提供的 GUID 不一致时，停止删除该设备。
- 不调用驱动包删除接口，不会删除 Driver Store 中的驱动包。
- 多个目标独立处理，调用方必须检查每一个 `RemoveResult`。

## 系统兼容性

- Windows 7、Windows Server 2008 R2 及以上版本使用
  `DiUninstallDevice`。
- Windows Vista、Windows Server 2008 回退到
  `SetupDiCallClassInstaller(DIF_REMOVE)`。
- Windows 客户端版和 Server 版使用相同的可执行文件和接口选择逻辑。
- 对于不支持 `pnputil /remove-device` 的旧版 Windows 10，仍可使用底层
  Windows API 删除设备实例。

程序会动态加载 `DiUninstallDevice`，因此在没有该接口的旧系统上仍能启动并
回退到兼容接口。可执行文件能否在指定旧系统上启动，还取决于编译时所用 MSVC
工具集支持的最低目标系统版本。

x64 Windows 应使用 x64 版本，x86 Windows 应使用 x86 版本。程序必须以管理员
或 LocalSystem 权限运行。

## 编译

### 使用现有脚本

打开对应架构的 MSVC Native Tools Command Prompt，运行：

```bat
build-msvc.cmd
```

构建脚本使用静态 C/C++ 运行库，并链接 Windows 的 `Setupapi.lib` 与
`Advapi32.lib`。

### 使用 CMake

在 Visual Studio 开发者命令提示符中生成并编译 x64 Release 版本：

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

需要编译 x86 版本时，将 `-A x64` 改为 `-A Win32`。MSVC 构建同样使用静态
C/C++ 运行库；Visual Studio 多配置生成器的可执行文件位于
`build\Release\remove-net-device.exe`。

## 命令行用法

列出所有已注册的网卡实例，包括当前已经不在线的设备：

```bat
remove-net-device.exe --list
```

输出中的设备实例 ID 和接口 GUID 可以直接传给 `--remove`。

校验两个标识后删除一张网卡：

```bat
remove-net-device.exe --remove ^
  "PCI\VEN_15AD&DEV_07B0&SUBSYS_...\..." ^
  "{01234567-89AB-CDEF-0123-456789ABCDEF}"
```

重复传入 ID/GUID 参数对，可以一次处理多张网卡：

```bat
remove-net-device.exe --remove ^
  "PCI\VEN_15AD&DEV_07B0&SUBSYS_...\..." ^
  "{01234567-89AB-CDEF-0123-456789ABCDEF}" ^
  "PCI\VEN_15AD&DEV_07B0&SUBSYS_...\..." ^
  "{11111111-2222-3333-4444-555555555555}"
```

每一组参数都会独立校验和处理。一次调用中，后续参数校验失败不会回滚前面已经
成功删除的设备。

## C++ 接口用法

将 `network_device_remover.cpp` 和 `network_device_remover.h` 加入调用方工程，
并链接 `Setupapi.lib` 与 `Advapi32.lib`。

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

调用方需要逐项检查返回结果：

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

只需要处理一张网卡时，可以调用 `RemoveNetworkAdapter(target)`。

## 退出码

- `0`：所有目标都已删除，不需要重启。
- `3010`：所有目标都已删除，需要重启。
- `2`：命令行参数或目标参数无效。
- `3`：至少有一个设备实例未找到。
- `4`：枚举或删除失败，或者无法读取设备节点的接口 GUID。
- `5`：调用方提供的 GUID 与目标设备节点不匹配。

Windows 会把设备卸载的详细过程记录在：

```text
C:\Windows\INF\setupapi.dev.log
```

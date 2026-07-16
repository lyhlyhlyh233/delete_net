# Windows 网卡设备实例删除工具

这是一个精简的 Windows C++ 命令行工具，用完整的 PnP `DeviceInstanceId` 删除
网卡设备实例。核心代码集中在 `remove_net_device.cpp`，不足 100 行。

程序直接调用：

1. `SetupDiCreateDeviceInfoList`
2. `SetupDiOpenDeviceInfoW`
3. `DiUninstallDevice`

它删除的是设备节点（devnode），不会删除 Driver Store 中的驱动包。

## 输入

命令行参数全部是待删除网卡的完整 PnP 设备实例 ID：

```bat
remove-net-device.exe ^
  "PCI\VEN_15AD&DEV_07B0&SUBSYS_...\..." ^
  "PCI\VEN_15AD&DEV_07B0&SUBSYS_...\..."
```

不再需要接口 GUID，也不再使用 `--remove` 参数。

这里的 ID 必须是完整 `DeviceInstanceId`，例如：

```text
PCI\VEN_15AD&DEV_07B0&SUBSYS_07B015AD&REV_01\4&12345678&0&0088
```

不要传入纯数字形式的 `Win32_NetworkAdapter.DeviceID`，也不要传入 Hardware ID。

## 注意事项

- 程序只拒绝空 ID，不再校验 GUID、设备属性或 ID 格式。
- 程序不会再次判断目标是不是网卡，请只传入已经确认的网卡设备实例 ID。
- 每个 ID 独立处理；某个 ID 失败后，程序会继续处理后续 ID。
- 必须以管理员或 LocalSystem 权限运行。
- 支持 Windows 7、Windows Server 2008 R2 及以上版本。
- 不再兼容 Windows Vista 和 Windows Server 2008 的旧卸载接口。

## 编译

### MSVC 脚本

在对应架构的 MSVC Native Tools Command Prompt 中运行：

```bat
build-msvc.cmd
```

### CMake

生成并编译 x64 Release 版本：

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

编译 x86 时将 `-A x64` 改为 `-A Win32`。

## 输出与退出码

成功时只输出：

```text
OK: <DeviceInstanceId>
```

失败时只输出 Win32 错误码：

```text
FAILED <ErrorCode>: <DeviceInstanceId>
```

- `0`：全部删除成功，不需要重启。
- `3010`：全部删除成功，需要重启。
- `1`：至少一个设备删除失败。
- `2`：没有传入设备实例 ID。

详细的设备卸载日志仍可在以下文件中查看：

```text
C:\Windows\INF\setupapi.dev.log
```

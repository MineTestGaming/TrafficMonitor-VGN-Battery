# TrafficMonitor VGN / VXE Battery

TrafficMonitor **1.86 x64 / API 7** 的键鼠电量插件。任务栏提供“键盘”和“鼠标”两个独立项目，悬浮提示与插件“选项”显示设备 Metadata。

本机已安装到 `C:\TrafficMonitor\plugins`，重启并保留原有显示项后启用了两项电量；用户已确认任务栏显示正常。

## 本机实测

2026-11-25，通过接收器直接发送只读查询，无需启动 V-HUB：

| 设备 | 实际接收器 | 已验证字段 |
| --- | --- | --- |
| VGN N75 PRO | `320F:5088`，FF1C:0092，64 字节 input/output | 原始电量百分比、充电标记、制造商、USB 标识与描述符版本 |
| VXE R1 SE+ | `3554:F58E`，FF02:0002，17 字节 input/output | 电量、充电、CID/MID、固件原始字节、回报率、DPI 档位编码 |

实测快照键盘约 56–57%、鼠标 20%，均未充电；电量会随使用变化。鼠标 `CID=2/MID=31`，回报率编码 `1` 对应 `1000 Hz`，DPI 档位编码 `2`。响应中电压位置的字节可拼成 `3548`，但它在本次声明的有效数据范围之外，且单位未确认，因此插件不把它当作有效电压展示。DPI 档位编码不等于实际 DPI 数值。固件 `03 00` 保留原始表示，不伪造语义版本号。

完整厂商协议及证据见 [协议分析](docs/protocol-analysis.md)。BLE 路径支持标准 `180F/2A19` 的 Uncached 电量读取及设备实际提供的 `180A` 字段；本次实际连接是 2.4G，**没有验证蓝牙实时电量成功返回**。Windows 旧配对记录中的 81%/20% 不作为当前电量。

## 安装

解压发布包，把以下两个文件放进 TrafficMonitor 主程序旁的 `plugins` 文件夹：

- `VgnVxeBattery.dll`
- `BatteryCollector.exe`

重新启动 TrafficMonitor，在“其他功能 → 插件管理”确认 `VGN / VXE Battery`，在“显示项目”中勾选“VGN N75 PRO 键盘电量”和“VXE R1SE 鼠标电量”。

手动刷新有两个入口：右键菜单中的 `VGN / VXE Battery → 立即刷新`，以及“插件管理 → 选项”窗口中的“立即刷新”按钮。设置窗口会实时更新电量和采集状态，并可展开设备详情。点击后在下一次监控轮询中提前采集；若正在采集，连续请求合并为完成后的一次额外采集，保留仍有效的电量显示。

可用 PowerShell 执行 `./scripts/install.ps1 -TrafficMonitorDirectory 'C:\TrafficMonitor'` 复制构建结果；脚本不结束正在运行的程序、不修改现有显示项目。

## 行为和范围

- 每 30 秒采集一次，有效期 120 秒。未知、断连、非法或过期电量显示 `--`，真实空电量显示 `0%`。
- 充电标记由设备报告决定：充电时以 `+` 替换 `%`（如 `59+`），停止充电后恢复 `59%`。两台设备插电后的实际充电标记均已验证。
- 优先使用本次验证的两种 2.4G 接收器；没有匹配接收器时按名称搜索已配对 BLE 设备。不硬编码本机地址。
- 当前原生 HID 后端仅启用上述两种已验证的接收器与型号；其他 PID、有线模式没有宣称支持。
- 设备休眠或接收器忙时可能超时，自动重试。V-HUB 与插件同时查询是否竞争仍需实际观察。
- 查询不会重配对、写按键/灯光/DPI 设置或升级固件。HID 的 WriteFile 用于发出读取命令，并不代表修改设备配置。
- DLL 不建立采集线程。I/O 在短命 helper 中运行，Job Object 在 DLL 卸载或宿主退出时终止自己的 helper；helper 另有 25 秒 watchdog，DLL 30 秒兜底。BLE 先完成电量并保存检查点，再尽力读取可选信息。
- 临时 UTF-16 INI 位于当前用户临时目录；包含设备地址和可选序列号，卸载 DLL 时删除。仓库不包含厂商 ASAR 或用户配置。

## 构建与验证

安装 Visual Studio 的 C++ 桌面开发工具和 Windows SDK（需 C++/WinRT），以及 CMake 3.24+。

```powershell
./scripts/build.ps1
ctest --test-dir build -C Release --output-on-failure
./build/Release/BatteryCollector.exe --output "$PWD/device-snapshot.ini"
./build/Release/plugin_smoke.exe "$PWD/build/Release/VgnVxeBattery.dll" --live
./build/Release/hid_inventory.exe
```

`--live` 要求两台设备在本次 DLL→helper→设备完整链路中得到有效百分比。普通 CTest 验证状态语义、真实报文解析、官方 ABI、并发 getter、反复加载卸载；不会把缺失设备当作 0%。`hid_inventory` 仅枚举相关 VID 的 HID 描述符，不发送设备命令。

测试结果与实际宿主验收状态记录在 [验证记录](docs/validation.md)。

## 接口来源与许可证

插件接口原样固定为 [TrafficMonitor V1.86 PluginInterface.h](https://raw.githubusercontent.com/zhongyang219/TrafficMonitor/V1.86/include/PluginInterface.h)，参照[官方插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/插件开发指南)。上游头文件版权归 Zhong Yang；其许可证随附于 `third_party/TrafficMonitor-LICENSE`。本项目使用同样的 Anti 996 License，见 `LICENSE`。不包含 V-HUB 实现代码，协议实现依据本机静态分析和设备响应独立编写。

# VGN/VXE Battery 插件设计

用户于 2026-11-25 批准：TrafficMonitor 1.86 x64 插件，键盘和鼠标各一个电量显示项；tooltip 展示设备信息、来源和新鲜度；优先直接读取 BLE，厂商字段仅在验证后启用。

## 结构
- 用户随后确认当前使用 2.4G，已实测并优先启用 N75 的 Evision 和 R1 SE+ 的 Compx 只读 HID 通路。仅当未发现匹配 HID collection 时尝试 BLE；接收器存在但设备不回应时保留对应 HID 状态。
- 使用官方 V1.86 API 7 头文件和 MSVC C++ ABI。
- DLL 的 DataRequired 只轮询短命 helper 的进程和结果文件，getter 只返回线程局部副本，无后台 DLL 线程或异步回调。
- helper 使用 C++/WinRT 读取已配对的 VGN N75 PRO / VXE R1SE 系列。BLE Battery Service 180F / Battery Level 2A19 使用 Uncached 读取；Device Information 180A 仅读取标准只读字符串与 PnP ID。
- 通过设备名称自动匹配，不把本机蓝牙地址写入源码。未知或断连为 --，过期为 --，有效 0% 与未知区分。
- helper 单次受时间限制；DLL 在超时后结束自己创建的 helper。helper 使用父进程退出时关闭的 Job Object，防止孤儿进程。
- PnP 缓存电量仅作为诊断信息，不能伪装为实时值。标准 BLE 不提供充电/DPI/回报率时显示不提供，不猜测。

## 验证
电量范围、断连、过期、0%、helper 超时、ABI 加载卸载测试；本机 helper 实读；编译导出检查；真实 TrafficMonitor 验证须说明加载和渲染状态。

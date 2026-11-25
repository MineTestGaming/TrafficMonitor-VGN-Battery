# VGN/VXE Battery Implementation Plan

> For agentic workers: execute tasks in this session and verify each deliverable.

**Goal:** 交付本机可用的键鼠电量插件及可追溯的 Metadata 调查。
**Architecture:** MSVC x64 DLL + 隔离 BLE helper。UI 仅使用缓存，采集进程有超时。
**Tech Stack:** C++17, Windows SDK C++/WinRT, CMake, CTest。
**Spec:** docs/design.md（用户已批准聊天中的同一设计）

## Global Constraints
API 7；保留官方 ABI；无设备设置写操作；不把旧缓存标为实时；无常驻 DLL 线程。

## Task 1: 显示与状态契约
- [x] tests/state_tests.cpp 验证 0、100、非法百分比、断连、过期和有效值；先运行失败再实现 src/state.h。
- [x] 使用 CMake/CTest 编译并运行。

## Task 2: BLE 采集器
- [x] 用户确认实际使用 2.4G 后，增加只读 Evision/Compx HID 后端；核对原生 collection、Report 长度、型号、校验和、状态码和有效数据长度，实读两台设备。
- [x] src/collector.cpp 按名称匹配配对设备，读取连接状态和 Uncached 2A19；读取 180A 字段，写出 INI 快照。
- [x] 使用带超时的异步操作和进程级隔离，单独运行并检查本机返回值。

## Task 3: TrafficMonitor DLL
- [x] third_party/PluginInterface.h 固定官方 V1.86；src/plugin.cpp 实现双显示项、缓存快照、tooltip 和短命 helper 管理。
- [x] tests/plugin_smoke.cpp 使用 LoadLibrary 验证 ABI、边界、getter 并发、反复卸载以及 helper 超时。

## Task 4: 交付
- [x] scripts/build.ps1、scripts/install.ps1、README.md、docs/protocol-analysis.md 提供构建安装与字段范围。
- [x] Release 构建、四项 CTest、实际采集、真实宿主安装重启，以及用户确认任务栏渲染；明确未验证的连接方式。

## 后续请求：手动刷新与提交
- [x] 用户指定右键菜单/插件设置提供手动刷新；使用 API 7 命令和设置按钮共用刷新请求。
- [x] 采集中请求合并为一次追加采集，不重置仍有效的电量；增加确定性采集器集成测试。
- [x] 独立代码审查未发现阻止提交的问题；提交范围排除厂商包、运行快照、用户配置和构建产物。

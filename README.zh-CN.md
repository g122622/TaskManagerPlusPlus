# TaskManagerPlusPlus

[English](README.md) | [简体中文](README.zh-CN.md)

一个采用 **C++20** 与 **WinUI 3** 开发的现代化 Windows 任务管理器替代品。

本项目旨在做到轻量、快速，并在**界面与交互上 1:1 复刻 Windows 11 任务管理器**，
同时在三个方面做出实质性改进：图表颜色自定义、性能指标采集性能、代码可维护性。

> **项目状态：里程碑 M1 已完成，M2 起步。**
> 应用已具备进程、性能与详细信息页。CPU 与内存走原生 API；磁盘、网络、GPU 取自
> Windows 性能计数器与 DXGI；内存条信息取自固件的 SMBIOS 表。性能页每个指标都可
> 配置颜色与线宽，每块磁盘与每个网卡各有独立图表；双击侧栏可收起为仅列表的紧凑
> 窗口。268 个单测通过，Debug 与 Release 均可从干净克隆构建并自包含运行。

## 主要特性

- **忠于 Windows 11** —— 布局、间距、配色、图标、交互均与系统任务管理器一致，
  用户无需重新学习。
- **原生指标采集** —— 所有计数器均来自 Windows 原生 API，走高性能路径；
  热路径上不使用 WMI。
- **界面永不卡顿** —— 采样在后台线程执行，发布不可变的版本化快照，
  UI 线程不会等待采集完成。
- **图表颜色可定制** —— 逐指标配色（使用系统颜色选择器）、可调线宽、
  浅色/深色/跟随系统主题。
- **逐设备图表** —— 每块磁盘与每个网卡各有独立一行与独立历史；读/接收为实线，
  写/发送为虚线，共用同一坐标轴。
- **硬件清单** —— 内存插槽、代数、频率、位宽与型号，取自任务管理器本身不提供的
  固件表。
- **紧凑模式** —— 双击侧栏收起为仅列表的窗口。
- **记住上次状态** —— 窗口位置与尺寸、两个侧栏宽度、导航栏展开状态，以及启动页。

尚未实现（见 [docs/ROADMAP.md](docs/ROADMAP.md)）：逐进程网络计数器（需提权）、
NVIDIA 显卡的 NVML 温度与功耗，以及中英文语言切换。

## 环境要求

| 组件 | 版本 |
| --- | --- |
| 操作系统 | Windows 11 或更高 |
| 架构 | x64 |
| Visual Studio | 2026（MSVC v145 工具集） |
| Windows SDK | 10.0.26100.0 |
| Windows App SDK | 1.8 |
| vcpkg | 用于第三方依赖 |

## 构建

```powershell
# Debug，并运行测试
tools\build.bat Debug test

# Release
tools\build.bat Release
```

`tools\build.bat` 会自行定位 Visual Studio 与 vcpkg，在首次构建时安装 vcpkg
依赖，还原 NuGet 包并构建解决方案。产物位于 `build\x64\<Configuration>\`。

Visual Studio 通过 `vswhere` 查找，找不到时回退到一组常见路径；两者都未命中
可将 `VSBASE` 设为安装目录。vcpkg 优先取 `VCPKG_ROOT`，手动安装依赖同样用它：
`tools\build.bat <配置> deps`。

环境搭建与疑难排查见 [docs/BUILD.md](docs/BUILD.md)。

## 运行

```powershell
cd build\x64\Debug
.\tmpp.exe
```

> 本程序**必须运行在交互式桌面会话**中。WinUI 3 无法在服务窗口站
> （例如 Session 0）上初始化，会在任何应用代码执行之前于运行时内部 fail-fast。
>
> 这也意味着单实例检查无法从服务会话中验证：`Local\` 命名互斥体与
> `FindWindow` 都按设计限定在单个会话内，从 Session 0 启动两次不会互相看见。

## 文档

| 文档 | 内容 |
| --- | --- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 分层模型、依赖规则、采样管线 |
| [docs/BUILD.md](docs/BUILD.md) | 环境搭建、构建命令、疑难排查 |
| [docs/METRICS.md](docs/METRICS.md) | 指标与原生 API 的映射及性能约束 |
| [docs/CODE_CONVENTIONS.md](docs/CODE_CONVENTIONS.md) | 命名、格式、日志、错误处理规范 |
| [docs/ROADMAP.md](docs/ROADMAP.md) | 里程碑与暂缓特性 |

## 许可证

[MIT](LICENSE)

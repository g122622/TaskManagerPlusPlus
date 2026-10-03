# TaskManagerPlusPlus

[English](README.md) | [简体中文](README.zh-CN.md)

一个采用 **C++20** 与 **WinUI 3** 开发的现代化 Windows 任务管理器替代品。

本项目旨在做到轻量、快速，并在**界面与交互上 1:1 复刻 Windows 11 任务管理器**，
同时在三个方面做出实质性改进：图表颜色自定义、性能指标采集性能、代码可维护性。

> **项目状态：早期开发中（M1-0 已完成）。**
> 构建链路已端到端验证通过，应用窗口可正常启动。功能开发尚未开始。

## 主要特性

- **忠于 Windows 11** —— 布局、间距、配色、图标、交互均与系统任务管理器一致，
  用户无需重新学习。
- **原生指标采集** —— 所有计数器均来自 Windows 原生 API，走高性能路径；
  热路径上不使用 WMI。
- **界面永不卡顿** —— 采样在后台线程执行，发布不可变的版本化快照，
  UI 线程不会等待采集完成。
- **图表颜色可定制** —— 支持逐指标调色与预设主题。
- **中英双语** —— 支持简体中文与 English。

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
# Debug
tools\m1build.bat Debug

# Release
tools\m1build.bat Release
```

该脚本会配置 MSVC 环境、还原 NuGet 包并构建解决方案。
产物位于 `build\x64\<Configuration>\`。

环境搭建与疑难排查见 [docs/BUILD.md](docs/BUILD.md)。

## 运行

```powershell
cd build\x64\Debug
.\tmpp.exe
```

> 本程序**必须运行在交互式桌面会话**中。WinUI 3 无法在服务窗口站
> （例如 Session 0）上初始化，会在任何应用代码执行之前于运行时内部 fail-fast。

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

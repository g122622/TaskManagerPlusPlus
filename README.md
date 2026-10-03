# TaskManagerPlusPlus

[English](README.md) | [简体中文](README.zh-CN.md)

A modern replacement for the Windows Task Manager, built with **C++20** and **WinUI 3**.

TaskManagerPlusPlus aims to be lightweight, fast, and visually faithful to the
Windows 11 Task Manager, while improving on it where it matters most: chart
colour customisation, metric collection performance, and code maintainability.

> **Project status: early development (M1-0 complete).**
> The build chain is verified end to end and the application window launches.
> Feature work has not started yet.

## Highlights

- **Faithful to Windows 11** — layout, spacing, colour, icons and interaction
  mirror the built-in Task Manager, so there is nothing new to learn.
- **Native metric collection** — every counter comes from native Windows APIs on
  a high-performance path. No WMI on the hot path.
- **Non-blocking UI** — sampling runs on background threads and publishes
  immutable versioned snapshots, so the UI never waits on a probe.
- **Customisable charts** — per-metric colours plus preset themes.
- **Bilingual** — Simplified Chinese and English.

## Requirements

| Component | Version |
| --- | --- |
| Operating system | Windows 11 or later |
| Architecture | x64 |
| Visual Studio | 2026 (MSVC v145 toolset) |
| Windows SDK | 10.0.26100.0 |
| Windows App SDK | 1.8 |
| vcpkg | For third-party dependencies |

## Building

```powershell
# Debug
tools\m1build.bat Debug

# Release
tools\m1build.bat Release
```

The script sets up the MSVC environment, restores NuGet packages, and builds the
solution. Output lands in `build\x64\<Configuration>\`.

See [docs/BUILD.md](docs/BUILD.md) for environment setup and troubleshooting.

## Running

```powershell
cd build\x64\Debug
.\tmpp.exe
```

> The application must run in an **interactive desktop session**. WinUI 3 cannot
> initialise on a service window station (for example, Session 0), where it
> fail-fasts inside the runtime before any application code executes.

## Documentation

| Document | Scope |
| --- | --- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layer model, dependency rules, sampling pipeline |
| [docs/BUILD.md](docs/BUILD.md) | Environment setup, build commands, troubleshooting |
| [docs/METRICS.md](docs/METRICS.md) | Metric-to-native-API mapping and performance constraints |
| [docs/CODE_CONVENTIONS.md](docs/CODE_CONVENTIONS.md) | Naming, formatting, logging, error handling |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Milestones and deferred features |

## License

[MIT](LICENSE)

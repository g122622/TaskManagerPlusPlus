# TaskManagerPlusPlus

[English](README.md) | [简体中文](README.zh-CN.md)

A modern replacement for the Windows Task Manager, built with **C++20** and **WinUI 3**.

TaskManagerPlusPlus aims to be lightweight, fast, and visually faithful to the
Windows 11 Task Manager, while improving on it where it matters most: chart
colour customisation, metric collection performance, and code maintainability.

> **Project status: milestone M1 complete, M2 starting.**
> The application runs with process, performance and details pages. CPU and memory
> are read natively; disk, network and GPU come from the Windows performance
> counters and DXGI; memory modules come from the firmware's SMBIOS table. Every
> metric on the performance page is plotted with configurable colours and stroke
> widths, each disk and adapter has its own chart, and double-clicking the sidebar
> collapses the window to the list alone. 324 unit tests pass, and all three
> configurations build; the release is packaged as a self-contained folder that
> needs nothing installed on the target machine.

## Highlights

- **Faithful to Windows 11** — layout, spacing, colour, icons and interaction
  mirror the built-in Task Manager, so there is nothing new to learn.
- **Native metric collection** — every counter comes from native Windows APIs on
  a high-performance path. No WMI on the hot path, and no administrator rights
  required.
- **Non-blocking UI** — sampling runs on background threads and publishes
  immutable versioned snapshots, so the UI never waits on a probe.
- **Customisable charts** — per-metric colours chosen with the system colour
  picker, a configurable stroke width, and light, dark or system theme.
- **Per-device charts** — every disk and network adapter gets its own row and its
  own history, with reads or received traffic solid and writes or sent traffic
  dashed on one shared axis.
- **Hardware inventory** — memory slots, module type, speed, bus width and part
  numbers, read from the firmware table that no Windows counter exposes.
- **Compact mode** — double-click the sidebar to collapse the window to the list.
- **Remembers where you were** — window position and size, both sidebar widths,
  the navigation rail's state, and which page to open on.

Planned but not yet implemented: per-process network counters (needs elevation),
NVML temperature and power figures for NVIDIA adapters, and a Chinese/English
language switch. See [docs/ROADMAP.md](docs/ROADMAP.md).

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
# Debug, then run the tests
tools\build.bat Debug test

# Release
tools\build.bat Release
```

`tools\build.bat` locates Visual Studio and vcpkg itself, installs the vcpkg
dependencies on a first build, restores the NuGet packages and builds the solution.
Output lands in `build\x64\<Configuration>\`.

Visual Studio is found through `vswhere`, with a short list of conventional paths as
a fallback. If neither finds it, set `VSBASE` to the installation directory. vcpkg is
taken from `VCPKG_ROOT` when that is set, which is also what `build.bat <cfg> deps`
uses to install the dependencies by hand.

See [docs/BUILD.md](docs/BUILD.md) for environment setup and troubleshooting.

## Releasing

```powershell
# Build Release, then assemble dist\TaskManagerPlusPlus-<version>-win-x64 and its zip
tools\package.ps1 -Build
```

The build output directory is not a release: it also holds the static libraries, the
test binary and gmock. The packaging script copies only what runs, puts the Visual C++
runtime next to the executable so that nothing has to be installed, verifies that the
result is complete, and zips it.

Two configurations matter here. `Release` is built without any debug information —
no PDB, no debug directory — and `RelWithDebInfo` is the one that keeps symbols, for
profiling and crash dumps.

A WinUI 3 native application cannot be collapsed into a single executable: there is no
static `Microsoft.UI.Xaml*.lib`, and Microsoft's `PublishSingleFile` is a .NET model.
The deliverable is therefore a clean folder and a zip. See
[docs/BUILD.md](docs/BUILD.md) for what the package contains and how it is checked.

## Running

```powershell
cd build\x64\Debug
.\tmpp.exe
```

Or run the packaged build, which is the same executable with its runtime beside it:

```powershell
dist\TaskManagerPlusPlus-0.1.0-win-x64\tmpp.exe
```

> The application must run in an **interactive desktop session**. WinUI 3 cannot
> initialise on a service window station (for example, Session 0), where it
> fail-fasts inside the runtime before any application code executes.
>
> This also means its single-instance check cannot be exercised from a service
> session: `Local\` named mutexes and `FindWindow` are both scoped to a session by
> design, so two launches from Session 0 will not see each other.

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

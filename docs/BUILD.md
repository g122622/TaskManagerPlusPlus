# 构建指南

本文档说明环境搭建、构建命令，以及本机已诊断出的各类问题。

## 1. 环境要求

| 组件 | 使用版本 | 说明 |
| --- | --- | --- |
| Windows | 11（build 26200 或更高） | WinUI 3 的目标特性集需要 Windows 11 |
| Visual Studio | 2026 Community | 安装路径 `D:\Program Files\Microsoft Visual Studio\18\Community` |
| MSVC 工具集 | v145（14.51.36231） | 随 VS 2026 提供 |
| Windows SDK | 10.0.26100.0 | 必须安装；工程已锁定此版本 |
| Windows App SDK | 1.8.260921001 | 经 NuGet 还原，不依赖系统级安装 |
| vcpkg | `E:\vcpkg` | 第三方依赖 |

### 所需的 Visual Studio 工作负载

- **使用 C++ 的桌面开发**

> **本机缺失两个组件**，它们会影响 WinUI 构建。当前的纯代码 UI 方案不依赖它们，
> 但若将来需要 XAML 支持则必须补装（见第 4 节）：
>
> - `MSBuild\Microsoft\WindowsXaml\` —— 原生 C++ XAML 构建支持。
> - `MSBuild\Microsoft\VC\v180\Application Type\Windows Store\` —— Windows Store 应用程序类型。

## 2. 构建

```powershell
# 安装第三方依赖（首次，或改动 vcpkg.json 之后）
tools\m1build.bat Debug deps

# Debug
tools\m1build.bat Debug

# Release
tools\m1build.bat Release
```

`tools\m1build.bat` 依次执行：

1. 调用 `tools\vsenv.bat` 建立 VS 开发环境（经 `VsDevCmd.bat`）并设置 vcpkg 根目录。
2. `MSBuild /t:Restore` 还原 NuGet 包。
3. `MSBuild` 构建解决方案。

产物：`build\x64\<Configuration>\`，含 `tmpp.exe` 与 `tmpp_tests.exe`。

### 运行单元测试

```powershell
build\x64\Debug\tmpp_tests.exe
# 只看失败摘要
build\x64\Debug\tmpp_tests.exe --gtest_brief=1
# 只跑某个套件
build\x64\Debug\tmpp_tests.exe --gtest_filter=WindowsProcessProbeTest.*
```

### 通过 Visual Studio 构建

打开 `TaskManagerPlusPlus.sln` 直接构建。Debug 与 Release 均只配置了 x64。

## 3. 疑难排查

以下问题均在 M1-0 阶段遇到并已修复。之所以记录在此，是因为每一个的报错信息
都未能指向其真正原因。

### 3.1 `The BaseOutputPath/OutputPath property is not set`

**原因。** `Microsoft.CppCommon.Targets` 无条件地把 `OutputPath` 赋值为 `OutDir`，
而 `OutDir` 仅由 `$(SolutionDir)` 推导；从裸命令行构建时该值为空。

**修复。** 在 `.vcxproj` 中显式设置 `OutDir` 与 `IntDir`。

### 3.2 `MSB8036: The Windows SDK version 10.0.17134.0 (or later) was not found`

**原因。** `WindowsTargetPlatformVersion` 被设为 `10.0`，即"最新已安装"别名。
解析该别名依赖注册表，在裸命令行下解析失败，导致 `TargetPlatformVersion`
回退到托管默认值 `7.0`。

**修复。** 将 `WindowsTargetPlatformVersion` 锁定为 `10.0.26100.0`。

### 3.3 工具集从未被导入（静默，且引发连锁失败）

**原因。** 将 `ApplicationType` 设为 `Windows Store` 会让 MSBuild 到
`MSBuild\Microsoft\VC\v180\Application Type\Windows Store\10.0\Platforms\x64\...`
查找工具集。本机不存在该目录，于是 `_ToolsetFound` 保持为空，整条工具集导入链被跳过。
其下游表现为令人困惑的 SDK 错误，而不是"工具集缺失"。

**修复。** 不设置 `ApplicationType` / `ApplicationTypeRevision`。
WinUI 3 桌面应用只需 `UseWinUI` 与 `DesktopCompatible`。

### 3.4 `C1076: compiler limit: internal heap limit reached`

**原因。** WinUI 3 投影头模板极重。覆盖面足够广的预编译头增长到 1.1 GB，
耗尽编译器内部堆。

**修复。** 工程**不使用预编译头**。`WinRT.h` 是普通共享头文件，
并用 `/Zm500` 提高编译器堆上限。

### 3.5 vcpkg：`Unable to find a valid Visual Studio instance`（已解决）

**现象。** 直接调用 `vcvars64.bat` 后执行 `vcpkg install`，报：

```
error: in triplet x64-windows: Unable to find a valid Visual Studio instance
Could not locate a complete Visual Studio instance
```

**根因。** 本机的 Visual Studio **未向 Visual Studio Installer 注册**：
`vswhere` 返回 0 个实例，且
`HKLM\SOFTWARE\Microsoft\VisualStudio\Setup\Instances` 与
`HKLM\SOFTWARE\WOW6432Node\...\Instances` 均不存在。

**解决方案（参考 Cubium 项目）。** 关键在于**用 `VsDevCmd.bat` 而非 `vcvars64.bat`
建立完整开发环境**。vcpkg 的编译器探测使用 **Ninja 生成器**
（`CMAKE_GENERATOR=Ninja`），它读取 `INCLUDE` / `LIB` / `WindowsSdkDir` 等环境变量，
**不查询注册的 VS 实例**。`VsDevCmd.bat` 会完整设置这些变量，`vcvars64.bat` 则不足以
让 vcpkg 完成探测。

`tools\vsenv.bat` 已封装该逻辑：

```bat
call "D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 -no_logo
set "VCPKG_ROOT=E:\vcpkg"
set "VCPKG_DEFAULT_BINARY_CACHE=E:\vcpkg\binary-cache"
```

安装依赖：

```powershell
tools\m1build.bat Debug deps
```

依赖会安装到**清单模式**位置 `TaskManagerPlusPlus\vcpkg_installed\x64-windows\`。

### 3.6 vcpkg 清单模式路径重复（已绕过）

**现象。** 启用清单模式后，MSBuild 把 include 路径解析成
`vcpkg_installed\x64-windows\x64-windows\include`（三元组出现两次），
导致 `Cannot open include file: 'spdlog/spdlog.h'`。

**根因。** 本机 vcpkg 版本中 `vcpkg.targets` 的一个缺陷：

- 第 79 行推导 `_ZVcpkgInstalledDir` 时**已包含** `$(VcpkgTriplet)`：
  `$(_ZVcpkgManifestRoot)vcpkg_installed\$(VcpkgTriplet)\`
- 第 90 行又在拼接 `_ZVcpkgCurrentInstalledDir` 时**再次追加** `$(VcpkgTriplet)`

**规避措施。** 在 vcxproj 中显式给出 `VcpkgInstalledDir`，跳过产生重复的那次推导：

```xml
<PropertyGroup>
  <VcpkgEnableManifest>true</VcpkgEnableManifest>
  <VcpkgManifestInstall>false</VcpkgManifestInstall>
  <VcpkgInstalledDir>$(MSBuildThisFileDirectory)..\..\vcpkg_installed\</VcpkgInstalledDir>
</PropertyGroup>
```

> **说明**：MSBuild 的 vcpkg 集成默认是**经典模式**（`VcpkgEnableManifest=false`），
> 只查 `<vcpkg-root>\installed`。必须显式开启清单模式，它才会消费
> `<repo>\vcpkg_installed\<triplet>`。

## 4. XAML 支持

本应用**以 C++ 代码构建 UI**，不编译任何 `.xaml` 页面。

这是针对组件缺失的刻意选择，而非偏好：把 XAML 编译器接入**原生 C++** 构建的文件

```
MSBuild\Microsoft\WindowsXaml\v18.0\Microsoft.Windows.UI.Xaml.Cpp.targets
```

在本机上不存在，其所在的整个 `WindowsXaml` 目录也不存在。NuGet 包自身的注释
证实了这种职责划分 —— `Microsoft.UI.Xaml.Markup.Compiler.interop.targets` 中写道：

> `$(PrepareResourcesDependsOn)` is used only for the Managed build.
> See `Microsoft.Windows.UI.Xaml.Cpp.Targets` for Native build rules.

缺少该文件时，XAML 编译步骤永远不会被挂载到 C++ 构建中，因此不会产生 `.xbf`，
也不会产生 `*.xaml.g.h`。

若要恢复 XAML 支持，需重新运行 Visual Studio Installer 并添加 C++ XAML 组件。
注意：由于 VS 处于未注册状态，安装器的**修复**功能可能不可用，可能需要完整重装。

## 5. 运行

```powershell
cd build\x64\Debug
.\tmpp.exe
```

### 本程序必须运行在交互式桌面会话中

WinUI 3 在**交互式窗口站**（`WinSta0`）上初始化其合成器与输入系统。
在服务窗口站上，它会在**任何应用代码执行之前**于运行时内部 fail-fast：

| 部署模式 | 退出码 | 故障模块 |
| --- | --- | --- |
| 自包含 | `0xC0000602` `FAIL_FAST` | `Microsoft.UI.Input.dll` |
| 框架依赖 | `0xC000027B` `STATUS_STOWED_EXCEPTION` | `Microsoft.UI.Xaml.dll` |

在自动化环境（例如 CI 代理或 Session 0）中的症状表现：

```
SessionId     = 0
WindowStation = Service-0x0-...$
```

**这不是应用缺陷。** 可用以下命令确认窗口站：

```powershell
query session
```

正常运行时进程位于 `console` 会话，而非 `services`。

## 6. 验证构建

```powershell
# 构建两个配置
tools\m1build.bat Debug
tools\m1build.bat Release

# 确认可执行文件存在
Test-Path build\x64\Debug\tmpp.exe
```

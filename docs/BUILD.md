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

# Release（发布用；不含任何调试信息）
tools\m1build.bat Release

# RelWithDebInfo（全优化 + 调试信息，用于崩溃转储与性能分析）
tools\m1build.bat RelWithDebInfo
```

### 三个配置的分工

| 配置 | 调试信息 | PDB | 用途 |
| --- | --- | --- | --- |
| `Debug` | `ProgramDatabase` | 有 | 日常开发与单测 |
| `Release` | **`None`** | **无** | **发布** |
| `RelWithDebInfo` | `ProgramDatabase` | 有 | 需要符号的剖析、崩溃分析 |

> **`Release` 是真正的发布配置**：`ClCompile` 的 `DebugInformationFormat` 被覆盖为
> `None`，链接器也设 `GenerateDebugInformation=false`，因此既不产出 PDB，也不在镜像里
> 留下调试目录。在本次调整之前，`Release` 实际上是"带调试信息的优化构建"，
> 那个角色现由 `RelWithDebInfo` 承担。

`tools\m1build.bat` 依次执行：

1. 调用 `tools\vsenv.bat` 建立 VS 开发环境（经 `VsDevCmd.bat`）并设置 vcpkg 根目录。
2. `MSBuild /t:Restore` 还原 NuGet 包。
3. `MSBuild` 构建解决方案。

产物：`build\x64\<Configuration>\`，含 `tmpp.exe` 与 `tmpp_tests.exe`。
该目录**不是**发布包——它同时含有静态库、测试程序与 gmock，Release 之外还含 PDB。
发布包由 `tools\package.ps1` 生成，见第 7 节。

### 运行单元测试

```powershell
build\x64\Debug\tmpp_tests.exe
# 只看失败摘要
build\x64\Debug\tmpp_tests.exe --gtest_brief=1
# 只跑某个套件
build\x64\Debug\tmpp_tests.exe --gtest_filter=WindowsProcessProbeTest.*
```

### 通过 Visual Studio 构建

打开 `TaskManagerPlusPlus.sln` 直接构建。`Debug`、`Release`、`RelWithDebInfo` 均只配置了 x64。

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
# 构建三个配置
tools\m1build.bat Debug
tools\m1build.bat Release
tools\m1build.bat RelWithDebInfo

# 确认可执行文件存在
Test-Path build\x64\Debug\tmpp.exe

# 单测（三个配置各自的二进制）
build\x64\Release\tmpp_tests.exe --gtest_brief=1

# Release 不应产出 PDB
Get-ChildItem build\x64\Release -Filter *.pdb      # 期望：无输出

# 版本资源已写入 exe
(Get-Item build\x64\Release\tmpp.exe).VersionInfo | Select-Object FileVersion, ProductName
```

> 单测中有 1 个用例在**处理器性能计数器不可用或核心处于 parked 状态**时自行跳过
> （`tests\unit\test_platform_system_extended.cpp`），因此不同时刻的总数可能是
> 324 通过，或 323 通过 + 1 跳过。这是环境的正常波动，不是失败。

## 7. 发布打包

```powershell
# 打包（默认 Release）
tools\package.ps1

# 先构建、再打包
tools\package.ps1 -Build

# 跳过 zip，只留目录
tools\package.ps1 -NoZip

# 只校验一个已生成的目录
tools\verify-portable.ps1
tools\verify-portable.ps1 -Path dist\TaskManagerPlusPlus-0.1.0-win-x64
```

### 7.1 为什么交付的不是"单个 exe"

WinUI 3 的原生 C++ 应用**无法**收敛成单个可执行文件：

- 不存在静态版的 `Microsoft.UI.Xaml*.lib`，其运行时只能以独立 DLL 形式随包分发。
- 微软文档明确 `PublishSingleFile` 是 .NET 的模型，对 WinUI 3 原生应用不适用。

因此交付形态是**干净的目录 + zip**。`tmpp.exe` 自身仅约 1 MB，包体大头是 Windows App
SDK 的自包含运行时（`onnxruntime.dll` 约 20.7 MB、`DirectML.dll` 约 17.8 MB、
`Microsoft.ui.xaml.dll` 约 14.4 MB 等），这些 DLL 是必需的。

### 7.2 产物

| 项 | 内容 |
| --- | --- |
| 目录 | `dist\TaskManagerPlusPlus-<版本>-win-x64\` |
| 压缩包 | `dist\TaskManagerPlusPlus-<版本>-win-x64.zip`（内含顶层目录，解压即得一个文件夹） |

包内实际内容：`tmpp.exe`、Windows App SDK 自包含运行时 DLL 与 `.pri` / `.winmd`、
79 个本地化子目录（WinUI 自身的 `.mui`）、VC++ 运行库 DLL、`LICENSE`、`README.txt`。

打包脚本排除的构建产物：`*.pdb`、`*.lib`、`*.exp`、`*.ilk`、`*.idb`、`*.obj`、
`*.tlog`、`tmpp_tests.exe`、`gmock*.dll`、`run.bat`。排除之后脚本会**再断言一次**
结果，因为 `robocopy` 的 `/XF` 只按文件名匹配，子目录里的同名残留会漏过。

### 7.3 VC++ 运行库随包分发

应用动态链接 CRT，启动时需要 `msvcp140.dll`、`vcruntime140.dll` 等。**Windows 并不
自带**这些 DLL（它们随 VC++ 可再发行组件安装），所以干净系统上可能不存在。

打包脚本从 VS 可再发行目录
`VC\Redist\MSVC\<版本>\x64\Microsoft.VC*.CRT` 复制 10 个 DLL（约 1.9 MB）进包，
即微软文档所述的**应用本地部署**方式；这样除操作系统本身外不再有外部依赖。
传 `-NoCrt` 可跳过（届时要求目标机已安装可再发行组件）。

### 7.4 版本一致性由打包脚本把关

版本号只有一个来源：`vcpkg.json` 的 `version` 字段，包名取自它。而 `src/App/app.rc`
中的版本资源是手写的，因此打包脚本会**比对 exe 的 `ProductVersion`**，不一致即拒绝
打包，避免出现"文件名一个版本、属性页另一个版本"。修改版本时两处都要改。

### 7.5 应用图标与版本资源（B-003）

`src/App/app.rc` 写入两类资源：

- **图标** `RT_GROUP_ICON` id 1。Windows 对可执行文件取**编号最小**的图标组，故 id 1
  即 Explorer、任务栏与 Alt-Tab 使用的图标。`app.ico` 含 8 个尺寸
  （256/64/48/40/32/24/20/16，均 32bpp），以免高 DPI 下拿 32×32 放大。
- **版本资源** `RT_VERSION`，即属性页"详细信息"读取的字段。在此文件存在之前，
  本程序的 exe **完全没有版本信息**。

`app.ico` 提取自 `%windir%\System32\taskmgr.exe` 的图标组 30651，可用
`tools\extract-icon.ps1` 复现（该脚本按 `RT_GROUP_ICON` 目录重建多尺寸 `.ico`，
不执行被检查的模块）：

```powershell
tools\extract-icon.ps1 -Module $env:windir\System32\taskmgr.exe -List
tools\extract-icon.ps1 -Module $env:windir\System32\taskmgr.exe -GroupId 30651 -Output src\App\app.ico
```

> **清单不放在 `.rc` 里。** 应用清单由工程的 `<Manifest>` 项嵌入，以便合并 Windows App
> SDK 的可激活类注册；若在 `.rc` 中再声明 `RT_MANIFEST`，会出现两份清单并链接失败。

### 7.6 打包后的校验

`tools\package.ps1` 在压缩前调用 `tools\verify-portable.ps1`；后者**直接读取二进制**，
不依赖任何文件清单：

1. **清单完整性**——解析 `tmpp.exe` 内嵌的 `RT_MANIFEST`，确认其中引用的每个文件都在
   包内（当前 25 个引用、0 缺失）。该清单是免注册 WinRT 激活的依据，缺文件表现为
   **启动时激活失败**，而不是功能降级。
2. **导入闭包**——解析包内每个 exe/dll 的 PE 导入表，确认每个被导入的 DLL 要么在包内，
   要么由操作系统提供（`System32`，或 `api-ms-win-*` / `ext-ms-win-*` 这类虚拟 API 集）。
   当前：68 个包内解析、1066 个由系统提供、**0 个未解析**。

两个检查都不执行被检查的二进制，因此不需要 VS 开发环境，也不需要 `dumpbin`。

### 7.7 发布前的冒烟测试

应用**必须**在交互式桌面会话中启动（原因见第 5 节）。打包后在 `dist` 目录内直接运行
`tmpp.exe`，确认窗口正常出现即可；`build\` 下的同名文件即使被占用也不影响打包，
但 `dist\...\tmpp.exe` 若正在运行，会阻止下一次重新打包（脚本会先删除该目录）。

## 12. 构建性能：诊断与结论

构建一度需要 **200 秒以上**。经排查降到 **约 48 秒**（冷构建）。本节记录结论、方法
和试过但无效的方案，避免以后重复走一遍。

### 12.1 结论

| 改动 | 效果 | 是否保留 |
| --- | --- | --- |
| 开启 `/MP`（`MultiProcessorCompilation`）+ `/FS` | 200s → 48s | **保留** |
| 预编译头（仅含 WinRT 投影 + 标准库） | 48s → **72s（更慢）** | **放弃** |

### 12.2 根因：`/MP` 默认关闭，WinRT 文件按串行编译

实测单文件编译耗时：

| 文件 | 耗时 |
| --- | --- |
| `UI/CoreGrid.cpp`（含 WinRT 投影） | **约 8 秒** |
| `UI/Formatting.cpp`（纯逻辑） | 约 1.6 秒 |

WinRT 投影的解析成本是纯逻辑文件的 **5 倍**。而 5 个工程**全部**没有开启
`MultiProcessorCompilation`（默认 `false`）。`/m` 只在**工程之间**并行，而工程之间有
依赖链（Platform → Domain → Core → App），所以实际是：14 个 UI 重文件在一个工程内
**串行**编译，每个约 8 秒。

**修复**：在每个 `.vcxproj` 的 `ClCompile` 中加入：

```xml
<MultiProcessorCompilation>true</MultiProcessorCompilation>
```

并在 `AdditionalOptions` 中加 `/FS`。**`/FS` 是必需的**：`/MP` 会拉起多个 `cl.exe`
进程写同一个 PDB，没有 `/FS` 就会报 `C1041: cannot open program database`。

验证方法：构建时采样 `cl.exe` 进程数。

```powershell
# 峰值应接近逻辑处理器数（本机 28 核 → 峰值 31，平均 12.4）
while ($job.State -eq 'Running') {
  (Get-Process cl -ErrorAction SilentlyContinue | Measure-Object).Count
  Start-Sleep -Milliseconds 250
}
```

也可以让 MSBuild 打印真实命令行，确认 `/MP` 真的传给了 `cl.exe`：

```powershell
MSBuild <proj> /p:Configuration=Debug /p:Platform=x64 /v:diagnostic /nologo |
  Select-String 'cl\.exe /c'
```

> **注意**：不要用 `.tlog` 文件判断 `/MP` 是否生效。tlog 可能来自上一次构建而滞后，
> 实测出现过后缀为 `.tlog` 的文件中**没有** `/MP`、但真实命令行**有** `/MP` 的情况。

### 12.3 试过但无效：预编译头

思路是把 WinRT 投影放进 PCH。**实测反而更慢（48s → 72s）**，原因：

- 生成的 `.pch` 文件达 **932 MB**。
- 每个翻译单元都要**读取**这 932 MB，成本超过了重新解析头文件。
- 它同时会重新触发 `C1076: internal heap limit`（这正是本项目早先移除 PCH 的原因）。

**关键教训**：PCH 对 WinRT 投影不适用，因为投影的预编译体积与源码体积之比极端。
不要仅凭"PCH 通常能加速"就重新引入它 —— 本项目已有两次实测数据表明相反。

### 12.4 各工程耗时分布（冷构建）

| 工程 | 耗时 |
| --- | --- |
| `TaskManagerPlusPlus`（App + UI，14 个 WinRT 文件） | **约 42 秒** |
| `tmpp_core` | 约 5.3 秒 |
| `tmpp_platform` | 约 3.3 秒 |
| `tmpp_domain` | 约 2.7 秒 |

瓶颈**完全**在 App 工程。其余三个工程合计仅约 11 秒。

### 12.5 尚未尝试的优化方向

若要进一步压缩（当前 48 秒，目标 20 秒以内），以下是尚未验证的方向：

1. **拆分 App 工程**：UI 与 App 分离成两个静态库，让 `/m` 能在工程级别并行，而不只
   依赖 `/MP` 的工程内并行。
2. **减少 WinRT 投影的包含面**：`UI/WinRTUI.h` 目前一次性引入 11 个投影头。按文件
   需要拆分，可降低单文件解析成本。
3. **`/Zf`（更快 PDB 生成）**：MSVC 的 `/Zf` 可加速 PDB 写入，未实测。
4. **共用头文件前置**：把 `Core/SamplingCoordinator.h`、`UI/Controls.h` 等高频头文件
   也纳入 PCH —— 但 12.3 的结论表明 PCH 在此项目不划算，需重新实测。
5. **增量构建**：增量构建已比冷构建快（改 UI 文件约 31 秒），瓶颈同样是单个 WinRT
   文件的解析成本。

> 如需继续优化，**务必先实测再下结论**：本节的 PCH 一项就是"看起来该有效、实际更慢"
> 的例子。
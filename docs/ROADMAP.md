# 路线图

## 里程碑一：最小闭环

**目标**：打通「原生采集 → Domain 计算 → 快照发布 → UI 渲染」主干，
产出一个可运行的进程列表 + 性能图表。

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| M1-0 | **构建链路验证**：NuGet 还原、MIDL/mdmerge/cppwinrt 投影、MSBuild 构建、自包含部署 | ✅ **已完成** |
| M1-1 | Platform 层：进程枚举探针（`NtQuerySystemInformation`）+ 系统 CPU/内存探针 | 待开始 |
| M1-2 | Domain 层：`RateMath`、`RingBuffer`、`ProcessModel`、`SystemModel` + 单测 | 待开始 |
| M1-3 | Core 层：`BackgroundSampler`、`Settings`、`PathService` | 待开始 |
| M1-4 | UI 层：外壳 + 进程列表（虚拟化 + 排序 + 搜索） | 待开始 |
| M1-5 | UI 层：Win2D 图表控件 + 性能页 CPU/内存块 | 待开始 |
| M1-6 | 图表颜色自定义 + 预设主题 | 待开始 |
| M1-7 | 多语言（中/英）+ 主题切换 + 窗口状态记忆 + 单实例 | 待开始 |

### M1-0 交付内容

- `TaskManagerPlusPlus.sln` 与 `src/App/TaskManagerPlusPlus.vcxproj`
- 纯代码构建的 WinUI 3 窗口外壳（`NavigationView` + 三个导航项 + 占位页面）
- NuGet 包管理（`Directory.Packages.props` / `nuget.config`）
- vcpkg 清单（`vcpkg.json`）与自定义 triplet
- 构建脚本 `tools/m1build.bat`
- 文档：`README.md`、`README.zh-CN.md`、`docs/ARCHITECTURE.md`、`docs/BUILD.md`、
  `docs/METRICS.md`、`docs/CODE_CONVENTIONS.md`、`docs/ROADMAP.md`

## 里程碑二：功能补全

- 磁盘、网络、GPU 三块性能图表与硬件信息卡
- 详细信息页（全列 + 列自定义）
- 进程控制（结束、强制结束、结束进程树）
- 提权机制（设置项「始终以管理员运行」+ 立即提权入口）
- 进程摘要卡片、右键菜单完整化

## 里程碑三：体验完善

- 系统托盘图标与最小化到托盘
- 窗口置顶（Always on Top）
- 效率模式（EcoQoS）开关
- 进程优先级设置
- CPU 亲和性、挂起 / 恢复
- GPU 逐进程数据增强
- 图表悬停十字光标读数
- 内存条 SMBIOS 详细信息

## 路线图：暂缓特性

以下功能本期不实现，但架构上预留扩展点。

### 其余页签

导航栏与面板注册机制支持后续新增页签。以下四个页签暂缓：

| 页签 | 说明 |
| --- | --- |
| 启动应用 | 读取注册表 `Run` 键与启动文件夹，支持启用/禁用 |
| 用户 | 当前登录用户及其资源占用，支持断开/注销 |
| 服务 | 服务列表与启停（需 SCM 权限） |
| 应用历史记录 | 应用资源使用记录 |

### 其他未来方向

- 句柄与模块查看
- 线程栈查看
- ETW 事件追踪
- 图表主题导入 / 导出与分享
- 配置备份与迁移
- 插件系统
- 远程只读 API
- CI 与自动发布

## 已知技术债务

| 编号 | 事项 | 说明 |
| --- | --- | --- |
| D-01 | 启动诊断代码 | `src/App/StartupLog.h` 为 M1-0 排查临时加入，发布前需移除或在发布构建中禁用 |
| D-02 | XAML 支持缺失 | 若补装 VS 的 C++ XAML 组件，可评估是否迁移回 XAML 声明式 UI |
| D-03 | vcpkg 包构建阻塞 | 本机 VS 未注册导致 `vcpkg install` 失败，见 `BUILD.md` 第 3.5 节 |
| D-04 | Windows App SDK 版本 | 现用 1.8。2.x 元包会拉入版本漂移的子包，待评估 |

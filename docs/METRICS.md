# 指标采集规范

本章是实现的核心技术依据。**所有指标必须走原生 API，严禁使用 WMI 作为主路径**
（WMI 仅可作为极少数无原生替代方案的兜底，且必须有明确 TODO 标注）。

> **当前状态**：CPU、内存、磁盘、网络、GPU 采集均已实现并实测。
> 本文档既是实现依据，也记录各指标实际使用的数据源与验证方法。
> 尚未实现的字段在下方就地标注。
> 其中列出的 API 均已通过参考项目
> （`tasksmack`、`SystemInformer`、`TaskExplorer`）验证可行。

## 1. 进程与 CPU

| 指标 | 原生 API | 说明 |
| --- | --- | --- |
| 全量进程枚举 | `NtQuerySystemInformation(SystemProcessInformation)` | **单次批量快照**，一次调用获得所有进程的 CPU 时间、内存、IO、句柄数、线程数、创建时间、映像名。避免逐进程 `OpenProcess`。 |
| 系统总 CPU 时间 | `GetSystemTimes` | 返回 Idle / Kernel / User 三个 `FILETIME`。**注意 Kernel 已含 Idle**，计算时需扣除。 |
| 每核心 CPU | `NtQuerySystemInformation(SystemProcessorPerformanceInformation)` | 返回每个逻辑处理器的 IdleTime / KernelTime / UserTime 等。 |
| 逻辑处理器信息 | `GetLogicalProcessorInformationEx` | 获取物理核 / 逻辑核 / 缓存层级与大小。 |
| CPU 型号与频率 | 注册表 `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\*` | 型号名、基准频率、标识、厂商。 |
| 进程映像路径 | `QueryFullProcessImageNameW` | 使用**增长式缓冲区**以支持超长路径（固定缓冲区在长路径场景下会截断）。 |
| 进程命令行 | `NtQueryInformationProcess(ProcessCommandLineInformation)` | 需 `PROCESS_QUERY_LIMITED_INFORMATION` 权限。 |
| 进程所有者 | `OpenProcessToken` + `GetTokenInformation(TokenUser)` + `LookupAccountSid` | 需提权才能覆盖全部进程。 |
| 进程状态 | `SYSTEM_PROCESS_INFORMATION` 的 `ThreadState` 聚合 | 区分 Running / Suspended / Not Responding 等。 |

### `SYSTEM_PROCESS_INFORMATION` 关键字段

经参考项目验证，以下字段可直接从批量快照取得，无需打开进程句柄：

| 字段 | 含义 |
| --- | --- |
| `CreateTime` | 进程启动时间（用于复合键，处理 PID 复用） |
| `UserTime` / `KernelTime` | 累计 CPU 时间（差值计算 CPU%） |
| `WorkingSetSize` / `PeakWorkingSetSize` | 工作集与峰值 |
| `PrivatePageCount` | 私有提交量 |
| `VirtualSize` / `PeakVirtualSize` | 虚拟内存与峰值 |
| `HandleCount` / `NumberOfThreads` | 句柄数 / 线程数 |
| `ReadTransferCount` / `WriteTransferCount` / `OtherTransferCount` | 累计磁盘 IO |
| `ImageName` | 映像名（`UNICODE_STRING`） |

> **实现提示**：`NtQuerySystemInformation` 从 `ntdll.dll` **动态加载**
> （`GetProcAddress`），不要静态链接，也不要依赖 WDK 头文件。
> 缓冲区需循环重试：`STATUS_INFO_LENGTH_MISMATCH` 时扩大缓冲区重试。

## 2. 内存

| 指标 | 原生 API |
| --- | --- |
| 系统内存总量 / 可用 / 已提交 | `GlobalMemoryStatusEx` |
| 内存组成（使用中/已修改/备用/空闲） | `NtQuerySystemInformation(SystemMemoryListInformation)` |
| 进程工作集 / 私有字节 / 虚拟大小 / 峰值 | `SYSTEM_PROCESS_INFORMATION`（见上表） |
| 进程缺页数 | `GetProcessMemoryInfo`（`PROCESS_MEMORY_COUNTERS_EX`） |
| 内存条速度 / 插槽 / 外形规格 | `GetSystemFirmwareTable('RSMB')` 解析 SMBIOS 原始数据 |

> **TODO**：内存条详细信息需解析 SMBIOS，实现成本较高。
> 若进度紧张可先隐藏这些字段，并在代码中标注 TODO。

## 3. 磁盘

| 指标 | 原生 API |
| --- | --- |
| 物理磁盘活动时间、读写速率 | PDH 计数器（`\PhysicalDisk(*)\% Idle Time`、`\Disk Read Bytes/sec` 等） |
| 逻辑卷容量与剩余 | `GetLogicalDrives` + `GetDiskFreeSpaceExW` |
| 磁盘型号与类型（SSD/HDD） | `DeviceIoControl(IOCTL_STORAGE_QUERY_PROPERTY)` 取 `StorageDeviceProperty` |
| 逐进程磁盘 IO | `SYSTEM_PROCESS_INFORMATION` 的 `ReadTransferCount` 等 |

> **重要陷阱**：PDH 计数器名称随系统语言变化。在本地化系统上硬编码英文名会失败。
> 必须使用**语言无关的索引号**，或 `PdhAddEnglishCounter`。

## 4. 网络

| 指标 | 原生 API |
| --- | --- |
| 网卡列表与累计字节数 | `GetIfTable2` / `GetIfEntry2`（64 位计数器 + Unicode 接口名，**优于** `GetIfTable`） |
| 适配器详细信息 | `GetAdaptersAddresses`（IP 地址、网关、DNS、链路速度、连接状态） |
| 逐进程网络字节数 | `GetPerTcpConnectionEStats` / `SetPerTcpConnectionEStats` |

> **重要限制**：逐进程 TCP 字节计数**需要管理员权限**才能启用采集。
> 无权限时该列必须显示为不可用，并提示提权。
> 参考项目中记录的实测行为：需先调用 `SetPerTcpConnectionEStats` 启用采集，
> 才能通过 `GetPerTcpConnectionEStats` 读取。

## 5. GPU

采用三级策略，逐级增强：

| 级别 | 数据源 | 提供的数据 |
| --- | --- | --- |
| 基础（所有 GPU） | **DXGI**：`IDXGIFactory1::EnumAdapters1` + `DXGI_ADAPTER_DESC1` | 适配器名称、显存总量、厂商 ID、设备 ID、LUID |
| 基础（所有 GPU） | **PDH**：`\GPU Engine(*)\Utilization Percentage`、`\GPU Adapter Memory(*)\Dedicated Usage` | 每 GPU 利用率、每进程 GPU 利用率、显存使用 |
| 增强（仅 NVIDIA） | **NVML**（动态加载 `nvml.dll`） | 显存使用、温度、功耗、功耗上限、GPU/显存频率、风扇转速、PCIe 吞吐、逐进程显存 |

### 实现要点

- NVML **必须动态加载**：`LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)`
  + `GetProcAddress`。**不得静态链接**。未安装 NVIDIA 驱动时静默降级。
- DXGI 适配器与 NVML 设备通过**名称模糊匹配**建立映射。
- NVML 的 UUID 体系与 DXGI 的 LUID 体系**不同，不可混用**。
- 部分 NVML 函数在旧驱动上不存在（如 `nvmlDeviceGetComputeRunningProcesses`），
  必须用**可选加载**：缺失时置空函数指针并降级，不得因此判定整体失败。
- NVML 用 `UINT64_MAX` 表示"显存信息不可用"，需与"真实 0"区分。
- 能力缺失**不是错误**，不得记录为 error 级别日志。

## 6. 电源与效率模式

| 指标 | 原生 API |
| --- | --- |
| 进程效率模式（EcoQoS）状态 | `GetProcessInformation(ProcessPowerThrottling)` |
| 设置效率模式 | `SetProcessInformation(ProcessPowerThrottling, PROCESS_POWER_THROTTLING_EXECUTION_SPEED)` |
| 系统电源状态 | `GetSystemPowerStatus` |

## 7. 进程操作

| 操作 | 原生 API |
| --- | --- |
| 结束进程 | `TerminateProcess`（需先 `OpenProcess(PROCESS_TERMINATE)`） |
| 结束进程树 | 按父子关系自底向上递归 `TerminateProcess` |
| 设置优先级 | `SetPriorityClass` |
| 设置亲和性 | `SetProcessAffinityMask` |
| 挂起 / 恢复 | `NtSuspendProcess` / `NtResumeProcess`（动态加载自 `ntdll.dll`） |

> **本期范围**：仅实现「结束进程 / 强制结束 / 结束进程树」。
> 优先级、亲和性、挂起恢复、效率模式属于 M2 / M3，见 `ROADMAP.md`。

## 8. 采集性能约束

| 编号 | 约束 |
| --- | --- |
| P-001 | 进程枚举必须是**单次批量调用**，禁止逐进程 `OpenProcess` + 查询。 |
| P-002 | 慢变信息（所有者、命令行、发布者、分类）通过**短生命周期进程句柄**获取，并设 TTL 缓存（5~30 秒），不随每次采样刷新。 |
| P-003 | 所有采集在后台线程执行，UI 线程零阻塞。 |
| P-004 | 渲染循环内禁止加锁、禁止深拷贝、禁止动态分配大量内存。 |
| P-005 | 历史数据使用**有界环形缓冲**，内存占用不随时间增长。 |
| P-006 | 重复的采样失败日志必须限流，渲染路径禁止无节流日志。 |
| P-007 | 空闲与最小化时主动降频（默认降至 5 秒）。 |

## 9. 能力标志设计

探针必须返回**能力标志**（capability flags），描述哪些指标可用：

```
hasIoCounters       逐进程磁盘 IO 是否可用
hasCommandLine      命令行是否可读
hasOwner            进程所有者是否可读
hasPriority         优先级是否可读写
hasNetworkCounters  逐进程网络计数是否可用
hasGpuMetrics       逐进程 GPU 是否可用
hasPerCoreCpu       每核心 CPU 是否可用
```

UI 层据此**隐藏不可用的列与操作**，而不是显示 0 或伪造数据。

> **原则**：能力缺失不是错误。受支持的系统仍可能因内核配置、权限、
> 硬件、驱动或可选厂商库而缺失某些指标。

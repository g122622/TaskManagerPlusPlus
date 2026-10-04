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
| 已缓存 / 分页池 / 非分页池 / 提交峰值 / 句柄总数 | `GetPerformanceInfo`（一次调用返回全部，页数 × `PageSize`） |
| 进程工作集 / 私有字节 / 虚拟大小 / 峰值 | `SYSTEM_PROCESS_INFORMATION`（见上表） |
| 进程缺页数 | `GetProcessMemoryInfo`（`PROCESS_MEMORY_COUNTERS_EX`） |
| 内存条容量 / 代数 / 频率 / 位宽 / 电压 / 厂商 / 型号 | `GetSystemFirmwareTable('RSMB')` 解析 SMBIOS Type 17 |

### 内存条（SMBIOS）

无 Windows API 报告内存条信息：操作系统知道**有多少**内存，不知道**由什么组成**。
唯一来源是固件表，用 `GetSystemFirmwareTable('RSMB')` 取原始 SMBIOS 后自行解析
Type 17 结构。`SmbiosMemoryProbe` 的字段偏移曾整体错位一个字段（读出的位宽
32767 实际是 Size 字段的“未设置”标记），且类型名称表把 DDR4 标成了 DDR2 FB-DIMM；
两者都是通过与 `Win32_PhysicalMemory` 对照才发现的——Windows 对同四条内存同样
报告 code 26，说明解析正确、只是命名错。本机实测：

| 项 | 读数 |
| --- | --- |
| 插槽 | 4 个全部占用 |
| 容量 | 4 × 32 GB = 128 GB（系统可用 127.8 GB） |
| 代数 / 封装 | DDR4 / DIMM |
| 频率 | 3200 MHz（配置值与标称值相同） |
| 位宽 / 电压 | 64-bit / 1.20 V |
| 厂商 / 型号 | JUHOR `JHD3200U1832JG`、Gloway `VGM4UX32C18BG-DTACW` |

“硬件保留”= 内存条总量 − 系统可用量（本机约 211 MB），在组成条最左侧以红色绘制。

## 3. 磁盘

| 指标 | 实际实现 | 说明 |
| --- | --- | --- |
| 设备枚举（设备索引 + 盘符） | `PdhEnumObjectItemsW(L"PhysicalDisk")` | **只用来取名字**，不取计数值 |
| 读写字节数、读写次数、服务时间、空闲时间 | `DeviceIoControl(IOCTL_DISK_PERFORMANCE)` | **累计值**，速率由 Domain 差分得出 |
| 队列深度 | 同上，`QueueDepth` 字段 | 瞬时值，不参与差分 |
| 容量 | `GetDiskFreeSpaceExW`（对实例名中的盘符） | 见下 |
| 文件系统、卷标 | `GetVolumeInformationW` | 同一个盘符 |
| 型号、总线类型 | `IOCTL_STORAGE_QUERY_PROPERTY` → `StorageDeviceProperty` | `BusType` 同在此描述符内 |
| SSD/HDD 判定 | `StorageDeviceSeekPenaltyProperty` + `StorageDeviceTrimProperty` | 见下 |
| 页文件所在设备 | 注册表 `Session Manager\Memory Management\PagingFiles` | 多字符串列表 |

> **为什么不用 PDH 的 `\PhysicalDisk(*)\Disk Read Bytes/sec`**：那是 PDH 预先算好的
> **速率**，而 Domain 对所有指标统一按「累计值差分」处理。混用两种约定，就会出现
> 某个值被差分两次的情况，而且第一次采样无法诚实地显示。因此除了 `% Idle Time`
> 之外一律不用 PDH 计数值。
>
> **活动时间为什么用 `IdleTime` 的补**：第一版用「读服务时间 + 写服务时间」，
> 结果偏高数倍。设备在处理重叠请求时两个计数器同时增长，其和会超过墙上时间。
> `IdleTime` 是墙上时钟量，取补即为准确的工作时间占比。

> **重要陷阱**：PDH 计数器名称随系统语言变化。在本地化系统上硬编码英文名会失败。
> 已统一使用 `PdhAddEnglishCounterW`；`PdhEnumObjectItemsW` 的 `PhysicalDisk`
> 是对象名而非计数器名，同样受语言影响，待本地化系统实测后决定是否改用索引号。

> **容量为什么不用 `IOCTL_DISK_GET_LENGTH_INFO`**：该 IOCTL 需要**读权限句柄**，
> 而探针以 query-only 打开（避免要求管理员权限），调用会静默失败并返回 0。
> 实测当时 5 块盘全部显示 0 GB。

## 4. 网络

| 指标 | 实际实现 | 说明 |
| --- | --- | --- |
| 接口索引枚举 | `GetIfTable2` | **每次进程只调用一次**，见下 |
| 累计收发字节、包数、错误、丢弃 | `GetIfEntry2`（按索引） | 累计值，速率由 Domain 差分得出 |
| 链路速率、连接状态、描述 | 同上，`MIB_IF_ROW2` 内 | |
| 逐进程网络字节数 | 未实现 | 需要管理员权限，见下 |

> **`GetIfTable2` 每次采样调用一次会让采样周期失效**。实测：本机 76 个实例、
> **433 ms**；同样 76 个实例改用 `GetIfEntry2` 逐个查询只要 **2 ms**。
> 因此接口索引在进程生命周期内**只枚举一次**，之后逐索引轮询。
> 设备集合在一次会话内不会变化，单个适配器查询失败时跳过该适配器即可，
> **不得**因此重建列表（第一版正是这样把 433 ms 重新塞回了每一次采样）。
> 实测：网络读取从 2499 ms 降到 1.58 ms，一轮三源合计 8 ms。

> **接口过滤用 `HardwareInterface` 标志**：先排除 loopback 与 tunnel 仍有 9 个接口，
> 改用 IF_TYPE 白名单降到 6 个，其中含 WAN Miniport（`IF_TYPE_PPP`，链路速率为 0）。
> 真正区分「真实连接」的是 `InterfaceAndOperStatusFlags.HardwareInterface`：
> Hyper-V 虚拟交换机报告普通 `IF_TYPE_ETHERNET_CSMACD`，隧道报告虚拟类型，
> 类型和链路速率都无法把它们与物理网卡区分开。最终只剩 2 个真实适配器。

> **重要限制**：逐进程 TCP 字节计数**需要管理员权限**才能启用采集。
> 无权限时该列必须显示为不可用，并提示提权。
> 参考项目中记录的实测行为：需先调用 `SetPerTcpConnectionEStats` 启用采集，
> 才能通过 `GetPerTcpConnectionEStats` 读取。

## 5. GPU

采用三级策略，逐级增强：

| 级别 | 数据源 | 提供的数据 |
| --- | --- | --- |
| 基础（所有 GPU） | **DXGI**：`IDXGIFactory1::EnumAdapters1` + `DXGI_ADAPTER_DESC1` | 适配器名称、显存总量、厂商 ID、设备 ID、LUID |
| 基础（所有 GPU） | **PDH**：`\GPU Engine(*)\Utilization Percentage`、`\GPU Adapter Memory(*)\Dedicated Usage`、`Shared Usage` | 利用率（总体 + 分引擎）、专用与共享显存 |
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

**实测确认的三处坑**：

- **显存计数用 `GPU Adapter Memory` 而非 `GPU Process Memory`**。后者报告的是
  进程**提交量**而非驻留量：本机实测求和得到 **112 GB**，而显卡只有 4 GB。
  该数字不可能作为「占显卡容量的比例」呈现。
- **显存总量用 DXGI 而非注册表**。注册表 `HardwareInformation.qwMemorySize`
  与用量计数器相差一个由驱动决定的倍数（实测 3.98 GB vs 5.95 GB）。
  DXGI 的 `DXGI_ADAPTER_DESC1::DedicatedVideoMemory` 是权威值，且顺带给出适配器名。
  WinUI 的 `DXGI_ADAPTER_DESC` 没有 `Flags`，需用 `EnumAdapters1` + `GetDesc1`。
- **利用率取「最忙引擎」而非求和**。实例名形如
  `pid_1234_luid_0x0_0xC1C3_phys_0_eng_0_engtype_3D`，按 `_engtype_` 分组后
  **组内求和、组间取最大**：两个引擎各忙一半时求和会超过 100%，而任何引擎都不会。
  分引擎数值同时上报（3D / Copy / VideoDecode / VideoEncode）。

> **Platform 层不得包含 WinRT**。探针一度 `#include <winrt/base.h>` 以使用
> `com_ptr`，结果该目标文件记录的 C++/WinRT 版本与 App 不一致，链接直接失败
> （`LNK2038`）。DXGI 枚举用 WRL 的 `ComPtr` 即可。

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

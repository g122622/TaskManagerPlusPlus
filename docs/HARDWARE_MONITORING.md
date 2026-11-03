# CPU / GPU 硬件监控与驱动接入调研

调研日期：2026-11-03。本文区分本机实测结果和后续实现方案；应用当前尚未接入这些新增传感器。
现有进程、利用率、磁盘与网络采集保持独立，硬件传感器是可选增强。

## 1. 本机结论

本机 CPU 为 Intel Core i7-14700KF（8 个 P 核 + 12 个 E 核，20 个物理核心、28 个逻辑处理器），
GPU 为 NVIDIA GeForce RTX 5060，驱动版本 610.74。

| 指标 | 已验证情况 | 接入建议 |
| --- | --- | --- |
| CPU 逐逻辑处理器性能 / 频率估计 | PDH 两种计数器均返回 28 个有效实例 | 先扩展现有 `ProcessorSpeedProbe`，按拓扑映射物理核心；校验 P/E 核频率基准 |
| CPU 每核心温度 | 本机没有注册 PawnIO，尚未实测 MSR 温度 | 官方签名 PawnIO + 官方签名 IntelMSR 模块 |
| CPU 每核心时钟 / 有效频率 | IntelMSR 官方模块允许读取相关 MSR；未实测本机 MSR | 分清时钟、工作期间平均频率和整个间隔的有效频率 |
| CPU VID | 有参考实现，尚未验证 14700KF 的寄存器口径 | 仅在型号与有效性校验通过后显示，明确标注“请求电压 VID” |
| CPU 每核心实测 Vcore | 没有证据表明每个核心都有独立可读的实测电压 | 主板 VRM / Super I/O 的供电轨读数另行研究，不复制成每核电压 |
| GPU 温度、核心频率、显存频率、功耗 | NVML 直接调用成功 | 无需额外安装硬件访问驱动，复用 NVIDIA 驱动提供的 DLL |
| GPU 核心电压 | NVAPI 私有只读接口连续读取成功 | 作为可选能力；记录接口版本，缺失或失败时显示不可用 |

一次三次采样的实测结果（动态值，不是硬件规格或持续性能）：

- NVML 温度：39 °C；核心频率：765–847 MHz；显存频率：7001 MHz；功耗：19.393–19.504 W。
- NVAPI 电压：0.725、0.735、0.740 V；三个调用的状态均为成功。
- `\Processor Information(*)\% Processor Performance`：28 个实例有效，约 69.1%–156.5%。
- `\Processor Information(*)\Processor Frequency`：28 个实例全部为 3400 MHz。
  因此不能把后者直接当作本机的逐核实时睿频；前者也需要正确的参考频率才能换算。

这些结果来自进程内 DLL / PDH 只读调用，未安装 PawnIO、未加载 CPU 模块、未修改频率、电压或功耗限制。
GPU 电压是驱动返回的遥测读数，尚未与独立测量工具对照精度。

## 2. 可复现探测

使用 Windows x64 Python 3.10 或更新版本，只依赖标准库：

```powershell
# 默认仅探测公开接口；GPU 电压查询默认关闭
py -3.10 tools\probe-hardware.py

# 额外查询 NVAPI 私有电压接口，默认连续三次采样
py -3.10 tools\probe-hardware.py --include-private-nvapi

# 保存结果；build/ 不纳入版本管理
py -3.10 tools\probe-hardware.py --include-private-nvapi > build\hardware-capabilities.json
```

脚本从系统目录加载 `pdh.dll`、`nvml.dll`、`nvapi64.dll`；逐项报告成功状态和不可用原因。
缺少显卡 DLL、缺少导出函数或不支持某项能力，都不等同于传感器读数为零。
脚本检查的是 PawnIO 服务注册状态，不足以证明驱动已经启动或当前进程有权打开设备。

## 3. CPU 温度与频率如何采集

### 3.1 使用已有核心拓扑

项目已有 `ProcessorTopology`，包含 processor group、逻辑处理器索引、物理核心索引、
P/E 核效率类及 SMT 主线程标记。沿用该映射，不按“逻辑处理器编号除以二”推断核心。

温度按物理核心 / 传感器作用范围展示。P 核的两个 SMT 线程通常共享核心温度，
E 核或其他架构也可能共享传感器，不能承诺 20 个独立温度源，更不能把 28 个线程当成 28 个独立温度源。
每个传感器应携带 `core`、`cluster` 或 `package` 的范围信息。

### 3.2 温度寄存器

官方 IntelMSR 模块允许读取下列寄存器，LibreHardwareMonitor 已有对应实现：

| MSR | 用途 | 解码要点 |
| --- | --- | --- |
| `0x1A2` / `IA32_TEMPERATURE_TARGET` | 温度目标 / TjMax | 参考实现读取 bits 23:16；仍须按 CPU 型号核对 |
| `0x19C` / `IA32_THERM_STATUS` | 核心数字温度传感器 | bit 31 为有效位；bits 22:16 为距温度目标的差值 |
| `0x1B1` / `IA32_PACKAGE_THERM_STATUS` | CPU 封装温度 | 单独展示，不能替代所有核心温度 |

在目标型号满足上述定义时，核心温度为 `TjMax - digital_readout`。
读取失败、有效位未置位、温度目标未知时返回不可用；不能固定减 100、用封装温度补每核温度，或把失败转换成 0 °C。

### 3.3 三种频率口径

1. **当前时钟**：型号支持时，读取 `0x198` / `IA32_PERF_STATUS` 的倍率并结合实际参考时钟。
   不能假定所有 Intel 型号都使用同一位域，也不能把 invariant TSC 频率当作实时核心频率。
2. **工作期间平均频率**：使用 `ΔAPERF / ΔMPERF × reference_frequency`。
   参考频率必须与该型号的计数器定义一致，P/E 核不能直接套用单个注册表值。
3. **整个采样间隔的有效频率**：在确认 APERF 计数单位后，使用 `ΔAPERF / elapsed_seconds` 换算 MHz。
   它包含核心休眠带来的影响，与第 2 项的统计口径不同。

官方 IntelMSR 0.2.11 模块的读白名单包含 `0xE7` / MPERF 和 `0xE8` / APERF。
需要两次采样，检测计数回退、时间倒退、分母为零；不要为了读两次计数器在每核心循环内 sleep。
PDH 可作为无额外驱动的降级来源，但应标注其估计口径，不能与 MSR 有效频率混称。

### 3.4 VID 与实测电压

LibreHardwareMonitor 对 `IA32_PERF_STATUS` 有逐核 VID 解码逻辑，这只能作为候选参考。
该寄存器的电压位域及单位必须先与 14700KF 对应的 Intel 文档、CPUID 型号和已知工具结果核对。
不要直接套用旧处理器公式或高低 16 位兜底，否则正常整数也可能被误解成合理的电压数值。

VID 是核心提出的供电请求；Vcore / VR VOUT 是供电轨遥测。两者需要独立名称、来源和有效性。
多个核心共享供电轨时，一个主板电压读数不能作为多个独立核心的实测电压。

## 4. 如何接入 PawnIO

建议采用官方签名版本；先支持 Intel 温度、时钟和 VID，再考虑主板传感器。

### 4.1 安装与信任链

1. 从 [PawnIO 官方网站](https://pawnio.eu/) 获取官方安装器，以管理员权限完成安装。
   驱动是系统组件，单独复制一个 DLL 或 SYS 到应用目录不等同于安装。
   当前源码的 INF 是 `Root\PawnIO` 软件设备安装方式，应使用官方安装流程处理设备、服务和驱动包。
2. 使用官方 Modules Release 中的签名 `IntelMSR.bin`。驱动的 Windows 签名和模块的 PawnIO 签名是两层检查。
   自行改写或重新编译 `.p` 文件不会自动获得官方模块签名。
3. 官方驱动设备 ACL 为 `D:P(A;;GA;;;SY)(A;;GA;;;BA)`，允许 SYSTEM 和管理员访问。
   普通非提权进程不能只因“驱动已安装”就获得读取权限。
4. 正式环境应保留 Windows 驱动签名和内存完整性配置，使用能在目标环境加载的签名版本。
   本次只核对了源码和安装包结构，尚未在本机验证安装、重启、HVCI / Secure Boot 兼容性。

本次核验的模块发行版为 **0.2.11**，其中 `IntelMSR.bin` 为 5324 字节，SHA-256：

```text
d6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f
```

这是本次下载产物的固定指纹，不替代驱动和模块自身的签名验证，也不适用于其他发行版。

### 4.2 原生 C++ 调用边界

两条可行路线：

- 动态加载官方 `PawnIOLib.dll`，使用其 C ABI。实现简单，但需要一并处理该库的版本和再分发许可。
- 自己封装设备句柄及 `DeviceIoControl`，参考公开的设备协议。
  LibreHardwareMonitor 的原生调用包装说明 C++ 不需要为使用 PawnIO 而引入 .NET。

建议第一版用官方 C ABI验证硬件能力，固定库和模块版本：

```cpp
// 示意调用顺序：实际实现需动态绑定函数、校验每次 HRESULT，并用 RAII 管理资源。
HANDLE executor = nullptr;
pawnio_open(&executor);
pawnio_load(executor, signedIntelMsr.data(), signedIntelMsr.size());

ULONG64 input[1] = {0x19C};
ULONG64 output[1] = {};
SIZE_T written = 0;
// 在已选择的物理核心代表线程上调用；调用前设置并记录 GROUP_AFFINITY。
pawnio_execute(executor, "ioctl_read_msr", input, 1, output, 1, &written);
// 只有 HRESULT 成功且 written == 1 才解码 output；随后恢复原亲和性。

pawnio_close(executor);
```

`pawnio_execute` 的输入、输出大小是 **64 位元素数量**，不是字节数；`pawnio_load` 的大小是字节数。
名称是模块导出函数名。不要把普通 Win32 `GetLastError` 与 C ABI 的 HRESULT 混用。

设备协议使用 `IOCTL_PIO_LOAD_BINARY` 和 `IOCTL_PIO_EXECUTE_FN`；执行缓冲区由 32 字节
ASCII 函数名和后续 64 位参数组成。应优先使用官方头文件并固定协议版本，避免手写猜测 IOCTL 编号。

### 4.3 核心选择与进程权限

专用采样线程通过 `SetThreadGroupAffinity` 绑定到某个物理核心的代表逻辑线程，读完恢复。
按 group 和 mask 定位，检查 API 返回值；失败时不继续把当前核心的值标成目标核心。
需要核验固定版本驱动 / 模块保持调用核心语义，才能确认逐核采集正确。

第一阶段可由管理员运行应用来验证。正式集成建议使用按需提权的采集进程：

```text
普通权限 WinUI 应用
  ├─ PDH / NVML / NVAPI：常规指标与 GPU 遥测
  └─ 受限 IPC → 提权采集进程 → PawnIO → 官方 IntelMSR 模块
```

采集进程仅提供“读取传感器快照”操作，固定模块及 MSR 读白名单，不对 UI 开放任意模块加载、
任意 MSR 或写寄存器操作。安装只在用户启用增强传感器时进行，正常启动只检测并连接。
采集进程退出时关闭自身执行器，不卸载其他监控软件可能正在使用的 PawnIO 驱动。

### 4.4 与其他方案的取舍

| 方案 | 收益 | 接入成本与边界 |
| --- | --- | --- |
| 原生 C++ + PawnIO | 保持本项目 C++ 架构，固定 Intel 只读模块，权限边界明确 | 需要实现型号解码、采样与可选提权进程 |
| LibreHardwareMonitorLib + 独立采集程序 | 复用多厂商、主板与传感器识别 | 引入 .NET 运行时和 IPC；应核验传感器口径，保留 MPL 及第三方许可 |
| 自研内核驱动 | 可定制接口 | 增加 WDK、签名、安装、HVCI 与长期维护成本；本项目当前无需优先选择 |
| 旧式 WinRing0 包装 | 旧项目参考较多 | 应先核对目标 Windows 的易受攻击驱动阻止列表及维护状态，不作为新功能默认依赖 |

PawnIO 驱动为 GPL-2.0-or-later，并附设备 IOCTL 通信的特殊例外；官方 PawnIOLib 和模块为 LGPL-2.1-or-later。
库、驱动、模块分别核对许可；不能因为主程序使用 MIT 就把第三方驱动与模块改标为 MIT。
采用直接 IOCTL 通信也不自动免除分发驱动 / 模块时的许可证、通知与相应源码义务。

## 5. GPU 接入细节

生产采样直接调用 DLL，不在每个采样周期启动 `nvidia-smi` 子进程。

- NVML：初始化和设备枚举只做一次，调用 `nvmlDeviceGetTemperatureV`（可用时优先）、
  `nvmlDeviceGetClockInfo` 和 `nvmlDeviceGetPowerUsage`。旧驱动缺少 TemperatureV 时再尝试旧温度接口。
- 温度单位为 °C，频率为 MHz，功耗接口为毫瓦，转换为 W 时除以 1000。
  显存时钟按 API 原始 MHz 命名，不能无说明地当作显存数据传输速率或带宽。
- GPU 电压：本机成功的查询为 `nvapi_QueryInterface(0x465F9BCF)`，即参考实现中的
  `NvAPI_GPU_ClientVoltRailsGetStatus`。本次使用 76 字节 V1 布局、`size | (1 << 16)` 的版本值，
  从偏移 `0x28`、`0x2C` 组合电压原始值，单位微伏。该函数未出现在本次核验的公开 NVAPI 头文件中。
- 私有接口按可选函数处理；结构不兼容、返回不支持或零值时显示不可用。
  公开 `NvAPI_GPU_GetPstates20` 返回的是性能状态信息，不能拿 P-State 配置电压冒充实时电压。
- 多显卡必须正确映射：保留 Windows LUID、厂商标识、PCI domain/bus/device/function 等标识，
  按支持的稳定身份链建立映射；不用名称模糊匹配，也不假定 NVML 索引等于 DXGI 或 NVAPI 索引。
  当前诊断脚本的 NVAPI 只输出 PCI bus，足以辅助本机单卡核验，尚不足以完成通用多卡映射。
- GPU 更换、睡眠恢复或设备丢失时，使当前读数失效并重新发现设备；不要持续展示旧值而不标识过期。

## 6. 项目实施顺序

1. 在 Platform 增加可选 GPU 传感器探针和逐逻辑处理器 PDH 计数器；复用现有拓扑。
   `HardwareCounterProbe` 继续保留现有利用率 / 显存来源，新增传感器按字段声明能力。
2. 在 Domain 建立带时间戳、来源、单位、范围和有效状态的传感器快照；累积计数器差分在 Domain 完成。
   温度、VID 等瞬时读数不做无意义差分。硬件不可用和真实 0 必须可区分。
3. Core 在后台采样线程持有探针，默认约 1 秒采样；UI 消费发布快照，不直接访问驱动。
   不给每个核心单独创建定时器，不在 UI 线程切换亲和性。
4. 增加 CPU 可选驱动连接、提权采集进程与型号识别，先打通 14700KF 的温度与时钟。
   VID 单独标注，未验证的传感器保持不可用。
5. 验收涵盖休眠 / 唤醒、空闲 / 睿频负载、P/E 核、SMT 共享温度、驱动缺失、权限不足、
   多显卡身份、私有 NVAPI 失败及采样重置，并与独立硬件工具对照数值。

## 7. 依据与仍待验证的事项

已阅读并核验的来源：

- [NVIDIA NVML 设备查询文档](https://docs.nvidia.com/deploy/nvml-api/api/group__nvmlDeviceQueries.html) 与
  [NVIDIA 发布的 NVML 头文件](https://github.com/NVIDIA/go-nvml/blob/main/pkg/nvml/nvml.h)。
- [公开 NVAPI GPU 文档](https://docs.nvidia.com/gameworks/content/gameworkslibrary/coresdk/nvapi/group__gpu.html) 与
  [NVAPI 公开头文件](https://github.com/NVIDIA/nvapi/blob/main/nvapi.h)。
- [PawnIO 官方站](https://pawnio.eu/)、[原生 C ABI 头文件](https://github.com/namazso/PawnIO/blob/9d52965895588b0ffa3703b72eec75ba19f4ccc0/PawnIOLib/include/PawnIOLib.h)、
  [驱动安装 INF / 设备 ACL](https://github.com/namazso/PawnIO/blob/9d52965895588b0ffa3703b72eec75ba19f4ccc0/PawnIO/PawnIO.inf.in) 与
  [PawnIO 许可例外](https://github.com/namazso/PawnIO/blob/9d52965895588b0ffa3703b72eec75ba19f4ccc0/README.md)。
- [官方模块发行版 0.2.11](https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.11) 与
  [该版 IntelMSR 模块源码](https://github.com/namazso/PawnIO.Modules/blob/0.2.11/IntelMSR.p)。
- LibreHardwareMonitor 参考版本 `677a3a56abde9adff5abdb42db3a7638c9137572`：
  [Intel CPU 解码](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/677a3a56abde9adff5abdb42db3a7638c9137572/LibreHardwareMonitorLib/Hardware/Cpu/IntelCpu.cs)、
  [NVAPI 互操作结构](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/677a3a56abde9adff5abdb42db3a7638c9137572/LibreHardwareMonitorLib/Interop/NvApi.cs)。
- [Microsoft 内核驱动签名要求](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/kernel-mode-code-signing-policy--windows-vista-and-later-) 与
  [内存完整性及驱动兼容性](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/device-guard-and-credential-guard)。

尚待完成：官方驱动安装后的真实逐核 MSR 采集、14700KF 位域的 Intel 文档逐项校验、
VID 与独立工具对照、签名版本在本机内存完整性环境的加载测试、跨驱动版本的私有 NVAPI 兼容测试。
本次访问 Intel SDM 文档入口返回 HTTP 403，因此本文没有将参考项目的寄存器解码当作已完成 Intel 文档核验。

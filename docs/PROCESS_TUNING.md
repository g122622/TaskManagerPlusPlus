# 进程精细设置（Process Tuning）

本文记录进程页侧栏「进程设置」面板的设计、它背后的原生 API，以及几条在实现过程中被实测推翻或修正的假设。
对应代码：

| 层 | 文件 |
| --- | --- |
| Platform（类型） | `src/Platform/ProcessActionTypes.h`、`src/Platform/ProcessTuningTypes.h` |
| Platform（实现） | `src/Platform/Windows/WindowsProcessTuning.{h,cpp}` |
| Platform（拓扑） | `src/Platform/Windows/WindowsSystemProbe.cpp` 的 `ReadProcessorTopology` |
| Core（门面） | `src/Core/SamplingCoordinator.{h,cpp}` |
| Core（规则） | `src/Core/ProcessRules.{h,cpp}` |
| UI（侧栏） | `src/UI/Controls/ProcessDetailPanel.{h,cpp}` |
| UI（宿主） | `src/UI/Views/ProcessesView.cpp` |
| 测试 | `tests/unit/test_process_rules.cpp` |

---

## 1. 面板内容

侧栏分四张卡片，顺序即使用顺序。

### 1.1 进程摘要

图标名、可执行文件全路径、PID、CPU、内存、挂起状态。路径来自 `QueryFullProcessImageNameW`，
用**增长式缓冲区**读取：固定缓冲区会截断长路径，而被截断的路径比没有路径更糟——它指向一个不存在的文件。

### 1.2 优先级（Priority）

四组设置，各自独立「下拉 + Apply」。**必须分开提交**，因为四者背后是四个不同的 API，
系统完全可能接受其中一个而拒绝另一个；一个共用的 Apply 只能把失败归到用户没碰过的那一行。

| 行 | 原生 API | 取值 |
| --- | --- | --- |
| CPU | `SetPriorityClass` / `GetPriorityClass` | Idle / BelowNormal / Normal / AboveNormal / High |
| Memory | `SetProcessInformation(ProcessMemoryPriority)` | VeryLow(1) … Normal(5) |
| I/O | `NtSetInformationProcess(ProcessIoPriority = 33)` | VeryLow(0) … High(3) |
| Efficiency | `SetProcessInformation(ProcessPowerThrottling)` | SystemManaged / On / Off |

要点：

- **Realtime 不在下拉里。** 内核愿意授予它，而授予之后一个进程可以让整台机器的输入线程与磁盘线程饿死。
  系统任务管理器也只在带警告的进阶入口提供它。把它摆在 "High" 旁边等于邀请用户做出唯一一个能让系统不可用的选择。
  进程本身是 Realtime 时，下拉清空而不是停在无关项上。
- **Memory priority 的枚举值刻意与内核一致（1..5）**，这样 Platform 侧是 `static_cast` 而不是查表。
- **I/O priority 是 `PROCESSINFOCLASS` 里的 33**，不在公开的 `PROCESS_INFORMATION_CLASS`（那一套是 kernel32 的表面）
  里，也没有 kernel32 包装；只能走 `NtSetInformationProcess`。
- **Efficiency mode 是三态而不是开关。** 控位掩码（`ControlMask`）说明「调用方是否有意见」，
  状态掩码（`StateMask`）说明「意见是什么」。从未被改过的进程报告「没有意见」，
  这与「被显式排除在节流之外」是两件不同的事；把二者都显示为 Off 会让进程在被切换过一次之后显示错误的当前值。
- **提高 CPU 或 I/O 优先级需要 `SeIncreaseBasePriorityPrivilege`**，只有提升过的令牌持有它。
  该特权在作用域内启用、退出作用域即还原，避免用户点过一次之后本应用的权限被永久放宽。
  未持有时按「需要管理员权限」报告，而不是报一个无信息的 access denied。

### 1.3 CPU 核心（CPU cores）

- 每逻辑处理器一个可点选的芯片，外加预设按钮：All / None / Invert / One per core / P-cores / E-cores
  （后两个只在混合架构机器上出现）。
- 芯片是**暂存**的：点选只改待提交的掩码，按下 "Apply CPU selection" 才写 `SetProcessAffinityMask`。
  改一个运行中进程的亲和性是真实变更，不应随每一次点击发生。
- 空掩码在本地就被拒绝并说明原因，而不是把 `ERROR_INVALID_PARAMETER` 转发给用户。
- 超出本处理器组的位会被清掉：内核会拒绝这样的掩码，而拒绝信息读起来与用户能做的事毫无关系。
- 掩码限制在**一个处理器组**（64 逻辑处理器以内）。机器有多个组时面板会明说「本编辑器只覆盖组 0」，
  而不是假装能表达全部——`GetProcessAffinityMask` 本身就只返回一个组的掩码。

#### 芯片标签的含义

```
P0 T1   P0 T2      第 0 个性能核的第 1、2 个线程
P1 T1   P1 T2      ...
E0      E1   ...   第 0、1 个能效核（单线程，因此没有 T 后缀）
C3      C3 T2      非混合架构机器上没有「性能/能效」之分，用 C 表示物理核
```

标签里不带 "CPU" 前缀：卡片自己的标题与说明已经交代了这一排是处理器，每个芯片再重复一次是同一个词写三十遍。
工具提示里给出完整信息（物理核序号、逻辑处理器序号、第几个线程）。

### 1.4 进程状态与规则（Process state / Saved rule）

- **挂起 / 恢复**：`NtSuspendProcess` / `NtResumeProcess`（运行时从 ntdll 解析）。
  挂起会弹确认框——它可能丢失未保存的工作；恢复不确认，因为它只是把用户已经选过的状态放回去，
  在回退路上加一道确认等于给纠错设障碍。
- **保存为规则**：按可执行文件名记住当前这组设置，下次该程序启动时自动套用。
  规则存的是**进程当前实际具有的值**，不是控件里暂存的选择——否则规则会悄悄应用一个用户从未提交的设置。
  亲和性只在进程确实被限制时才写入规则：把 28 个处理器全写进去的规则换一台核心数不同的机器就会出错，
  而「换机器仍可用」正是规则存在的意义。

---

## 2. 规则文件

路径：`%LOCALAPPDATA%\TaskManagerPlusPlus\process-rules.json`（与 `settings.json` 同目录，**独立文件**）。

独立成文件的原因：规则由一次显式动作逐条产生、比任何其他设置都长寿，而且是唯一一个用户很可能手写、
甚至在机器之间复制的部分。分开存放也意味着一份损坏的规则文件不会连带毁掉窗口布局与图表配色。

```json
{
  "version": 1,
  "rules": [
    {
      "executable": "game.exe",
      "enabled": true,
      "settings": {
        "cpuPriority": "aboveNormal",
        "memoryPriority": "normal",
        "ioPriority": "high",
        "efficiencyMode": "off",
        "affinityMask": 15
      }
    }
  ]
}
```

- 枚举写成**名字**而不是整数：文件是要给人读、给人改的，`"aboveNormal"` 说明了自己的意思，`3` 没有。
- 无法识别的名字会让该项**保持未设置**，而不是回落到默认值。规则里多了一个用户没要求的设置，
  比少一个更糟。
- 一个既不命名可执行文件、又什么都不要的规则在加载时就被丢弃：它永远不会匹配，或者永远不会生效。
- 解析全部走不抛异常的路径。文件是给人手改的，一个错字应该让用户失去规则，而不是失去这次会话。

### 2.1 匹配与触发语义

规则在**采样线程**上评估，紧接在每一次发布快照之后：

- 这是新进程第一次被看见的地方；
- 在 UI 线程上为每个新进程开句柄会给某一帧加上无界开销。

引擎按「PID + 创建时间」记住已经处理过的进程身份：

| 情形 | 行为 |
| --- | --- |
| 同一进程被连续采样 | 只应用一次 |
| 进程退出后同名进程再次启动（PID 可能复用） | 视为新进程，再次应用 —— 这正是该功能的意义 |
| 两条规则命名同一个可执行文件 | 只应用第一条；否则同一样本里两条都会生效，后者静默覆盖前者 |
| 进程报告了空映像名（Idle、Memory Compression） | 不匹配 |
| 规则被编辑 | 已处理集合被清空，改动在下一个样本生效，而不是等到目标程序下次启动 |
| 进程在应用前已退出 | 不算新闻，不报告 |
| 应用被拒绝 | 记录并通过事件队列报告给 UI |

「已处理」集合每个样本从当前快照**重建**而不是累积：一台每秒都启动进程的机器上，累积会无界增长。

规则的应用结果通过一个队列交给 UI 排空，而不是从采样线程回调进 UI。

---

## 3. 撤销

- 每次成功应用前记录 `(要改的项, 改之前的值)`，按进程身份入栈。
- 每个进程最多保留 **8 步**；最多保留 **16 个进程**的历史，超出即丢弃最久未触碰的那个进程的历史。
- 撤销**只还原当时被改动的那几项**，不整份回写记录的状态：后者会把进程在这期间自行改变的设置也一起重置，
  而那并不是用户要求的变更。
- 撤销本身不入栈，否则按钮可以来回打转。
- 若被改动的设置此刻已不可读，说明「无法还原」而不是给一个看起来没反应的按钮。

---

## 4. 列表单元格热力底色

进程页的 CPU / 内存 / 磁盘 / 网络 / GPU 列，单元格底色按该行数值填充：**从全透明到 `#08336E`**。

- 归一化基准是**当前可见行里该列的最大值**，与系统任务管理器一致：
  最忙的一行总是画到最深，其余按比例。固定标尺会让空闲机器的每一格都看不见，
  而且同一个深浅在不同列里代表完全不同的量。
- 深色只有一个（`#08336E`），渐变的是 **alpha**。单元格底色与它上面的文字因此永远不会「各自是一种颜色」。
- 32 级量化，画笔在启动时一次建好。整表重绑会触碰屏幕上每一行的每一个指标格，
  为每一格新建一个画笔等于把几百次分配放进一条「重排列表就会跑」的路径上（约束 P-004）。
  量化同时让数值相近的两行共用同一支画笔，不再在两种肉眼难分的深浅之间闪。
- 取不到的值（首帧速率为未知、逐进程网络尚未采集）**不着色**，与它们显示的破折号一致。
- 单元格是 `Border` 包 `TextBlock` 而不是裸 `TextBlock`：`TextBlock` 的 `Background` 只覆盖字形自身的方框，
  而底色需要填满整列。四周留 1 像素，让相邻两列的色带之间保留与两行之间同一条细线。

---

## 5. 与分层架构的关系

- Platform 层**只读只写系统状态**，不做任何速率换算；`WindowsProcessTuning` 是无状态的，每次调用自行开关句柄。
- **UI 不直接调用任何 Windows API**：侧栏调用 `SamplingCoordinator` 上的门面方法，
  创建时间校验（防 PID 复用）在 Core 层完成。
- 规则引擎的「应用」动作是**注入**的（`ProcessRuleEngine::Applier`），因此匹配逻辑可以在没有机器可跑的情况下测试。
- `WindowsProcessTuning` 属于 Platform 的工厂头，**UI 不包含它**；UI 只包含 `Platform/ProcessTuningTypes.h` 这样的中性类型头。

---

## 6. 实测修正记录

实现过程中有两条被真实机器推翻的假设，写在这里以免后来者重犯。

### 6.1 `EfficiencyClass` 的方向与字段名相反

`PROCESSOR_RELATIONSHIP::EfficiencyClass` 的名字暗示「越大越省电」，据此把 0 当作性能核是**错的**。

实测机器：**Intel Core i7-14700KF**，8 个性能核（支持超线程）+ 12 个能效核。Windows 报告：

```
logical= 0 physicalCore= 0 class=1 primary=1     ← 两个逻辑处理器共享一个物理核
logical= 1 physicalCore= 0 class=1 primary=0
...
logical=15 physicalCore= 7 class=1 primary=0
logical=16 physicalCore= 8 class=0 primary=1     ← 单线程物理核
...
logical=27 physicalCore=19 class=0 primary=1
```

即 **8 个双线程核（性能核）拿到 class 1，12 个单线程核（能效核）拿到 class 0**。
按字段名反读会让这台机器的全部 28 个处理器被贴反标签。

判定「是否混合架构」也因此不能拿某一个值去和 0 比，而要比较**最小与最大类值是否不同**：
两家厂商的处理器并不约定同一个数字代表性能核。

代码中的结论：**类值越大 = 性能越强**，性能类取「本机出现过的最大类值」。

### 6.2 超线程是判断物理核的独立证据

上面那条结论之所以能确定，靠的不是文档而是**硬件事实**：能效核不支持超线程。
于是「两个逻辑处理器共享一个 physicalCoreIndex」这件事本身就足以指出性能核，
与厂商给的编号无关。拓扑读取据此把同一物理核的线程聚在一起（`primaryThread`），
芯片标签的 `T1/T2` 与「One per core」预设都建立在它之上。

### 6.3 挂起状态没有查询接口

内核不提供「这个进程是否被挂起」的查询，状态是每个线程的属性。本项目的做法是在**已有的批量快照**里
顺带读出来：`SYSTEM_PROCESS_INFORMATION` 后面内联着 `SYSTEM_THREAD_INFORMATION` 数组，
所有线程都停在 `ThreadState == StateWait` 且 `WaitReason == Suspended` 时进程才是被挂起的。
一个线程都没有的进程报告为运行中——零线程是进程正在被拆毁时的瞬时状态，把它显示成「已挂起」
会让用户看到一个自己从未选择过的状态。

`ThreadState` / `WaitReason` 的取值 `winternl.h` 只声明了结构体、没有声明枚举，
因此这两个常量在 `WindowsProcessProbe.cpp` 里显式命名（内核 ABI 自 Windows 2000 未变）。

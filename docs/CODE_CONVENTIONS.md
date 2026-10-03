# 代码规范

本规范适配自 Cubium 项目规范（`E:\dev\minecraft-reborn-branch-1\docs\CODE_CONVENTIONS.md`），
并按本项目需求调整。

> **与 Cubium 的关键差异**：Cubium 要求注释使用简体中文。本项目**所有注释、日志、
> 标识符与提交信息一律使用英文**；仅项目文档（`docs/` 与 README）使用中文。

## 1. 命名规范

| 类型 | 规范 | 示例 |
| --- | --- | --- |
| 头文件 | `PascalCase.h` | `ProcessModel.h` |
| 源文件 | `PascalCase.cpp` | `ProcessModel.cpp` |
| 测试文件 | `test_*.cpp` | `test_process_model.cpp` |
| 类 / 结构体 | `PascalCase` | `class ProcessModel;` |
| 接口 | `I` 前缀 + `PascalCase` | `class IProcessProbe;` |
| 成员变量 | `m_` + `camelCase` | `m_workingSetSize` |
| 静态成员 | `s_` + `camelCase` | `s_nextId` |
| 全局变量 | `g_` + `camelCase` | `g_defaultInterval` |
| 局部变量 | `camelCase` | `processCount` |
| 函数 | `PascalCase` | `SampleProcesses()` |
| 私有方法 | `_` 前缀 + `PascalCase` | `_computeDelta()` |
| 常量 | `UPPER_SNAKE_CASE` | `MAX_HISTORY_POINTS` |
| 枚举值 | `PascalCase`（scoped enum） | `enum class ProcessState { Running, Suspended };` |
| 命名空间 | 全小写 | `tmpp::platform::windows` |

### 说明

- 私有方法使用 `_` 前缀，沿用 Cubium 规则。
- 由于本项目的 UI 由 C++/WinRT 构建，**XAML 相关的类成员**遵循 WinUI 惯例
  （公开方法用 `PascalCase`），其余业务代码遵循上表。

## 2. 命名空间

```cpp
namespace tmpp {
namespace platform {
namespace windows {

enum class ProcessState : uint8_t {
    Running,
    Suspended
};

}}}  // namespace tmpp::platform::windows
```

- **禁止** `using namespace std;`
- 仅在单个 `.cpp` 内使用的符号放入**匿名命名空间**，并加 `_` 前缀，不使用 `static`。
- 头文件中**禁止**使用 `using namespace`。

## 3. 格式规范

沿用 Cubium 的 `.clang-format`（基于 LLVM 风格）：

- 缩进 4 空格，不使用 Tab
- 列宽 120
- 指针/引用左对齐（`int* ptr`）
- `AfterFunction: true`（函数大括号换行）
- `AccessModifierOffset: -4`
- 包含排序：对应头文件 → 项目头文件 → 标准库 → 第三方库

同时提供 `.clang-tidy` 与 `.clangd` 配置。

> **注意**：这些配置文件**仅作为编辑器 / IDE 的格式化依据**，**不参与构建**。
> 构建使用 MSVC，不调用 clang-format。

## 4. C++20 特性使用

| 特性 | 建议 |
| --- | --- |
| `auto`、结构化绑定 | ✅ 推荐 |
| `std::optional`、`std::variant` | ✅ 推荐 |
| `std::string_view` | ✅ 推荐（只读字符串参数） |
| `std::span` | ✅ 推荐（数组视图） |
| `std::format` | ✅ 推荐（替代 `sprintf`） |
| `if constexpr`、concepts、ranges | ✅ 推荐 |
| `std::jthread` + `std::stop_token` | ✅ 推荐（后台采样线程） |
| `std::any` | ⚠️ 谨慎使用 |

### 禁止使用

```cpp
// ❌ C 风格强制转换 —— 用 static_cast 等
int* ptr = (int*)malloc(sizeof(int));

// ❌ 原始数组 —— 用 std::array / std::vector
int arr[10];

// ❌ 宏定义常量 —— 用 inline constexpr
#define MAX_PLAYERS 100

// ❌ 裸 new / delete —— 用智能指针
Type* p = new Type();  delete p;

// ❌ 异常处理正常错误流 —— 用 Result<T>
try { ... } catch (...) { ... }
```

## 5. 错误处理

使用 `Result<T>` 模式（对齐 Cubium）：

```cpp
Result<std::vector<ProcessInfo>> EnumerateProcesses();
```

- 使用 `[[nodiscard]]` 标记必须检查返回值的函数。
- 错误对象携带错误码、消息与来源位置。
- **不鼓励**使用异常处理正常错误流。

## 6. 断言

```cpp
#include "Core/Assert.h"

TMPP_ASSERT_RELEASE(index < capacity);
TMPP_ASSERT_RELEASE_MSG(index < capacity, "Index out of range");
TMPP_UNUSED(unusedParam);
```

- 需要大量断言以保证运行时能及时暴露问题。
- **不要过度防御性编程**：只在系统边界与不可信输入处做防御检查，
  其余位置假设前置条件成立，用断言代替冗余 `if`。

## 7. 日志

使用 spdlog，规则：

1. 所有日志内容使用**英文**。
2. 允许级别：`info`、`warn`、`error`、`critical`。
   **禁止** `trace` 与 `debug` 进入发布构建（性能考虑）。
3. 任何偏离正常路径的异常情况（回退、能力缺失、降级、未实现）必须用
   `warn` 及以上级别记录，**不得**用 `info` 掩盖。
4. 重复的失败日志必须限流。
5. 渲染路径禁止无节流日志。

## 8. 注释与文档

1. **所有注释使用英文**。
2. 头文件中使用 Doxygen 风格文档注释。
3. 行内注释解释「为什么」而非「是什么」。
4. 代码区域使用分隔线标记：

```cpp
// ============================================================================
// Process enumeration
// ============================================================================
```

5. **强制 TODO 标记**：所有暂时的简化实现、不完整实现、因未实现而暂时未使用的
   代码 / 函数 / 变量，必须加带明文 `TODO` 的注释。若发现已有代码逻辑不完整
   却缺少 TODO，应顺手补上。

```cpp
// TODO: Replace with SMBIOS parsing once GetSystemFirmwareTable path is validated.
// FIXME: Handle counter rollback when PID is recycled within one sampling interval.
// NOTE: This is a hot path; do not add allocations here.
```

6. **禁止**出现「参考 XX 项目」这类无信息量的注释。

## 9. include 规范

```cpp
#include "ProcessModel.h"          // ✅ 对应头文件
#include "Domain/SamplingConfig.h" // ✅ 项目头文件，从根目录起算
#include <vector>                  // ✅ 标准库
#include <spdlog/spdlog.h>         // ✅ 第三方库

#include "../../Platform/Windows/WindowsProcessProbe.h"  // ❌ 禁止使用 ../
```

## 10. 内存与性能

- 优先 `std::unique_ptr`，共享所有权才用 `std::shared_ptr`。
- 容器预分配容量（`reserve`），用 `emplace_back` 代替 `push_back`。
- 大对象用 `const&` 传递，只读字符串用 `std::string_view`。
- 循环中缓存 `size()`。
- 热路径（采样、渲染）禁止动态分配与字符串格式化。

## 11. 界面代码的特殊约定

由于 UI 由 C++ 构建（不使用 XAML），需额外遵守：

1. **控件树的构建与逻辑分离**：视图类负责构建控件树与响应事件，
   不直接调用 Windows API，不直接访问 Platform 层。
2. **`using` 声明放在文件作用域**：C++/WinRT 命名空间较深，
   在每个 `.cpp` 顶部用 `using` 声明引入所需类型，便于阅读。
3. **运行时类不得标记 `final`**：C++/WinRT 生成的激活工厂会派生自实现类型。
4. **不要使用预编译头**：WinUI 投影头会导致 PCH 膨胀并触发 `C1076`。

## 12. Git 规范

提交信息格式（**英文**）：

```
<type>(<scope>): <subject>
<body>
```

类型：`feat` / `fix` / `refactor` / `perf` / `docs` / `test` / `build` / `chore`

- 不允许使用线性历史（rebase）。
- 非大型特性不新开分支，直接提交到 `main`。

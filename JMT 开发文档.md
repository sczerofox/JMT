# JMT（Java Manager Tool）开发文档 V1.7

> Windows 平台 JDK 版本管理工具 | C++17 | 纯 Win32 API | 无第三方依赖

本文档依据仓库当前源码（分支 `hotfix`，包含 `include/` / `src/` 按模块拆分的重构）整理，描述**实际实现**而非设计意图；与实际代码不一致的地方在「10.4 已知问题与实现差异」中明确列出。

---

## 目录

1. [项目概述](#1-项目概述)
2. [架构总览](#2-架构总览)
3. [命令层](#3-命令层)
4. [服务层（jdk/）](#4-服务层jdk)
5. [系统与基础设施](#5-系统与基础设施)
6. [数据流与典型场景](#6-数据流与典型场景)
7. [提权策略](#7-提权策略)
8. [异常处理与退出码](#8-异常处理与退出码)
9. [构建、测试与发布](#9-构建测试与发布)
10. [附录](#10-附录)

---

## 1. 项目概述

### 1.1 简介

JMT 是一款 Windows 平台命令行 JDK 版本管理工具（类似 NVM for Node.js）：扫描本机 JDK、切换默认版本、下载安装新版本、维护 PATH 环境变量，并保留回收站以便回退。

**核心特性**

- 交互式 REPL（无参启动进入 `jmt>` 提示符）与单次命令模式
- 固定磁盘全盘扫描，识别合法 JDK（校验 `bin\java.exe` + `bin\javac.exe` + `lib` 目录）
- 直接读写注册表中的系统 PATH（不使用 `JAVA_HOME`），修改后广播 `WM_SETTINGCHANGE`
- 多源下载：内置华为云镜像 + 可扩展的 `.repo` 外部源 + Adoptium 官方回退
- 下载引擎双通道：`curl.exe` 优先，失败回退 WinHTTP Range 多线程分片
- 删除先进回收站（`.trash`），支持 `rollback` 还原
- 极简自带测试框架 + CTest，业务代码统一编译为静态库 `jmt_core` 供 exe 与测试共用

### 1.2 系统要求

- Windows 10/11 x64
- 运行时无第三方 DLL 依赖（静态链接 CRT，`MSVC_RUNTIME_LIBRARY=MultiThreaded`）
- 下载依赖系统自带 `curl.exe`（可选）与 PowerShell `Expand-Archive`

### 1.3 命令总览

共 **11 个命令**（`src/main.cpp` 中注册），提权方式见第 7 章。

| 命令 | 功能 | 提权位置 |
|------|------|----------|
| `search [--force]` | 扫描 JDK；PATH 无 JDK 时自动设置最大版本 | `main.cpp` + 命令内 |
| `list` | 列出所有已识别版本并标注当前版本 | 无需 |
| `use <ver>` | 切换 PATH 中的 JDK `bin` 条目 | `main.cpp` + 命令内 |
| `env` | 把 JMT 自身目录加入 PATH | `main.cpp` + 命令内 |
| `remove <子命令>` | 删除 JDK / 清理环境 / 清空回收站 | `main.cpp` + 命令内 |
| `download <ver>` | 下载并安装 JDK（镜像/EXE/官方） | `main.cpp` + 命令内 |
| `data <output\|input>` | 导出 / 导入 JDK 列表 | 命令内（仅 `input`） |
| `rollback <ver\|list>` | 从回收站恢复 JDK | 命令内（`list` 除外） |
| `shell` | 打开新终端进入交互模式并关闭旧窗口 | 无需 |
| `version` | 显示版本信息 | 无需 |
| `help [命令]` | 显示帮助或命令详情 | 无需 |

---

## 2. 架构总览

### 2.1 分层结构

```
┌────────────────────────────────────────────────────────────┐
│ 入口层  src/main.cpp                                        │
│ InitConsole → reloadMappings → 构建 JmtContext → 注册命令    │
│ → REPL（无参）/ 提权判断 + 单次执行                          │
└──────────────────────────┬─────────────────────────────────┘
                           │
┌──────────────────────────▼─────────────────────────────────┐
│ 命令层  include/command/ + src/command/                     │
│ CommandBase 接口 → CommandRegistry 映射 → 11 个命令实现      │
│ 每个命令：解析参数 → （必要时提权）→ 调用服务层 → 输出结果   │
└──────────────────────────┬─────────────────────────────────┘
                           │
┌──────────────────────────▼─────────────────────────────────┐
│ 服务层  include/jdk/ + src/jdk/                             │
│ JdkScanService       全盘扫描 + 缓存读写与自愈               │
│ JavaEnvService       PATH 中 JDK 条目增删改查                │
│ JdkDownloadService   多源映射 + 下载 + 解压 + 嵌套修复       │
│ JmtPathService       JMT 自身 PATH 注册                      │
└──────────────────────────┬─────────────────────────────────┘
                           │
┌──────────────────────────▼─────────────────────────────────┐
│ 支撑层                                                       │
│ system/   RegistryOperator / PathUtils / ElevationHelper /   │
│           FileLock / utils                                   │
│ network/  MultiThreadDownloader（WinHTTP Range 分片）        │
│ console/  color_print / console_progress / repl_engine /     │
│           repl_utils                                         │
│ common/   StringHelper                                       │
└────────────────────────────────────────────────────────────┘
```

### 2.2 目录与模块映射

头文件与实现物理分离：`include/` 是头文件搜索根目录（`target_include_directories`），`src/` 目录名与 `include/` 下模块名一一对应，因此所有包含都写模块前缀：

```cpp
#include "command/command_base.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "platform/output.hpp"
#include "app/app_context.hpp"
```

```
include/                       src/
├── app/                       ├── app/        app_paths / app_context / app_runtime / elevation_gate
├── command/                   ├── command/    command_registry.cpp + 11 个 *_command.cpp
├── jdk/                       ├── jdk/        4 个服务实现（实例类，注入端口）
├── platform/                  ├── platform/   console_output / win_registry / win_elevator（端口适配器）
├── system/                    ├── system/     path_utils / file_lock / utils（无状态工具）
├── network/                   ├── network/    multi_thread_downloader.cpp
├── console/                   ├── console/    repl_engine / repl_utils
└── common/                    ├── common/     string_helper.cpp + exit_code.hpp / result.hpp
                               └── main.cpp    入口（不属于 jmt_core）
resources/
├── app.rc                     仅包含 IDI_ICON1 ICON "app.ico"
└── app.ico
tests/                         自带框架 + CTest（.gitignore 排除，仓库不发布）
```

依赖方向：`common`（纯值类型）→ `platform`（端口 + Win32 适配器）/ `system` / `network` → `jdk`（服务）→ `command`（用例）→ `app`（组合根）；`main.cpp` 只做装配与分发。端口层刻意不含 `windows.h`，命令层与服务层只依赖端口，不直接触碰注册表/控制台。

### 2.3 核心抽象

**AppContext**（`include/app/app_context.hpp`）+ **AppPaths**（`include/app/app_paths.hpp`）

```cpp
struct AppPaths {                 // 运行期路径的唯一来源
    std::wstring exeDir, exePath, cacheFile, tempDir, trashDir, repoDir, dataDir;
    static AppPaths rootedAt(const std::wstring& root);   // 测试指向临时目录
    static AppPaths fromExecutable();
};

struct AppContext {               // 命令层可见的全部依赖
    AppPaths paths;
    IOutput* out = nullptr;       // 输出端口
    IRegistry* registry = nullptr;// 环境变量端口
    IElevator* elevator = nullptr;// 提权端口
    JdkScanService* scan = nullptr;
    JavaEnvService* env = nullptr;
    JdkDownloadService* download = nullptr;
    JmtPathService* jmtPath = nullptr;
    bool isInteractive = false;
    bool isElevated = false;
};
```

**CommandBase / CommandRegistry**（`include/command/command_base.hpp`、`command_registry.hpp`）

```cpp
class CommandBase {
public:
    virtual ~CommandBase() = default;
    [[nodiscard]] virtual std::wstring name() const = 0;                 // L"use"
    [[nodiscard]] virtual bool requiresElevation() const { return false; }
    virtual ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) = 0;
    [[nodiscard]] virtual std::wstring getHelp() const = 0;
};

class CommandRegistry {   // std::map<std::wstring, std::unique_ptr<CommandBase>>
public:
    void registerCommand(const std::wstring& name, std::unique_ptr<CommandBase> cmd);
    [[nodiscard]] CommandBase* findCommand(const std::wstring& name) const;
};
```

命令名是大小写敏感的精确匹配，`args[0]` 是命令名本身（REPL 与单次模式一致）。

**端口**（`include/platform/`，均不含 `windows.h`）

```cpp
enum class EnvTarget { Auto, SystemOnly, UserOnly };        // platform/registry.hpp

class IOutput {                                             // platform/output.hpp
    virtual void line(OutputLevel level, const std::wstring& text) = 0;
    virtual void progress(const std::wstring& text) = 0;
    virtual void clearProgress() = 0;
};

class IRegistry {                                           // platform/registry.hpp
    virtual std::wstring readEnv(const std::wstring& name, EnvTarget) = 0;
    virtual bool writeEnv(const std::wstring& name, const std::wstring& value, EnvTarget) = 0;
    virtual bool deleteEnv(const std::wstring& name, EnvTarget) = 0;
    virtual std::vector<std::wstring> listEnvNames(EnvTarget) = 0;
    virtual std::wstring readPath(EnvTarget) = 0;
    virtual bool writePath(const std::wstring& path, EnvTarget) = 0;
};

class IElevator {                                           // platform/elevator.hpp
    virtual bool isElevated() = 0;
    virtual bool relaunchElevated(const std::wstring& commandLine) = 0;
};
```

**ExitCode / Status / Result**（`include/common/exit_code.hpp`、`result.hpp`）：命令返回 `ExitCode`（数值仍是 0~4，见 8.1），服务可用 `Status` / `Result<T>` 携带错误文案。

---

## 3. 命令层

所有命令位于 `src/command/`，命名 `<名称>_command.cpp`，类名 `<名称>Command`。通用模式：

1. 解析参数（剔除 `--user` / `--sys` 等目标参数）
2. 若需要且当前未提权 → 拼装完整命令行调用 `ElevationHelper::RelaunchElevated`，成功则父进程 `return 0`
3. 调用服务层
4. 通过 `PrintInfo` / `PrintSuccess` / `PrintWarning` / `PrintError` 输出，返回退出码

### 3.1 search — 扫描与自动配置

- 仅识别 `--force`（大小写不敏感），其它参数忽略
- `JdkScanService::scanJdks(force, ctx.cacheFilePath)` → 结果为空返回 `2`
- `JavaEnvService::getCurrentVersion()` 非空：只报告结果和当前版本，提示用 `jmt use <版本号>` 切换，返回 `0`
- PATH 中无 JDK：`std::max_element` 按 `std::stoi(版本号)` 取最大版本 → `JavaEnvService::setCurrentJdk(最大版本路径, EnvTarget::Auto)`，失败返回 `3`

### 3.2 list — 列出版本

- `scanJdks(false, cachePath, /*silent=*/true)`；空列表返回 `2`
- 匹配当前版本的条目用 `PrintSuccess` 高亮，输出「版本总数」与「当前生效版本 / 当前没有 JMT 管理的 JDK 版本」

### 3.3 use — 切换版本

- 缺少版本号：`1`；缓存中找不到该版本：`2`
- 调用 `JavaEnvService::setCurrentJdk(targetPath, EnvTarget::Auto)`，失败返回 `3`
- 成功后提示「请重启终端使环境变量生效」

### 3.4 env — 注册自身 PATH

- 调用 `JmtPathService::registerJmtPath(ctx.exeDirectory, EnvTarget::Auto)`
- 失败返回 `3` 并提示需要管理员权限

### 3.5 remove — 删除与清理

参数预处理：`--user` → `EnvTarget::UserOnly`，`--sys` → `EnvTarget::SystemOnly`，其余进入 `filteredArgs`。缺子命令返回 `1`。

| 子命令 | 实现要点 |
|--------|----------|
| `env` | 读 PATH → `PathUtils::removeEntries(entries, ctx.exeDirectory)` → 重写 PATH |
| `all` | 需按 `y` 确认；`clearCurrentJdk`（含恢复 Oracle javapath）→ 移除 JMT 目录 → `fs::remove_all` 删 `.trash` / `.temp` → 删 `.jmt_cache` → 枚举环境变量删除所有以 `JAVA_HOME` 开头的变量 |
| `temp` | `fs::remove_all(exeDir\.temp)`，不存在则直接返回 `0` |
| `trash` | 需按 `y` 确认，`fs::remove_all(exeDir\.trash)` |
| `<版本号>` | 正则 `^\d+$` 匹配；详见下方流程 |

`remove <版本号>` 流程：

1. `scanJdks(false)`，空则 `scanJdks(true)` 重扫，仍为空返回 `2`；找不到版本返回 `2`
2. PATH 处理：
   - 当前版本 == 待删版本：在剩余版本中取最大者 `setCurrentJdk`；无剩余版本则 `clearCurrentJdk`（失败返回 `3`）
   - 否则：仅从 PATH 移除 `<路径>\bin` 条目
3. 目录处理：`MoveFileW` 到 `.trash\jdk-<版本>_<yyyyMMdd_HHmmss>`；跨卷失败时 `fs::copy(recursive)` + `fs::remove_all`，异常返回 `4`；随后写入 `.original_path` 元数据
4. 缓存更新：从列表中剔除该版本后 `writeCache`
5. 兼容清理：删除 `JAVA_HOME<版本>` 变量

### 3.6 download — 下载安装

参数解析：`--mirror` → `useMirror`，`java` → `forceOfficial`，`exe` 仅识别后跳过（当前与默认策略等价，见 10.4），其余第一个非选项参数作为版本号。版本号用 `^(\d+)` 提取主版本。

分支：

| 条件 | 调用 |
|------|------|
| `forceOfficial` | `JdkDownloadService::downloadFromOfficial(版本)` |
| `useMirror` | `JdkDownloadService::downloadFromMirror(版本)` |
| 默认 | `JdkDownloadService::downloadAndInstall(版本)` |

覆盖安装：若缓存中已存在该版本，先询问 `y/n`；确认后 `MoveToTrash`（与 `remove` 相同的目录结构与元数据）→ 更新缓存 → 当前版本被删则切到最大版本或清空 PATH → 清理 `JAVA_HOME<版本>`。

安装成功后：`scanJdks(true)` 强制重扫，按「路径等于安装路径 or 版本号等于目标版本」设置当前版本；都没匹配到则退化为设置最大版本。返回值为 `EXE_DOWNLOADED` 时表示只下载了 EXE 安装包（返回 `0`），空字符串表示失败（返回 `4`）。

### 3.7 data — 导入导出

- `output`：`scanJdks(false, cachePath, true)` → 写入 `exeDir\.data\ver_out.txt`，每行 `版本号|安装路径`（UTF-8 带 BOM）
- `input`：提权后读取 `ver_out.txt`，逐行解析（容忍 `\r`）；
  - `isValidJdk(path)` 为真 → 跳过并计数
  - 否则交互询问 `y/n`；确认后以该路径的**父目录**为安装根调用 `downloadAndInstall(版本, 安装根)`
  - 汇总输出「无需安装 / 成功安装 / 已下载 EXE / 失败」四项计数，有失败则返回 `4`，否则 `0`；有成功安装时强制重扫刷新缓存

### 3.8 rollback — 回收站回退

- `rollback`（不带参数）与 `rollback list` 等价：列 `.trash\jdk-*` 目录，用 `^jdk-(.+)_\d{8}_\d{6}$` 提取版本（版本可含点/下划线，如 `17.0.9`、`1.8.0_202`），读取 `.original_path`（经 `CleanPath` 去控制字符与尾分隔符）后输出；不需要提权。列表末尾会打印恢复用法
- `rollback <版本>`：提权后按 `jdk-<版本>_*` 匹配、以 `ftCreationTime` 取最新条目；`.original_path` 缺失返回 `4`；原路径已存在返回 `2`；父目录用递归辅助函数创建，失败返回 `3`
- 恢复：`MoveFileW` 优先；失败则 `ShellExecuteExW(runas)` 执行 `xcopy /E /I /Y` 并等待退出码，成功后删除回收站副本
- 最后删除 `.original_path`、强制重扫刷新缓存

### 3.9 shell / version / help

- `shell`：`ShellExecuteW(cmd.exe, "/k \"<exeDir>\\jmt.exe\"")` 打开新窗口，再用 `WriteConsoleInputW` 向当前控制台注入 `exit\r`，让旧窗口自行退出
- `version`：`PrintInfo(L"JMT v1.7 (build 2026.07.13)")`（硬编码）
- `help`：无参数打印分组帮助；`help <命令>` 先 `registry_.findCommand` 取 `getHelp()`，再对 `search` / `download` / `remove` / `data` / `rollback` 追加子命令说明；未知命令提示错误但返回 `0`

---

## 4. 服务层（jdk/）

### 4.1 JdkScanService — 扫描与缓存

```cpp
static std::vector<std::pair<std::wstring, std::wstring>> scanJdks(
        bool force, const std::wstring& cachePath, bool silent = false);
static bool isValidJdk(const std::wstring& path);
static std::wstring extractVersion(const std::wstring& path);
static void writeCache(const std::vector<std::pair<std::wstring, std::wstring>>& jdks,
                       const std::wstring& cachePath);
```

**合法性校验** `isValidJdk`：目录名为 `jre` 直接排除；必须同时存在 `bin\java.exe`、`bin\javac.exe` 和 `lib` 目录。

**版本提取** `extractVersion`：

1. 读 `<path>\release`，正则 `JAVA_VERSION="([^"]+)"` 并交给 `JavaVersion::parse`，**原样返回完整版本**（`17.0.9` / `22` / `1.8.0_202`）
2. 回退路径匹配 `(?:jdk|openjdk)[-_]?(\d+(?:[uU]\d+)?(?:\.\d+)*(?:_\d+)?)`，覆盖 `jdk-17.0.2` / `jdk1.8.0_202` / `jdk-8u202` / `jdk17` / `openjdk-11.0.2`
3. 都解析不出有效版本时返回空串（该目录不会被收录）

**重要**：目录名常常只有主版本（实测 `D:\Program Files\Java\jdk-17` 的 `release` 是 `JAVA_VERSION="17.0.9"`），所以版本必须以 `release` 为准，不能从目录名推断。

**扫描策略** `scanJdks`：

- `force == false`：先 `FileLock` 独占缓存文件 → `ReadFileText` 逐行 `版本号|路径` → 逐条 `isValidJdk` 校验；无效条目被剔除，条目数与行数不一致时立即重写缓存并返回
- 否则全盘扫描：先扫常见目录（`C:\Program Files\Java`、`C:\Program Files (x86)\Java`、`C:\jdk`、`D:\Java`、`E:\Java`、`D:\jdk`），再遍历 `GetAvailableDrives()`（仅 `DRIVE_FIXED`）根目录，递归深度 3 层
- 排除目录集合：`C:\Windows`、`C:\ProgramData`、`C:\System Volume Information`、`$Recycle.Bin`、`System Volume Information`、`Recovery`、`Temp`
- 同版本去重（`std::set<std::wstring> seen`，先到先得），结束后 `writeCache`；`silent == false` 时逐条打印「找到 JDK: <路径>」

**缓存写入** `writeCache`：`FileLock::tryLock()` 加锁后 `writeAllText`，内容是 `版本号|路径\n` 拼接（UTF-16 LE 原始宽字符、无 BOM）。读路径统一走 `system/utils.hpp` 的 `ReadFileText`，它按「UTF-16 BOM → UTF-16 启发式 → UTF-8 → ANSI」顺序解码，因此缓存与手改文件都能读。

### 4.2 JavaEnvService — PATH 中的 JDK 条目

```cpp
static bool setCurrentJdk(const std::wstring& jdkPath, EnvTarget target = EnvTarget::Auto);
static bool clearCurrentJdk(EnvTarget target = EnvTarget::Auto);
static std::wstring getCurrentVersion();
static void removeOracleJavaPath(EnvTarget target = EnvTarget::Auto);
static void restoreOracleJavaPath(EnvTarget target = EnvTarget::Auto);
```

**条目识别**（内部自由函数 `IsJdkBinPath`）：忽略大小写、忽略末尾 `\` / `/` 后，要求路径以 `bin` 结尾且整体包含 `jdk`。

**setCurrentJdk**：读 PATH → 过滤掉所有 JDK bin 条目 → `removeOracleJavaPath` → 重新读取 PATH 再过滤一次（防止不彻底）→ `addUniqueEntry` 追加 `<jdkPath>\bin` → 用 `;` 重新拼接 → `RegistryOperator::setPath`。

**clearCurrentJdk**：过滤掉所有 JDK bin 条目后写回，再调用 `restoreOracleJavaPath`（仅当 `C:\Program Files\Common Files\Oracle\Java\javapath` 目录存在且未在 PATH 中时才追加）。

**getCurrentVersion**：按「**用户 PATH 优先 → 系统 PATH**」的顺序读取（Windows 上用户 PATH 先于系统 PATH 生效；此前只读系统 PATH，导致写在用户 PATH 里的 JDK 被判定为「当前无版本」），对首个匹配 JDK bin 的条目用 `jdk(?:1\.(\d+)|[-_]?(\d+))` 提取版本号；都没有则返回空串。该函数在 `search` 中用于判断「PATH 是否已有 JDK」。

**作用域**：`setCurrentJdk` / `clearCurrentJdk` / `removeOracleJavaPath` / `restoreOracleJavaPath` 均接受 `EnvTarget`，由命令层经 `EnvScope` 解析 `--user` / `--sys` 后传入；不传则用 `Auto`（先系统、失败再用户）。

### 4.3 JmtPathService — 自身 PATH 注册

```cpp
static bool registerJmtPath(const std::wstring& exeDir, EnvTarget target = EnvTarget::Auto);
```

先 `removeEntries` 删除与 `exeDir` 相等的旧条目，再 `addUniqueEntry` 追加，写回并广播。成功时输出 `PrintDebug` 日志（仅 Debug 构建可见）。

### 4.4 JdkDownloadService — 下载与安装

```cpp
static std::wstring downloadAndInstall(const std::wstring& version, const std::wstring& installRoot = L"");
static std::wstring downloadFromMirror(const std::wstring& version, const std::wstring& installRoot = L"");
static std::wstring downloadFromOfficial(const std::wstring& version, const std::wstring& installRoot = L"");
static void reloadMappings();
```

返回值语义：安装目录路径 / `L"EXE_DOWNLOADED"`（仅下载了 EXE，需手动安装）/ 空串（失败）。

**映射管理**

- `initBuiltinMappings()`：ZIP 源覆盖主版本 11~26（含各补丁版本），EXE 源覆盖主版本 6~13；数据源为华为云 `repo.huaweicloud.com` 与 `mirrors.huaweicloud.com`
- `ensureExternalMappingFiles()`：创建/补齐 `.repo\jdk_zip_repo.txt`、`.repo\jdk_exe_repo.txt`；若检测到文件以 `FF FE` 开头（旧版遗留 UTF-16 LE）则删除重建；写入时用 `WriteFileText`（UTF-8 带 BOM），内容为注释头 + 内置 URL 列表
- `loadExternalMappings()`：逐行读取（跳过空行与 `#` 注释），`ExtractVersionFromUrl` 用 `[/-](\d+)(?:\.\d+)*[/_-]` 提取主版本后追加到对应列表
- `zipUrlsFor` / `exeUrlsFor`：返回该版本的**完整**源列表（内置在前、外部追加）

**下载计划**（`jdk/download_plan.hpp`，阶段 1 引入）

源选择被抽成纯逻辑，便于脱离网络测试：

- `DownloadMode`：`Default`（ZIP → EXE → 官方）、`MirrorOnly`、`OfficialOnly`
- `DownloadPlan::build(mode, installerOnly, zipUrls, exeUrls)`：按顺序展开为 `DownloadStep` 列表；`-demos` URL 不进入步骤，而是记录到 `skippedDemoUrls` 供提示
- `installerOnly = true`（命令行 `exe`）：计划只含 EXE 源（官方 API 只提供 ZIP，不参与该模式）
- 执行由 `JdkDownloadService::executePlan` 完成：逐步尝试，输出「源 i/n」，单个源失败继续下一个；全部失败返回空串

**安装路径**：`installRoot` 为空时用 `GetInstallRoot()`——从 `D:` 依次到 `Z:` 尝试 `Program Files\Java`，返回第一个已存在或创建成功的目录，全部失败则落地 `C:\Program Files\Java`；目标目录为 `<root>\jdk-<主版本>`。若目标目录已是合法 JDK 则直接返回，不重复下载。

**下载引擎**（`DownloadFile`）

1. `DownloadFileWithCurl`：`SearchPathW` 找 `curl.exe`（找不到回退 `C:\Windows\System32\curl.exe`），执行 `-L --retry 3 -o <目标文件> <URL> --progress-bar`，非 0 退出码即失败并删除半成品
2. 回退 `DownloadFileWithMultiThread`：`MultiThreadDownloader`，4 连接 / 60 秒超时 / 重试 3 次；额外做体积（≥ 1 MB）与 ZIP 魔数（`PK\x03\x04`）校验

**解压与修复**：`ExtractZip` 调 `powershell -Command "Expand-Archive -Path ... -DestinationPath ... -Force"`（`CREATE_NO_WINDOW`）；解压后若不是合法 JDK，则 `FixNestedJdkDirectory` 检查「只存在一个子目录且该子目录是合法 JDK」，是则把内容上移并删除空壳目录。

**官方回退**：`GetOfficialDownloadInfo` 请求 `https://api.adoptium.net/v3/binary/latest/<版本>/ga/windows/x64/jdk/hotspot/normal/eclipse`，禁用自动重定向（`WINHTTP_DISABLE_REDIRECTS`），收到 3xx 后从 `Location` 头取真实下载地址与文件名。

---

## 5. 端口与基础设施

### 5.1 WinRegistry — 注册表环境变量（IRegistry 的 Win32 适配器）

```cpp
class WinRegistry : public IRegistry {
    std::wstring readEnv(const std::wstring& name, EnvTarget target = EnvTarget::Auto) override;
    bool writeEnv(const std::wstring& name, const std::wstring& value, EnvTarget target = EnvTarget::Auto) override;
    bool deleteEnv(const std::wstring& name, EnvTarget target = EnvTarget::Auto) override;
    std::vector<std::wstring> listEnvNames(EnvTarget target = EnvTarget::Auto) override;
    std::wstring readPath(EnvTarget target) override;
    bool writePath(const std::wstring& path, EnvTarget target) override;
};
```

- 根键：`EnvTarget::SystemOnly → HKEY_LOCAL_MACHINE`，`UserOnly → HKEY_CURRENT_USER`，`Auto` 先系统后用户（`targetsFor()` 返回尝试序列）
- 子键按目标选择：系统 = `SYSTEM\CurrentControlSet\Control\Session Manager\Environment`，用户 = `HKCU\Environment`；用户级键在写模式下不存在时会 `RegCreateKeyExW` 创建
- 值类型：`REG_EXPAND_SZ`，读取时接受 `REG_EXPAND_SZ` 与 `REG_SZ`
- `Auto` 模式在系统写入失败后自动尝试用户分支；两个分支都失败时返回 `false`，由调用方决定如何上报（`remove env` / `remove all` 会检查返回值并返回退出码 3）
- 写/删成功后调用 `BroadcastEnvironmentChange()`：`SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, L"Environment", SMTO_ABORTIFHUNG, 10000)`；首次返回 0（超时/失败）时 `Sleep(500)` 后用 5 秒超时重试一次，两次都失败只写调试日志（`OutputDebugStringW`，仅 Debug 构建），不影响注册表结果

### 5.2 PathUtils — PATH 字符串处理

```cpp
static std::wstring normalize(const std::wstring& path);   // 去尾部分隔符 + 转小写
static bool arePathsEqual(const std::wstring& a, const std::wstring& b);
static std::vector<std::wstring> splitPath(const std::wstring& path);
static std::vector<std::wstring> removeEntries(const std::vector<std::wstring>& entries,
                                               const std::wstring& toRemove);
static std::vector<std::wstring> addUniqueEntry(const std::vector<std::wstring>& entries,
                                                const std::wstring& newEntry);
```

- `splitPath` 跳过空项，保留 `%VAR%` 原样（不做环境变量展开）
- 比较只统一大小写与尾部分隔符，**不统一 `/` 与 `\`**（已知限制，单元测试已固化该语义）
- 只做字符串级增删，注册表读写由 `IRegistry`（`WinRegistry`）负责

### 5.3 WinElevator — 提权适配器

```cpp
class WinElevator : public IElevator {   // src/platform/win_elevator.cpp
    bool isElevated() override;
    bool relaunchElevated(const std::wstring& commandLine) override;
};
```

- `isElevated`：`OpenProcessToken(TOKEN_QUERY)` + `GetTokenInformation(TokenElevation)`
- `relaunchElevated`：把当前 exe 路径与参数拼成 `cmd /c "<exe> <args> & pause"`，以 `runas` 通过 `ShellExecuteW` 异步启动；返回 `HINSTANCE > 32` 表示成功。父进程随后退出，实际命令在新控制台窗口中执行
- 调用方不再直接用这个适配器，而是走 7.1 的 `ElevationGate`

### 5.4 FileLock — 跨进程文件锁

`CreateFileW(OPEN_ALWAYS, FILE_SHARE_READ | FILE_SHARE_WRITE)` + `LockFileEx`（`LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY`）实现非阻塞互斥；`writeAllText` 要求已持锁，`SetFilePointer` 归零 + `SetEndOfFile` 截断后写入 UTF-16 LE 内容，用于 `.jmt_cache`。

### 5.5 system/utils — 文件与字符串工具

`GetExeDirectory()`（仅 `AppPaths::fromExecutable()` 使用）、`GetAvailableDrives()`（仅固定盘）、`IsDirectory()` / `IsFile()`、`JoinPath()`、`ReadFileText()`（多编码嗅探）、`WriteFileText()`（UTF-8 带 BOM + 覆盖写）、`ToWideString()`（ANSI → 宽字符）。控制台初始化由 `ConsoleOutput::init()` 负责。

### 5.6 MultiThreadDownloader — WinHTTP 分片下载

```cpp
void setConnections(int);   // 1~32，默认 4
void setTimeout(int);       // ≥ 5 秒，默认 30（调用方传 60）
void setRetryCount(int);    // ≥ 0，默认 3
bool download(const std::wstring& url, const std::wstring& destPath,
              ProgressCallback onProgress = nullptr, ErrorCallback onError = nullptr);
```

- 先 `HEAD` 取 `Content-Length`；拿不到或文件 < 10 MB 时降为单连接
- 按连接数均分字节区间，`Range: bytes=start-end` 分片下载到 `%TEMP%\jmt_part_<n>.tmp`，每片带重试与退避（`Sleep(1000 * (retry + 1))`）
- 线程间用 `std::mutex` 保护进度；分片启动前插入 0~300 ms 随机抖动
- 全部完成后按 `index` 顺序 `mergeParts` 合并到目标文件，随后清理分片；任一分片失败或校验不通过则整体失败并清理
- 进度通过 `ProgressCallback` 回调（百分比 + 已下载字节），但**不计算速度**（`speed` 恒为 0）

### 5.7 console 模块

- `ConsoleOutput`（`src/platform/console_output.cpp`）：`init()` 设置代码页 UTF-8 并在支持时开启虚拟终端；`line()` 按等级加 `[INFO]/[SUCCESS]/[WARN]/[ERROR]/[DEBUG]` 前缀并用 ANSI 颜色 + `WriteConsoleW` 输出，Debug 等级仅在 `_DEBUG` 构建生效；`progress()` 覆盖当前行、`clearProgress()` 清空进度行。`NullOutput` 用于静默场景
- `repl_engine.cpp`：交互循环（banner、`std::getline(std::wcin, line)`、`SetConsoleCtrlHandler` 捕获 `CTRL_C_EVENT` 后重显提示符、`exit` / `quit` 退出、未知命令提示）；每条命令先经 `ElevationGate::ensure` 再执行，命令返回非 `Ok` 时用 Debug 输出返回码
- `repl_utils.cpp`：`splitCommandLine` 按空格切分并支持双引号包裹

---

## 6. 数据流与典型场景

### 6.1 `jmt search`

```
main → 需要管理员 → SearchCommand::execute
  → JdkScanService::scanJdks(force, cache)
      [缓存命中] FileLock → ReadFileText → 逐条 isValidJdk 校验（失效则重写缓存）
      [否则]     常见目录 + 固定盘根目录递归 3 层 → 同版本去重 → writeCache
  → JavaEnvService::getCurrentVersion()
      [非空] 报告当前版本，不改环境变量
      [空]   max_element(版本) → setCurrentJdk(路径, Auto)
                 ├─ 过滤所有 IsJdkBinPath 条目
                 ├─ removeOracleJavaPath
                 ├─ 追加 <path>\bin
                 └─ RegistryOperator::setPath → WM_SETTINGCHANGE（失败重试）
```

### 6.2 `jmt use 21`

```
main（需要管理员）→ UseCommand::execute(["use","21"])
  → scanJdks(false, cache, silent=true) → 精确匹配版本 "21"，未命中返回 2
  → JavaEnvService::setCurrentJdk(path, EnvTarget::Auto) → 失败返回 3
  → 提示重启终端
```

### 6.3 `jmt download 17`

```
main（需要管理员）→ DownloadCommand::execute
  → 解析参数（--mirror / exe / java）→ 主版本号提取
  → scanJdks(false)：已存在则询问覆盖
        确认 → MoveToTrash 旧目录（.trash\jdk-17_<时间戳> + .original_path）
             → 更新缓存 → 当前版本被删则切最大版本或清空 PATH → 删 JAVA_HOME17
  → JdkDownloadService::downloadAndInstall("17")
       1) 镜像 ZIP（findZipUrl 第 0 条）→ DownloadFile → ExtractZip → 校验/修复嵌套
       2) 镜像 EXE（findExeUrl 第 0 条）→ 下载到 .temp → 返回 EXE_DOWNLOADED
       3) 官方 Adoptium API → 解析 307 Location → ZIP → 解压 → 校验/修复
  → scanJdks(true) 强制重扫 → setCurrentJdk(新版本)
```

### 6.4 `jmt remove 17`

```
main（需要管理员）→ RemoveCommand::execute（解析 --user/--sys）
  → scanJdks(false)（空则 force 重扫）→ 定位版本 17，未命中返回 2
  → 当前版本 == 17 ?
       是 → 剩余版本取最大 → setCurrentJdk；无剩余 → clearCurrentJdk
       否 → 从 PATH 移除 <jdkPath>\bin
  → MoveFileW → .trash\jdk-17_<时间戳>（跨卷则复制 + 删除）
  → 写 .original_path → 更新缓存 → 删除 JAVA_HOME17
```

### 6.5 `jmt rollback 17`

```
main → RollbackCommand::execute（list 免提权，其余提权）
  → 匹配 .trash\jdk-17_* 并按 ftCreationTime 取最新
  → 读 .original_path → CleanPath → 原路径已存在则返回 2
  → 递归创建父目录 → MoveFileW 或提权 xcopy /E /I /Y
  → 删除 .original_path → scanJdks(true) 刷新缓存
```

### 6.6 `jmt data input`

```
main（不提权）→ DataCommand::execute(["data","input"])
  → 命令内提权 → 读 .data\ver_out.txt → 逐行 版本|路径
  → isValidJdk(path) ? 跳过 : 询问 → downloadAndInstall(版本, 父目录)
  → 统计并输出汇总（有失败返回 4）→ 有成功安装则 scanJdks(true)
```

---

## 7. 提权策略

### 7.1 唯一提权入口 ElevationGate

提权判断集中在 `app/elevation_gate.hpp`，由命令元数据驱动，`main.cpp` 与 `REPL` 共用同一条路径：

```cpp
// 命令声明自己是否需要管理员权限、是否支持 --user
[[nodiscard]] bool requiresElevation() const override { return true; }   // use/env/remove/search/download
[[nodiscard]] bool allowsUserScope() const override { return true; }     // 同上五个命令

// 调用方（main / REPL）
const ElevationDecision decision = ElevationGate::ensure(
        command->requiresElevation(), command->allowsUserScope(), args, ctx);
if (!decision.proceed) return toInt(decision.code);   // 已启动提权进程或提权失败
```

- `ElevationDecision{proceed, code}`：`proceed == false` 表示「已启动提权进程（`code == Ok`）」或「提权失败（`code == PermissionDenied`，对应退出码 3）」，调用方据此停止执行当前进程的命令
- `--user` 免提权：当命令 `allowsUserScope()` 且命令行含 `--user`（`ElevationGate::hasFlag`）时直接放行，由 `EnvScope` 把 `EnvTarget::UserOnly` 传给服务层
- `data input` 与 `rollback <版本>` 这类按子命令提权的命令，使用 `ElevationGate::requestElevation(args, ctx)`（无条件请求提权）
- 命令行拼接（含空格参数加引号）由 `ElevationGate::buildCommandLine` 统一实现；提示语固定为「需要管理员权限，正在请求提权...」，失败提示「提权失败，请手动以管理员身份运行」
- 骨架阶段之前，main 与 7 个命令各自复制了一份等价实现（语义已分叉）；现在命令内部不再有任何提权代码

`main.cpp` 中的豁免分支只覆盖 `remove` 带 `--user`（以及一个永远不会命中的 `search --user` 分支），而 `RemoveCommand` 内部并无同样豁免，因此该豁免实际被命令层覆盖（见 10.4）。

### 7.2 需要/不需要提权的命令

| 需要提权 | 免提权 |
|----------|--------|
| `search`、`use`、`env`、`remove`、`download`、`data input`、`rollback <版本>` | `list`、`version`、`shell`、`help`、`data output`、`rollback list`（只读注册表 PATH；`list` / `data output` 还会在校验缓存时顺带重写 `.jmt_cache`） |

### 7.3 提权失败的降级

- `RelaunchElevated` 返回 false：命令返回 `3`，提示「请手动以管理员身份运行」
- `RegistryOperator` 在 `Auto` 模式下系统写失败会尝试用户分支；由于用户分支当前指向 `HKCU` 下不存在的键，实际不会成功（见 10.4），因此最终表现为写入失败并返回 `3`

---

## 8. 异常处理与退出码

### 8.1 退出码

| 退出码 | 含义 | 主要来源 |
|--------|------|----------|
| 0 | 成功 | 所有命令正常返回 |
| 1 | 参数错误 / 未知命令 | `main.cpp` 未知命令；各命令缺参、版本格式错误 |
| 2 | 未找到 JDK / 版本 / 条目 | `search`（无 JDK）、`list`、`use`、`remove`、`rollback`、`data` |
| 3 | 权限不足 | 提权失败、注册表写入失败、目录创建失败 |
| 4 | 网络或磁盘错误 | 下载/解压失败、回收站移动失败、写文件失败 |

### 8.2 异常处理

- `main.cpp` 与 `ReplEngine` 都用 `try-catch(const std::exception&)` 包住 `execute()`，用 `ToWideString(e.what())` + `PrintError` 输出；`main` 返回 `1`
- 命令内部对文件系统操作使用 `try-catch` 并转换为 `4` / `3`
- `fs::remove_all` 等清理操作失败降级为警告，不改变主流程退出码
- REPL 中命令的非 0 返回码只通过 `PrintDebug` 提示（Debug 构建可见），不会中断会话

---

## 9. 构建、测试与发布

### 9.1 CMake 结构

（以下摘自 `CMakeLists.txt`，略有精简）

```cmake
project(JMT LANGUAGES CXX RC)          # 需要 RC 编译 resources/app.rc
set(CMAKE_CXX_STANDARD 17)
option(JMT_BUILD_TESTS "构建 JMT 单元测试与集成测试" ON)

add_library(jmt_core STATIC ${JMT_CORE_SOURCES})   # main.cpp 之外的全部源文件
target_include_directories(jmt_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(jmt_core PUBLIC Shlwapi.lib Wininet.lib Advapi32.lib Winhttp.lib)
target_compile_options(jmt_core PRIVATE /utf-8)

add_executable(jmt src/main.cpp)
target_link_libraries(jmt PRIVATE jmt_core)
target_sources(jmt PRIVATE resources/app.rc)

# MSVC：静态链接 CRT
set_target_properties(jmt_core jmt PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
```

测试目标在 `tests/CMakeLists.txt` 存在时才会注册（`tests/` 被 `.gitignore` 排除，仓库里可能不存在）。

### 9.2 构建命令

```bash
cmake -S . -B build -G Ninja        # 或使用 CLion 默认的 cmake-build-debug
cmake --build build                 # Ninja 单配置
cmake --build build --config Release  # 多配置生成器（VS）
```

MSVC 环境需先执行 `vcvars64.bat`（或由 IDE 注入），否则会报找不到标准库头文件。

### 9.3 测试

```bash
ctest --test-dir build --output-on-failure
build/jmt_tests.exe --list          # 列出全部用例
build/jmt_tests.exe --suite path_utils
```

| 套件 | 内容 |
|------|------|
| `unit.string_helper` | 分隔符切分、忽略大小写比较 |
| `unit.path_utils` | 标准化、比较、拆分、去重（含 `/` 与 `\` 不统一的已知限制） |
| `unit.version_parse` | `jdk-17.0.2` / `jdk-21` / `jdk1.8.0_202` / `openjdk-11.0.2` / 无版本路径 |
| `integration.cli_smoke` | 启动真实 `jmt.exe` 断言 `version`=0、`help`=0、未知命令=1（超时 120 秒） |

框架为 `tests/test_framework.hpp`：`JMT_TEST(套件, 用例)` 自动注册、`JMT_CHECK` / `JMT_CHECK_EQ` 断言失败即抛异常并带文件行号，入口 `tests/test_main.cpp` 汇总通过/失败数。

编写约定：`tests/unit/` 不得触碰注册表、PATH、网络、缓存文件；`tests/integration/` 只允许调用只读命令，避免污染开发机环境。

### 9.4 发布约定

`.gitignore` 的规则体现了本仓库的发布口径：

- 发布：`src/`、`include/`、`resources/`、`CMakeLists.txt`、`README.md`、已编译的 `cmake-build-*/jmt.exe`
- 不发布：`.idea/`、构建中间产物、`CLAUDE.md`、`本次需求文档.txt`、`*.docx`、`tests/`，以及运行期产物 `.jmt_cache` / `.repo/` / `.temp/` / `.trash/` / `.data/`

---

## 10. 附录

### 10.1 编码规范

| 项目 | 规范 |
|------|------|
| 文件名 | 小写下划线（`java_env_service.cpp`） |
| 类名 | 大驼峰（`JavaEnvService`） |
| 函数 | 小驼峰（`setCurrentJdk`） |
| 变量 | 小驼峰；成员变量带尾随下划线（`zipMap_`） |
| 字符串 | 一律 `std::wstring`，字面量加 `L""` |
| 头文件 | `#pragma once`；只放声明，实现集中在 `src/` |
| 包含路径 | 模块前缀形式（`"jdk/..."`），`include/` 为搜索根 |
| Windows API | 显式使用 `W` 版本；句柄用 RAII 或显式 `CloseHandle` |
| 注释 | 中文注释，说明「为什么」而不是复述代码 |

### 10.2 文件格式约定

| 文件 | 位置 | 编码 | 格式 |
|------|------|------|------|
| `.jmt_cache` | exe 同级 | UTF-16 LE（无 BOM，`FileLock::writeAllText` 写入） | 每行 `版本号\|绝对路径` |
| `.repo/jdk_zip_repo.txt` | exe 同级 | UTF-8 带 BOM（`WriteFileText`） | 每行一个 URL，`#` 为注释；检测到 UTF-16 BOM 时删除重建 |
| `.repo/jdk_exe_repo.txt` | exe 同级 | 同上 | 同上 |
| `.data/ver_out.txt` | exe 同级 | UTF-8 带 BOM | 每行 `版本号\|安装路径` |
| `.trash/jdk-<版本>_<时间戳>/.original_path` | exe 同级 | UTF-8 带 BOM | 原始安装路径字符串 |

读取统一经 `ReadFileText`，它按 UTF-16 BOM → UTF-16 启发式（奇数位多为 `0x00`）→ UTF-8 → 当前代码页的顺序解码，因此历史遗留编码文件通常仍可读。

### 10.3 命令速查

| 场景 | 命令 |
|------|------|
| 首次部署 | `jmt env` |
| 扫描并自动配置 | `jmt search` / `jmt search --force` |
| 查看版本列表 | `jmt list` |
| 切换版本 | `jmt use 21` |
| 默认策略下载 | `jmt download 17` |
| 仅镜像下载 | `jmt download 17 --mirror` |
| 官方源下载 | `jmt download 17 java` |
| 导出 / 导入列表 | `jmt data output` / `jmt data input` |
| 删除版本 | `jmt remove 17` |
| 查看 / 恢复回收站 | `jmt rollback list` / `jmt rollback 17` |
| 清理下载缓存 | `jmt remove temp` |
| 清空回收站 | `jmt remove trash` |
| 注销 JMT 自身 PATH | `jmt remove env` |
| 完全清理 | `jmt remove all` |
| 新窗口交互 | `jmt shell` |

### 10.4 已知问题与实现差异

按影响面从高到低排列；修复时需要同步更新本文档与 `README.md`。

| # | 位置 | 现象 | 影响 |
|---|------|------|------|
| 1 | `src/command/search_command.cpp` 等 | `restoreOracleJavaPath` 依赖真实目录 `C:\Program Files\Common Files\Oracle\Java\javapath` 是否存在 | 单测无法覆盖「恢复 javapath」分支（该目录不存在时是空操作），目前只验证 JDK 条目语义，javapath 往返留在手测清单 |
| 2 | `src/jdk/jdk_download_service.cpp` | 官方源（Adoptium）只提供 ZIP | `jmt download 17 exe` 这类「只要安装包」的请求只能走镜像 EXE 源，官方无法提供；阶段 2 可考虑官方 MSI/EXE 变体 |
| 3 | `src/jdk/download_plan.cpp` | `isDemoUrl` 依赖 URL 中出现 `-demos` | 若某镜像用其它命名方式提供 demo 包，仍会被当成正式包下载 |
| 4 | `src/command/download_command.cpp` | `exe` 与 `java` / `--mirror` 同时给出时以 `exe` 优先 | 组合开关的语义是「显式覆盖」，未做冲突提示 |
| 5 | `src/command/help_command.cpp` | `help <未知命令>` 打印错误后仍 `return ExitCode::Ok` | 脚本无法通过退出码判断帮助参数是否有效 |
| 6 | `src/app/elevation_gate.cpp` | 提权提示语在骨架阶段统一为「需要管理员权限，正在请求提权...」 | 仅提示文案差异（原先 main 用的是「此操作需要管理员权限，正在请求...」），行为与退出码不变 |
| 7 | `src/platform/console_output.cpp` | `progress()` 以 `info.dwSize.X` 填充整行，未处理控制台换行/滚动边界 | 进度行在窗口边缘可能残留字符 |
| 8 | `src/network/multi_thread_downloader.cpp` | `DownloadProgress.speed` 恒为 0 | 界面无法显示速度；`calcPercent` 在拿不到总大小时返回 `-1` |
| 9 | `src/command/version_command.cpp` / `src/console/repl_engine.cpp` | 版本号与 banner 各自硬编码 | 升级版本需三处同步（含文档），无单一数据源 |
| 10 | `resources/app.rc` | 只有图标，没有 `VERSIONINFO` 资源 | 文件属性页看不到版本信息，只能靠 `jmt version` |
| 11 | `src/jdk/jdk_scan_service.cpp` | 同版本 JDK 只保留先扫描到的那一份（`seen` 去重）；扫描深度固定 3 层 | 多份同版本安装无法在 `list` 中共存；深层目录中的 JDK 不会被发现 |
| 12 | `src/command/download_command.cpp`（官方源） | 官方 Adoptium 端点只有 `latest/{feature}`，没有按补丁版本选择 | `download 17.0.9 java` 拿到的是 17 系列最新版；靠 `versionSatisfied` 校验后放弃该源，精确版本请走镜像源（阶段 3 支持 `/v3/binary/version/...` 后消除） |

版本模型阶段（11.2）已消除的老问题（保留记录）：

- 版本被截断成主版本（`17.0.9` → `17`、`1.8.0_202` → `8`），`use`/`remove` 无法指定补丁版本
- 用 `std::stoi` 比较版本导致 `17.0.2` 与 `17.0.9` 视为相等
- 缓存读取被文件锁拒绝，缓存从未生效、每次全盘扫描

### 10.5 本地资料（不随仓库发布）

- `jdk下载分析数据.txt`：镜像源可用性实测记录（南大 / 清华 TUNA / 华为云 / Adoptium 等），可作为扩充内置映射的参考
- `本次需求文档.txt`：需求草稿（当前为空）
- `CLAUDE.md`：面向 AI 协作工具的项目说明

---

## 11. 架构骨架（阶段 0）现状与后续阶段

阶段 0 的目标是「不改行为地把缝撬开」，已在 `codex/arch-skeleton` 分支按 9 个提交完成：

| 提交 | 内容 |
|------|------|
| skeleton/1 | `ExitCode` / `Error` / `Status` / `Result<T>`（`common/`） |
| skeleton/2 | 输出端口 `IOutput` + `ConsoleOutput` / `NullOutput`（`platform/output.hpp`） |
| skeleton/3 | `AppPaths` 收口运行期路径，`AppContext` 取代 `JmtContext` 的前身 |
| skeleton/4 | 环境变量端口 `IRegistry`（`EnvTarget` 迁入端口层） |
| skeleton/5 | 提权端口 `IElevator` + 唯一入口 `ElevationGate`，删除 7 处命令内提权代码 |
| skeleton/6 | `AppContext` + 命令元数据（`name()` / `requiresElevation()`）+ 命令返回 `ExitCode` |
| skeleton/7a | 扫描 / 环境 / JMT 自身 PATH 服务改为实例并注入端口，新增 `AppRuntime` 组合根 |
| skeleton/7b | 下载服务改为实例（映射表不再静态、约 90 处输出改走端口） |
| skeleton/8 | 删除输出过渡层与旧适配器：`color_print.*`、`console_progress.*`、`system/registry_operator.*`、`system/elevation_helper.*` |

**现在可以做到的事**（阶段 0 的收益）：

- 给命令注入 `NullOutput` / 自定义 `IOutput`、内存版 `IRegistry`、假的 `IElevator`，用 `AppContext` 直接构造被测命令，无需触碰真实注册表与控制台
- 服务层不再有静态状态：`JdkDownloadService` 的映射表是实例成员，重复创建互不影响
- 提权、输出、环境变量、路径四件事各只有一个入口，新增命令只需实现 `CommandBase` 的 4 个方法与注册

### 11.1 阶段 1（功能硬伤）已完成

| 提交 | 内容 |
|------|------|
| phase1/1 | `WinRegistry` 用户级改用 `HKCU\Environment`；`targetsFor()` 取代下标运算；写模式下键不存在则创建 |
| phase1/2 | `CommandBase::allowsUserScope()` + `ElevationGate` 的 `--user` 免提权判定 |
| phase1/3 | `app/env_scope.hpp` 统一解析 `--user/--sys`；五个命令透传 target；`getCurrentVersion` 改为用户优先；`remove env/all` 检查写入返回值 |
| phase1/4 | `ConsoleOutput` 非控制台回退 UTF-8 + CRLF；`progress/clearProgress` 在重定向下空操作 |
| phase1/5 | `jdk/download_plan`（源选择纯逻辑）+ `executePlan` 逐条轮询；demo 包跳过 |
| phase1/6 | `exe` 参数生效：`downloadInstallerOnly` 只下载安装包 |

测试套件从 4 组增加到 8 组：新增 `unit.java_env_service`（内存 IRegistry）、`unit.download_plan`（源选择）、`integration.cli_output`（管道捕获输出）、`integration.registry_user_scope`（HKCU 往返，只写自建一次性变量）。

> 注意：`integration.registry_user_scope` 会写注册表，沙箱环境下会被拒绝（`Requested registry access is not allowed`），需要在沙箱外运行 `ctest`（或用管理员/普通用户终端直接跑 `jmt_tests.exe --suite registry_user_scope`）。

### 11.2 阶段 2（版本模型与精确版本）已完成

| 提交 | 内容 |
|------|------|
| version/1 | `common/java_version.hpp`：`JavaVersion` 解析（`17.0.9` / `22` / `1.8.0_202` / `8u202` / `17.0.2+8`）、比较、别名匹配；`jdk/version_match.hpp`：`resolveVersion` / `sortByVersionDesc` / `maxVersion` |
| version/2 | `extractVersion` 返回完整版本；缓存写入 `#jmt-cache-v2` 头，旧格式判为无效并要求重扫；扫描去重从「按版本」改为「按路径」（同版本多份都保留） |
| version/3 | `getCurrentVersion` 支持注入 `VersionResolver`（默认读 `release`），用户 PATH 优先，解析失败回退路径文本 |
| version/4 | `use` 支持完整版本/主版本/前缀/别名并新增 `--exact`，命中多条取最高并打印候选；`remove` 放宽版本参数、按真实版本比较取最大、缓存按路径剔除；`search` 最大版本改用 `JavaVersion`；`list` 按版本倒序 |
| version/5 | `download` 保留完整版本（安装目录 `jdk-17.0.2`）、`filterUrlsForVersion` 按版本筛源、`versionSatisfied` 安装后校验（不一致则删除并放弃该源） |
| version/6 | `CommandBase::preflight` 钩子：提权前做只读校验，版本不存在时直接返回 2 而不弹 UAC |
| version/7 | 修复缓存读取被独占文件锁拒绝导致缓存永远失效、每次全盘扫描的问题 |

**范围说明**：本轮**未改造** `data output` / `data input`（按要求暂缓）。它们的文件格式仍是 `版本号|路径`，只是版本字段现在会写成完整版本；`data input` 调用 `downloadAndInstall` 时按 `use` 相同规则处理。

### 11.3 阶段 3（下载子系统）进行中

| 提交 | 内容 |
|------|------|
| download/1 | `network/host_throttle`：单主机并发上限（默认 2）、最小请求间隔、指数退避 + 抖动、单 URL/单主机/单命令三级预算、429/503/403 拉黑 10 分钟；注入下载链路（acquire/release/noteResult）。curl 改为静默 + 超时 + 统一 UA + 状态码解析；`.temp` 文件名按 URL 哈希唯一化；多线程连接数跟随策略（2）；`executePlan` 跳过被拉黑主机并在失败摘要里输出请求次数 |
| download/2 | `common/cancel_token`：全局取消开关；REPL 与单次模式的 Ctrl+C 都会中断下载，`TerminateProcess` 终止 curl 子进程并删除半成品；多线程引擎各循环响应取消 |
| download/4 | `network/curl_output`：解析 curl 的 `--progress-bar` 百分比与 `-w` 收尾统计（状态码/字节/用时/速度）+ 里程碑计算。`downloadFileWithCurl` 改为边等边读管道：百分比走 `IOutput::progress` 原地刷新，25/50/75/100% 额外打印普通行（重定向可见），无百分比时每 5 秒心跳；成功后打印「下载完成: X MB，平均 Y MB/s」 |

**尚未完成**（原阶段 3 计划的其余部分，留待下一批）：

1. `IDownloadEngine` 端口 + `CurlEngine` / `MultiThreadEngine` 统一抽象（当前仍按函数分支，但已全部经由节流器）
2. 统一安装管线 `installFromSource`：消除 `tryZipSource` / `tryExeSource` / `tryOfficialZip` 三份重复（下载→校验→解压→嵌套修复→版本校验）
3. 校验增强：魔数与最小尺寸校验统一到两个引擎、可选 SHA-256（官方源 `.sha256`）、解压前 zip-slip 防护
4. 失败源**持久化**拉黑（当前仅进程内 10 分钟）
5. 官方源精确补丁版本（`/v3/binary/version/{release_name}`）
6. `Result<InstallOutcome>` 取代 `L"EXE_DOWNLOADED"` 哨兵；`.part` 复用（同会话断点续传）
7. 进度显示速度/ETA（当前仍只有百分比）

### 11.4 后续阶段（尚未开始）

1. **阶段 3 · 下载子系统重做**：`DownloadSource` 策略化（优先级/测速/并发重试）+ 统一的「下载→校验→解压→安装」管线 + 取消与速度上报；顺带支持官方源的精确版本（`/v3/binary/version/...`）
2. **阶段 4 · 扫描与数据**：并行/可取消扫描、进度显示；`data output/input` 改造与版本字段迁移
3. **阶段 5 · 可测性与 CI**：注入式 fake（文件系统/HTTP）+ PATH/回收站往返集成测试 + GitHub Actions 跑 `ctest`（同时决定 `tests/` 是否改为发布）

---

**文档版本**：V1.7（对应 `hotfix` 分支 `include/` / `src/` 模块化重构后的代码）
**最后更新**：2026-09-16
**版本对应**：JMT v1.7 (build 2026.07.13)

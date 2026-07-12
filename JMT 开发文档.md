# JMT（Java Manager Tool）开发文档 V1.7

> Windows 平台 JDK 版本管理工具 | C++17 | 纯 Win32 API | 无外部依赖

---

## 目录

1. [项目概述](#1-项目概述)
2. [架构总览](#2-架构总览)
3. [命令参考](#3-命令参考)
4. [核心模块](#4-核心模块)
5. [服务层](#5-服务层)
6. [基础设施层](#6-基础设施层)
7. [数据流与典型场景](#7-数据流与典型场景)
8. [提权策略](#8-提权策略)
9. [异常处理与退出码](#9-异常处理与退出码)
10. [构建与部署](#10-构建与部署)
11. [附录](#11-附录)

---

## 1. 项目概述

### 1.1 简介

JMT 是一款 Windows 平台命令行 JDK 版本管理工具（类似 NVM for Node.js），全盘扫描合法 JDK、一键版本切换、在线下载安装、环境变量自动管理。

**核心特性**：
- 交互式 REPL（无参启动进入 `jmt>` 提示符）和单次命令模式
- SSD 全盘扫描，自动识别合法 JDK（校验 `java.exe` + `javac.exe` + `lib` 目录）
- 直接操作系统/用户 PATH 环境变量（注册表 `Environment` 键），不使用 `JAVA_HOME`
- 多源下载：内置华为云镜像 + 外部可配置源 + Adoptium API 回退
- 多线程 HTTP Range 下载（断点续传）
- 回收站机制：删除前移至 `.trash` 支持回退
- 彩色输出，当前版本高亮

### 1.2 系统要求

- Windows 10/11 x64
- Visual C++ Redistributable（或静态链接 CRT）

### 1.3 全部命令

| 命令 | 功能 | 提权 |
|------|------|------|
| `search [--force]` | 扫描 JDK，若 PATH 中无 JDK 则自动设置最大版本 | 是 |
| `list` | 列出所有已识别版本，标注当前生效版本 | 否 |
| `use <ver>` | 切换 PATH 中的 JDK bin 路径至指定版本 | 是 |
| `env` | 将 JMT 自身目录加入 PATH（去重） | 是 |
| `remove <子命令>` | 删除 JDK 或清理环境 | 是 |
| `download <ver>` | 下载并安装 JDK（支持镜像/官方/EXE） | 是 |
| `data <子命令>` | 导出/导入 JDK 列表 | input 需提权 |
| `rollback <ver>` | 从回收站恢复已删除的 JDK | 是 |
| `version` | 显示版本信息 | 否 |
| `shell` | 打开新 CMD 窗口进入交互模式 | 否 |
| `help [命令]` | 显示帮助 | 否 |

---

## 2. 架构总览

### 2.1 分层架构

```
┌──────────────────────────────────────────────────────────┐
│                   表示层 (main.cpp)                        │
│  初始化资源映射 → 构建上下文 → 提权决策 → REPL/单次执行    │
└──────────────────────┬───────────────────────────────────┘
                       │
┌──────────────────────▼───────────────────────────────────┐
│                  命令层 (command/)                         │
│  CommandBase 接口 → CommandRegistry 映射 → 12 个命令实现   │
│  每个命令：解析参数 → 调用服务层 → 输出结果              │
└──────────────────────┬───────────────────────────────────┘
                       │
┌──────────────────────▼───────────────────────────────────┐
│                   服务层 (service/)                        │
│  JdkScanService    全盘扫描 + 缓存自愈                    │
│  JavaEnvService    PATH 中 JDK 路径管理（增删改查）      │
│  JdkDownloadService 多源下载 + ZIP 解压 + 嵌套目录修复   │
│  JmtPathService    JMT 自身 PATH 注册/注销               │
└──────────────────────┬───────────────────────────────────┘
                       │
┌──────────────────────▼───────────────────────────────────┐
│                 基础设施层 (infrastructure/)               │
│  RegistryOperator   注册表读写（自动降级）               │
│  PathUtils         PATH 解析/标准化/去重                 │
│  ElevationHelper   提权检测 + runas 递归启动             │
│  FileLock          缓存文件跨进程锁                      │
└───────────────────────────────────────────────────────────┘

辅以：
  network/       多线程 HTTP Range 下载器
  print/         彩色控制台输出 + 进度条
  utils/         文件系统工具 + 字符串处理
  repl/          交互式命令行循环
```

### 2.2 核心数据结构

**JmtContext** (`src/core/jmt_context.hpp`)
```cpp
struct JmtContext {
    std::wstring exeDirectory;    // jmt.exe 所在目录（无尾随反斜杠）
    std::wstring cacheFilePath;   // exeDirectory + L"\\.jmt_cache"
    bool isInteractive;           // 是否交互模式
    bool isElevated;              // 是否已提权
};
```

**CommandBase** (`src/core/command_base.hpp`)
```cpp
class CommandBase {
public:
    virtual ~CommandBase() = default;
    virtual int execute(const std::vector<std::wstring>& args, JmtContext& ctx) = 0;
    virtual std::wstring getHelp() const = 0;
};
```

---

## 3. 命令参考

### 3.1 search — 扫描并配置 JDK

```
jmt search [--force]
```

- 扫描所有驱动器下合法 JDK（校验 `bin/java.exe` + `bin/javac.exe` + `lib` 目录）
- 写入缓存 `.jmt_cache`
- **若 PATH 中已有 JDK**：仅提示当前版本，不自动切换
- **若 PATH 中无 JDK**：自动切换到最大版本
- `--force`：强制全盘重新扫描，忽略缓存

### 3.2 list — 列出版本

```
jmt list
```

- 从缓存读取 JDK 列表（缓存失效则自动重扫）
- 当前生效版本绿色高亮
- 标注 PATH 中是否有 JMT 管理的 JDK

### 3.3 use — 切换版本

```
jmt use <version>
```

- 从缓存找到指定版本的 JDK 路径
- 自动检测并删除 PATH 中所有 `IsJdkBinPath()` 匹配的条目
- 添加新版本的 `bin` 路径
- 自动移除 Oracle javapath

### 3.4 env — 注册 PATH

```
jmt env
```

- 将 `jmt.exe` 所在目录添加到 PATH
- 去重，删除已有旧条目再添加

### 3.5 remove — 删除与清理

```
jmt remove <子命令> [--user|--sys]
```

子命令：
| 子命令 | 功能 |
|--------|------|
| `env` | 从 PATH 中移除 JMT 自身目录 |
| `all` | 完全清理：清除 JDK PATH + JMT PATH + 删除 `.trash` `.temp` 缓存 + 清理 `JAVA_HOME*` |
| `temp` | 删除 `.temp` 下载缓存目录 |
| `trash` | 永久清空回收站 `.trash` |
| `<版本号>` | 将指定版本移动到回收站，自动切换 PATH 到最大版本（若删除的是当前版本） |

`--user` / `--sys` 指定操作目标（用户 PATH / 系统 PATH），默认 Auto（自动降级）。

### 3.6 download — 下载安装

```
jmt download <version> [--mirror] [exe] [java]
```

下载策略（按优先级）：
1. **默认（无参数）**：尝试镜像 ZIP → 镜像 EXE → 官方 ZIP
2. **`--mirror`**：仅从镜像源下载，不回退官方
3. **`exe`**：强制下载 EXE 安装包到 `.temp`，不自动安装
4. **`java`**：强制从官方 Adoptium API 下载

**覆盖安装**：若目标版本已存在，提示用户确认 → 移动到回收站 → 自动切换 PATH → 下载新版本 → 自动设为当前版本。

### 3.7 data — 数据导入导出

```
jmt data output     # 导出当前 JDK 列表到 .data\ver_out.txt
jmt data input      # 从 .data\ver_out.txt 导入并安装 JDK
```

**output**：扫描当前所有合法 JDK，写入 `版本号|安装路径` 格式到 `.data\ver_out.txt`。

**input**：读取 `ver_out.txt`，逐条检查：
- 若路径下已有合法 JDK → 跳过
- 若路径下无 JDK → 交互询问 → 调用 `downloadAndInstall` 下载安装到指定路径

### 3.8 rollback — 回退

```
jmt rollback <version>      # 恢复指定版本到原始路径
jmt rollback list           # 列出回收站中所有可恢复版本
```

- 从 `.trash\jdk-<版本>_<时间戳>` 恢复
- 读取 `.original_path` 元数据文件确定原始路径
- 优先 `MoveFileW`（同卷），失败则 `xcopy` 提权复制
- 恢复后强制刷新缓存

### 3.9 shell / version / help

```
jmt shell            # 打开新 CMD 窗口并自动进入交互模式
jmt version           # 显示 JMT v1.7 (build YYYY.MM.DD)
jmt help [command]    # 显示帮助信息或命令详情
```

---

## 4. 核心模块

### 4.1 main.cpp — 入口与提权调度

**初始化顺序**：
1. `InitConsole()` — 设置 UTF-8 代码页，启用虚拟终端
2. `JdkDownloadService::reloadMappings()` — 加载镜像源映射
3. 构建 `JmtContext`（目录、缓存路径、交互/提权状态）
4. 注册 12 个命令到 `CommandRegistry`
5. 交互模式 → `ReplEngine::run()`
6. 单次命令模式 → 提权判断 → 执行

**提权判断逻辑**：
```cpp
bool needsAdmin = (cmd == L"use" || cmd == L"env" || cmd == L"remove"
                || cmd == L"search" || cmd == L"download");
```
需要提权的命令若未提权：`ShellExecuteW(runas)` 递归启动，父进程退出。

### 4.2 command/ — 命令实现

每个命令类继承 `CommandBase`，职责：
- 参数解析
- 提权检查（部分命令自身也做提权，因为 `data input` 等子命令需要）
- 调用服务层接口
- 异常捕获与用户输出

### 4.3 repl/ — 交互式循环

- `ReplEngine` 使用 `std::wcin` 读取行
- `ReplUtils::splitCommandLine` 解析参数
- 支持 `exit` / `quit` 退出
- `Ctrl+C` 捕获后重新显示提示符
- 支持所有命令；但不处理提权（因为已在 main 中处理或在命令自身中处理）

---

## 5. 服务层

### 5.1 JdkScanService — 扫描与缓存

**接口**：
```cpp
static std::vector<std::pair<std::wstring, std::wstring>> scanJdks(
    bool force, const std::wstring& cachePath, bool silent = false);
static bool isValidJdk(const std::wstring& path);
static std::wstring extractVersion(const std::wstring& path);
static void writeCache(const std::vector<...>& jdks, const std::wstring& cachePath);
```

**JDK 合法性校验** (`isValidJdk`)：
1. 排除 `jre` 目录
2. 必须存在 `bin\java.exe` + `bin\javac.exe` + `lib` 目录

**版本号提取** (`extractVersion`)：
1. 优先读取 `release` 文件中的 `JAVA_VERSION` 字段
2. 回退从路径正则匹配：`jdk1.8` → 8, `jdk-17` → 17, `jdk17` → 17

**扫描策略**：
1. 优先扫描常见路径（`C:\Program Files\Java`, `D:\Java` 等）
2. 遍历所有非系统驱动器
3. 递归深度 3 层
4. 排除系统目录（`C:\Windows`, `C:\ProgramData` 等）

**缓存机制**：
- 文件位置：`jmt.exe` 同级 `.jmt_cache`
- 格式：每行 `版本号|绝对路径`，UTF-16 LE
- 读取时逐条自愈（无效条目移除并重写）
- `FileLock` 跨进程并发保护

### 5.2 JavaEnvService — PATH 管理

**核心逻辑**：直接操作 PATH 字符串，不使用 `JAVA_HOME`。

**JDK bin 路径判定** (`IsJdkBinPath`)：
- 路径以 `bin` 结尾（忽略末尾斜杠）
- 路径包含 `jdk`（不区分大小写）

**接口**：
```cpp
static bool setCurrentJdk(const std::wstring& jdkPath, EnvTarget target);
static bool clearCurrentJdk(EnvTarget target);
static std::wstring getCurrentVersion();
```
- `setCurrentJdk`：删除所有 JDK bin 路径 → 移除 Oracle javapath → 添加新路径 → 广播环境变更
- `clearCurrentJdk`：删除所有 JDK bin 路径 → 恢复 Oracle javapath（若目录存在）
- `getCurrentVersion`：从 PATH 中遍历，提取首个 JDK bin 路径中的版本号

### 5.3 JdkDownloadService — 下载与安装

**映射管理**（优先级）：
1. **内置映射**：`initBuiltinMappings()` 静态初始化，包含 JDK 6~26 的华为云镜像 URL
2. **外部文件**：`.repo/jdk_zip_repo.txt` + `.repo/jdk_exe_repo.txt`，追加到映射列表末尾
3. **官方回退**：Adoptium API (`api.adoptium.net/v3/binary/latest/...`)

**文件自动修复**：
- 启动时检测 `.repo/*.txt` 文件编码（UTF-16 LE 旧版 → 删除重建）
- 不存在时从内置映射自动生成

**下载策略**：
- `downloadAndInstall`：ZIP（内置+外部）→ EXE → 官方 ZIP
- `downloadFromMirror`：仅镜像 ZIP → EXE
- `downloadFromOfficial`：仅官方 ZIP

**下载引擎**：
1. 优先 `curl.exe`（系统自带）
2. 回退 `MultiThreadDownloader`（WinHTTP Range 分片，4 线程）

**ZIP 解压**：通过 PowerShell `Expand-Archive` 命令

**嵌套目录修复** (`FixNestedJdkDirectory`)：
若解压后目标目录下只有一个子目录且为合法 JDK，则上移内容并删除空壳目录。

### 5.4 JmtPathService — 自身注册

```cpp
static bool registerJmtPath(const std::wstring& exeDir, EnvTarget target);
static bool unregisterJmtPath(const std::wstring& exeDir, EnvTarget target);
```

- 删除旧条目（标准化比较去重）
- 添加新条目
- 写入注册表并广播

---

## 6. 基础设施层

### 6.1 RegistryOperator — 注册表操作

**环境变量目标**：
```cpp
enum class EnvTarget { Auto, SystemOnly, UserOnly };
```
- `Auto`：先写系统（HKLM），失败自动降级用户（HKCU）

**核心操作**：
- 读写 PATH（`REG_EXPAND_SZ`）
- 读写普通环境变量
- 读写 `REG_MULTI_SZ`（`writeMultiString` / `readMultiString`）
- 枚举所有变量名（`enumerateEnvValueNames`）

**环境变更广播**（`WM_SETTINGCHANGE`）：
- `SendMessageTimeoutW` 超时 10 秒
- 首次失败 → 等待 500ms 重试（5 秒超时）
- 两次失败仅记录，不影响注册表写入

### 6.2 PathUtils — 路径处理

```cpp
static std::wstring normalize(const std::wstring& path);    // 小写+去尾斜杠
static bool arePathsEqual(const std::wstring& a, const std::wstring& b);
static std::vector<std::wstring> splitPath(const std::wstring& path);  // 按分号拆分
static std::vector<std::wstring> removeEntries(..., const std::wstring& toRemove);
static std::vector<std::wstring> addUniqueEntry(..., const std::wstring& newEntry);
```

- 保留 `%VAR%` 展开形式不变
- 标准化比较用于去重

### 6.3 ElevationHelper — 提权

```cpp
static bool IsElevated();
static bool RelaunchElevated(const std::wstring& commandLine);
static bool RelaunchElevatedAndWait(const std::wstring& commandLine, int& exitCode);
```

- `IsElevated`：`OpenProcessToken` + `TokenElevation`
- `RelaunchElevated`：`ShellExecuteW(runas)` 异步启动
- `RelaunchElevatedAndWait`：等待子进程结束返回退出码

### 6.4 FileLock — 文件锁

基于 `CreateFileW` 的互斥锁，用于缓存文件并发保护。

---

## 7. 数据流与典型场景

### 7.1 `jmt search`

```
main → SearchCommand::execute()
  → JdkScanService::scanJdks(force, cachePath)
    → [缓存命中] 读取 .jmt_cache，逐条校验，返回
    → [缓存未命中/force] 扫描全部驱动器
    → writeCache()
  → JavaEnvService::getCurrentVersion()
    → [PATH 已有 JDK] 输出当前版本，不切换
    → [PATH 无 JDK] setCurrentJdk(最大版本)
```

### 7.2 `jmt use 21`

```
main → UseCommand::execute("21")
  → 提权检查
  → JdkScanService::scanJdks() 获取列表
  → 查找版本 21
  → JavaEnvService::setCurrentJdk(path)
    → 删除所有 IsJdkBinPath 条目
    → 移除 Oracle javapath
    → 添加 <path>\bin
    → 广播 WM_SETTINGCHANGE
```

### 7.3 `jmt download 17`

```
main → DownloadCommand::execute()
  → 提权检查
  → 参数解析：[--mirror | exe | java | 默认]
  → 检查是否已存在 → 确认覆盖 → 移至回收站 → 切换 PATH
  → JdkDownloadService::downloadAndInstall("17")
    → findZipUrl("17") → 尝试镜像 ZIP
    → 失败 → findExeUrl("17") → 尝试镜像 EXE
    → 失败 → GetOfficialDownloadInfo("17") → 官方 ZIP
    → ExtractZip → FixNestedJdkDirectory → 返回安装路径
  → 扫描缓存 → 自动设置当前版本
```

### 7.4 `jmt remove 17`

```
main → RemoveCommand::execute(["remove", "17"])
  → 提权检查
  → 查找 JDK "17"
  → JavaEnvService::getCurrentVersion()
    → [是当前版本] 切换到最大版本 / 清除 PATH
    → [非当前版本] 从 PATH 中删除该版本 bin 路径
  → MoveFileW → .trash\jdk-17_<时间戳>
  → 写入 .original_path 元数据
  → 更新缓存
```

### 7.5 `jmt data input`

```
main → DataCommand::execute(["data", "input"])
  → 提权检查
  → 读取 .data\ver_out.txt
  → 逐行解析 version|path
  → 每项：isValidJdk → [已存在]跳过 | [不存在]询问 → downloadAndInstall
  → 强制刷新缓存
```

### 7.6 `jmt rollback 17`

```
main → RollbackCommand::execute(["rollback", "17"])
  → 提权检查
  → 在 .trash\ 下查找 jdk-17_* 目录
  → 读取 .original_path 获取原始路径
  → MoveFileW 恢复 / xcopy 提权复制
  → 强制刷新缓存
```

---

## 8. 提权策略

### 8.1 需要提权的命令

| 命令 | 原因 |
|------|------|
| `search` | 写注册表 PATH |
| `use` | 写注册表 PATH |
| `env` | 写注册表 PATH |
| `remove` | 写注册表 PATH + 删文件 |
| `download` | 写磁盘（安装 JDK） |
| `data input` | 写磁盘 |
| `rollback` | 写磁盘 |

### 8.2 免提权命令

`list`, `version`, `shell`, `help`, `data output`, `rollback list`

### 8.3 提权流程

1. 检查 `JmtContext::isElevated`
2. 未提权：`ElevationHelper::RelaunchElevated()` 通过 `runas` 递归启动
3. 父进程返回 0 退出
4. 子进程（已提权）继续执行

### 8.4 降级策略

`RegistryOperator` 在 `Auto` 模式下：
- 先尝试系统 PATH（HKLM）
- 若失败（无管理员权限）自动降级到用户 PATH（HKCU）
- `--user` 参数强制使用用户 PATH

---

## 9. 异常处理与退出码

### 9.1 退出码

| 退出码 | 含义 |
|--------|------|
| 0 | 成功 |
| 1 | 参数错误 / 未知命令 |
| 2 | 未找到 JDK / 版本 |
| 3 | 权限不足 |
| 4 | 网络或磁盘错误 |

### 9.2 异常处理

- 命令层 `try-catch` 捕获 `std::exception`，用 `PrintError` 输出
- 退出码由命令 `execute()` 返回值决定
- 未捕获异常由 `main` 兜底

---

## 10. 构建与部署

### 10.1 CMake 配置

```cmake
cmake_minimum_required(VERSION 3.15)
project(JMT LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 17)

# 静态链接 CRT
set_property(TARGET jmt PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

# 编译选项
target_compile_options(jmt PRIVATE /utf-8)

# 链接库
target_link_libraries(jmt
    Shlwapi.lib Wininet.lib Advapi32.lib Winhttp.lib
)
```

### 10.2 构建命令

```bash
cmake -S . -B build              # 配置
cmake --build build --config Release   # Release 构建
cmake --build build --config Debug     # Debug 构建
```

输出 `jmt.exe`，无外部 DLL 依赖。

### 10.3 源文件清单

```
src/
├── main.cpp
├── app.rc                          # 版本资源
├── core/
│   ├── command_base.hpp
│   ├── command_registry.cpp/hpp
│   └── jmt_context.hpp
├── command/                        # 12 个命令
│   ├── search_command.cpp/hpp
│   ├── list_command.cpp/hpp
│   ├── use_command.cpp/hpp
│   ├── env_command.cpp/hpp
│   ├── remove_command.cpp/hpp
│   ├── download_command.cpp/hpp
│   ├── version_command.cpp/hpp
│   ├── shell_command.cpp/hpp
│   ├── help_command.cpp/hpp
│   ├── rollback_command.cpp/hpp
│   └── data_command.cpp/hpp
├── service/
│   ├── jdk_scan_service.cpp/hpp
│   ├── java_env_service.cpp/hpp
│   ├── jdk_download_service.cpp/hpp
│   └── jmt_path_service.cpp/hpp
├── infrastructure/
│   ├── registry_operator.cpp/hpp
│   ├── path_utils.cpp/hpp
│   ├── file_lock.cpp/hpp
│   └── elevation_helper.cpp/hpp
├── network/
│   └── multi_thread_downloader.cpp/hpp
├── print/
│   ├── color_print.cpp/hpp
│   └── console_progress.cpp/hpp
├── repl/
│   ├── repl_engine.cpp/hpp
│   └── repl_utils.cpp/hpp
└── utils/
    ├── utils.cpp/hpp
    └── string_helper.cpp/hpp
```

### 10.4 运行时目录结构

```
jmt.exe 同级目录：
├── .jmt_cache              # JDK 扫描缓存（自动生成）
├── .trash/                  # 回收站（删除时移入）
├── .temp/                   # 下载临时文件
├── .repo/                   # 外部镜像源映射（自动生成）
│   ├── jdk_zip_repo.txt
│   └── jdk_exe_repo.txt
└── .data/                   # 导出数据
    └── ver_out.txt          # data output 命令生成
```

### 10.5 部署

复制 `jmt.exe` 到任意目录，运行 `jmt env` 注册 PATH。

---

## 11. 附录

### 11.1 编码规范

| 项目 | 规范 |
|------|------|
| 文件名 | 小写下划线（`java_env_service.cpp`） |
| 类名 | 大驼峰（`JavaEnvService`） |
| 函数 | 小驼峰（`setCurrentJdk`） |
| 变量 | 小驼峰 |
| 常量 | `k` 前缀 + 大驼峰 |
| 字符串 | 全部 `std::wstring`，字面量 `L""` |
| 头文件 | `#pragma once` |
| 内存 | 禁止裸 `new/delete`，使用智能指针 |
| HANDLE | RAII 包装 |
| Windows API | 显式使用 `W` 版本 |

### 11.2 资源映射文件格式

`.repo/jdk_zip_repo.txt` 和 `.repo/jdk_exe_repo.txt`：
- 每行一个 URL
- `#` 开头为注释
- 版本号从 URL 中自动提取（正则匹配主版本号）
- 程序启动时若文件不存在，自动从内置映射生成
- 外部 URL 追加到内置映射之后

### 11.3 缓存格式

`.jmt_cache`：UTF-16 LE 编码，每行 `版本号|绝对路径`

### 11.4 命令速查

| 命令 | 示例 | 说明 |
|------|------|------|
| search | `jmt search --force` | 强制重扫并配置 |
| list | `jmt list` | 列出版本 |
| use | `jmt use 21` | 切换至 JDK 21 |
| env | `jmt env` | 注册 PATH |
| remove env | `jmt remove env` | 移除自身 PATH |
| remove all | `jmt remove all` | 完全清理 |
| remove temp | `jmt remove temp` | 删除下载缓存 |
| remove trash | `jmt remove trash` | 清空回收站 |
| remove 17 | `jmt remove 17` | 删除版本 17 |
| download | `jmt download 21` | 下载并安装 |
| download exe | `jmt download 8 exe` | 仅下载 EXE |
| download --mirror | `jmt download 21 --mirror` | 镜像下载 |
| download java | `jmt download 21 java` | 官方下载 |
| data output | `jmt data output` | 导出列表 |
| data input | `jmt data input` | 导入安装 |
| rollback | `jmt rollback 17` | 恢复版本 |
| rollback list | `jmt rollback list` | 查看可恢复版本 |
| version | `jmt version` | 版本信息 |
| shell | `jmt shell` | 新窗口交互 |

---

**文档版本**：V1.7  
**最后更新**：2026-07-13  
**版本对应**：JMT v1.7 (build 2026.07.13)
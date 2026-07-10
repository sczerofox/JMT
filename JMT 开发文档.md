# JMT（Java Manager Tool）开发文档 V1.6

> 本文档是 JMT 项目的唯一技术依据，所有实现必须严格遵循。  
> V1.6 主要更新：下载策略优化、资源映射外置、环境变量管理方式简化、下载命令增强、新增清理子命令。

---

## 目录
1. [项目概述](#1-项目概述)
2. [总体架构设计](#2-总体架构设计)
3. [模块详细设计](#3-模块详细设计)
4. [命令规范与交互流程](#4-命令规范与交互流程)
5. [安全提权与权限策略](#5-安全提权与权限策略)
6. [数据流与典型场景](#6-数据流与典型场景)
7. [缓存机制与自愈](#7-缓存机制与自愈)
8. [异常处理与退出码](#8-异常处理与退出码)
9. [编码规范](#9-编码规范)
10. [测试策略](#10-测试策略)
11. [构建与部署](#11-构建与部署)
12. [附录](#12-附录)

---

## 1. 项目概述

### 1.1 项目简介
JMT（Java Manager Tool）是一款基于 C++17 开发的 Windows 平台纯命令行 JDK 版本管理工具。支持全盘扫描合法 JDK、自动校验完整性、一键版本切换、自动修复系统环境变量、JDK 在线下载、程序自注册 PATH 等功能。

**核心特性**：
- 交互模式（无参数启动进入 `jmt>` 提示符）
- 单次命令模式（带参数执行后自动退出）
- 智能动态提权（仅写操作时弹 UAC）
- 缓存便携（`jmt.exe` 同级目录存放 `.jmt_cache`）
- 彩色输出，当前版本高亮

### 1.2 支持的操作系统
- Windows 10/11（x64），要求安装 Visual C++ Redistributable（或静态链接 CRT）。

### 1.3 全部功能清单
| 命令 | 功能 | 参数 | 提权需求 |
|------|------|------|----------|
| `jmt search` | 全盘扫描合法 JDK，生成/刷新缓存，**将最大版本 JDK 的 bin 路径加入 PATH** | `--force` 强制重扫 | 需要（写 PATH） |
| `jmt list` | 列出所有已识别版本，标注当前生效版本（绿色） | 无 | 不需要 |
| `jmt use <ver>` | 切换 PATH 中的 JDK bin 路径至指定版本 | 版本号（如 21） | 需要（写 PATH） |
| `jmt env` | 将 JMT 自身目录加入 PATH（去重更新） | 无 | 需要（写系统/用户 PATH） |
| `jmt remove` | 清除所有 JMT 创建的环境变量和 PATH 条目 | `env`, `all`, `temp`, `<版本号>` | 需要（通常） |
| `jmt download <ver>` | 下载并安装指定版本的 JDK（默认仅 ZIP） | 版本号，可选 `exe` 强制下载 EXE | 需要（写磁盘） |
| `jmt version` | 显示 JMT 自身版本信息 | 无 | 不需要 |
| `jmt shell` | 打开一个新的 CMD 窗口并自动进入交互模式 | 无 | 不需要 |
| `jmt rollback <ver>` | 从回收站恢复已删除的 JDK | 版本号 | 需要 |
| `jmt clean-trash` | 永久清空回收站 | `--force` | 需要 |
| `exit` / `quit` | 仅在交互模式下退出 | 无 | - |

---

## 2. 总体架构设计

### 2.1 四层架构
```
┌─────────────────────────────────────────────────┐
│              表示层 (main.cpp)                   │
│  - 初始化 JmtContext                           │
│  - 检测提权状态，必要时 ShellExecute(runas)    │
│  - 选择 REPL 或单次执行模式                    │
└──────────────────┬──────────────────────────────┘
│
┌──────────────────▼──────────────────────────────┐
│              命令层 (command/)                   │
│  - CommandBase 接口                            │
│  - CommandRegistry 映射表                      │
│  - 每个子命令类（Search/List/Use/...）        │
└──────────────────┬──────────────────────────────┘
│
┌──────────────────▼──────────────────────────────┐
│              服务层 (service/)                   │
│  - JavaEnvService：管理 PATH 中 JDK 路径       │
│  - JmtPathService：管理 JMT 自身 PATH          │
│  - JdkScanService：扫描、缓存、校验            │
│  - JdkDownloadService：下载、解压、源管理      │
└──────────────────┬──────────────────────────────┘
│
┌──────────────────▼──────────────────────────────┐
│           基础设施层 (infrastructure/)            │
│  - RegistryOperator：注册表读写（降级）        │
│  - PathUtils：PATH 字符串解析/去重/标准化     │
│  - FileLock：缓存文件跨进程锁                  │
│  - ElevationHelper：提权检测与递归启动         │
└─────────────────────────────────────────────────┘
```

### 2.2 核心数据结构
#### `JmtContext`
```cpp
struct JmtContext {
    std::wstring exeDirectory;        // jmt.exe 所在目录（无尾随反斜杠）
    std::wstring cacheFilePath;       // 默认为 exeDirectory + L"\\.jmt_cache"
    bool isInteractive;               // 是否交互模式
    bool isElevated;                  // 是否已提权（管理员）
};
```

#### `CommandBase`
```cpp
class CommandBase {
public:
    virtual ~CommandBase() = default;
    virtual int execute(const std::vector<std::wstring>& args, JmtContext& ctx) = 0;
    virtual std::wstring getHelp() const = 0;
};
```

#### `CommandRegistry`
```cpp
class CommandRegistry {
public:
    void registerCommand(const std::wstring& name, std::unique_ptr<CommandBase> cmd);
    CommandBase* findCommand(const std::wstring& name) const;
private:
    std::map<std::wstring, std::unique_ptr<CommandBase>> commands_;
};
```

---

## 3. 模块详细设计

### 3.1 命令层（command/）
每个命令类实现 `execute`，调用服务层接口，**仅命令层负责解析参数、捕获异常、输出用户信息**。

#### 🔴 `DownloadCommand`（V1.6 增强）
- **参数解析**：
  - `jmt download <version>`：默认尝试 ZIP 源。
  - `jmt download <version> exe`：强制下载 EXE 安装包（仅下载到 `.temp`，提示手动安装）。
- **执行逻辑**：
  1. 调用 `JdkDownloadService::getSourceInfo(version)` 获取源信息（是否有 ZIP/EXE，是否 Demo）。
  2. 若为 Demo，打印醒目警告。
  3. 若为 ZIP 且未指定 `exe`，执行下载并解压安装。
  4. 若为 ZIP 但用户指定 `exe`，则忽略 ZIP，下载 EXE。
  5. 若无 ZIP 且未指定 `exe`，输出提示：`未找到 ZIP 源，请使用 'jmt download <ver> exe' 下载 EXE 手动安装`，返回 0（非错误）。
  6. 若无 EXE 且指定 `exe`，提示无可用源。
  7. 若 ZIP 下载失败，尝试回退官网（仅当未指定 `exe` 且无用户自定义源时）。
- **下载目录**：所有下载文件存放于 `.temp` 目录，ZIP 解压后删除临时文件，EXE 保留并提示用户手动运行。

#### 🔴 `RemoveCommand`（新增子命令）
- 新增子命令 `temp`：
  - 判断 `.temp` 目录是否存在，若存在则递归删除（`fs::remove_all`）。
  - 若不存在，输出提示信息。

#### 其他命令（基本不变，但内部调用服务层新接口）
- `SearchCommand`：扫描后调用 `JavaEnvService::switchToVersion` 将最大版本加入 PATH。
- `UseCommand`：调用 `JavaEnvService::switchToVersion` 切换。

### 3.2 服务层（service/）

#### 🔴 `JdkDownloadService`（V1.6 重构）
- **新增结构体**：
  ```cpp
  struct SourceInfo {
      bool hasZip;
      bool hasExe;
      bool isDemo;
      std::wstring zipUrl;
      std::wstring exeUrl;
  };
  ```
- **新增方法**：
  - `static SourceInfo getSourceInfo(const std::wstring& version);`
  - `static void ensureExternalMappingFiles();`：启动时检查 `.repo` 目录及 `jdk_zip_repo.txt`、`jdk_exe_repo.txt`，若不存在则从内置映射生成。
- **映射加载顺序**（优先级从高到低）：
  1. **内置映射**（`initBuiltinMappings()` 填充的静态 map）
  2. **外部文件**（`.repo/*.txt` 追加到 map，若 key 相同，外部 URL 追加到列表末尾，但查找时返回第一个，即内置优先）
  3. **官网回退**（仅当无任何 ZIP 源时使用 Adoptium API）

#### 🔴 `JavaEnvService`（V1.6 彻底重构）
**旧方案**（已废弃）：
- 创建 `JAVA_HOME<ver>` 变量，`JAVA_HOME=%JAVA_HOME<ver>%`，PATH 中添加 `%JAVA_HOME%\bin`。

**新方案**：
- 不再读写 `JAVA_HOME*` 变量。
- 新增静态方法：
  - `static void addJdkToPath(const std::wstring& jdkPath, EnvTarget target = EnvTarget::Auto);`  
    将 `<jdkPath>\bin` 添加到 PATH（去重）。
  - `static void removeJdkFromPath(const std::wstring& jdkPath, EnvTarget target = EnvTarget::Auto);`  
    从 PATH 中删除所有与 `<jdkPath>\bin` 标准化相等的条目。
  - `static void switchToVersion(const std::wstring& version, const std::vector<std::pair<std::wstring, std::wstring>>& jdks);`  
    遍历 `jdks`，找到匹配版本，根据 `JMT_Managed_Path` 列表删除所有旧管理的 JDK bin 路径，然后添加新版本路径，并更新管理列表。
- **管理列表机制**：使用注册表 `HKEY_CURRENT_USER\Software\JMT` 下的多字符串值 `ManagedPaths` 存储当前由 JMT 添加的所有 JDK bin 绝对路径。切换或搜索时，先删除这些路径，再添加新路径，并更新列表。

### 3.3 基础设施层（infrastructure/）
- `RegistryOperator` 新增：
  - `static bool writeMultiString(const std::wstring& key, const std::vector<std::wstring>& values);`
  - `static std::vector<std::wstring> readMultiString(const std::wstring& key);`
- 其余模块（`PathUtils`、`FileLock`、`ElevationHelper`）保持不变。

---

## 4. 命令规范与交互流程

### 4.1 命令解析
- 命令行模式：`wmain` 接收 `argv`，转换为 `std::vector<std::wstring>`。
- 交互模式：`ReplEngine` 使用 `ReadConsoleW` 读取一行，调用 `ReplUtils::SplitCommandLine` 解析。

### 4.2 交互模式（REPL）
- 无参启动时进入，打印横幅和帮助信息。
- 提示符：`jmt> `。
- 支持 `exit` / `quit` 退出，`Ctrl+C` 捕获后重新显示提示符。

### 4.3 帮助信息
- `help` 或 `?` 显示所有命令简要说明。
- `help <command>` 显示详细用法。

### 4.4 `jmt version`
输出格式：`JMT v1.6 (build YYYY.MM.DD)`

### 4.5 `jmt shell`
启动新的 CMD 窗口，自动进入交互模式。

---

## 5. 安全提权与权限策略

### 5.1 提权判断
`ElevationHelper::IsElevated()` 结果存入 `JmtContext::isElevated`。

### 5.2 命令分类与提权规则
| 命令 | 是否需要提权 | 处理方式 |
|------|-------------|----------|
| `list`, `version`, `shell`, `help` | 否 | 直接执行 |
| `search`, `use`, `env`, `remove`, `download` | 是（写 PATH 或磁盘） | 若未提权，自动弹 UAC 提权重启 |

### 5.3 自动提权流程
```cpp
if (needsAdmin && !ctx.isElevated) {
    ElevationHelper::RelaunchElevated(commandLine);
    return 0;   // 父进程退出
}
// 子进程继续执行
```

### 5.4 降级策略
`RegistryOperator` 在 `Auto` 模式下，若系统写入失败，自动降级到用户变量（HKCU）。

---

## 6. 数据流与典型场景

### 6.1 `jmt search`（V1.6 新行为）
1. 扫描所有合法 JDK。
2. 写入缓存。
3. 获取最大版本（或指定版本）。
4. 调用 `JavaEnvService::switchToVersion(maxVer, jdks)`：
  - 读取注册表 `ManagedPaths` 列表。
  - 从 PATH 中删除这些路径。
  - 将 `<maxVerPath>\bin` 添加到 PATH。
  - 更新 `ManagedPaths` 为新路径。
5. 输出成功信息。

### 6.2 `jmt use 21`
1. 获取 JDK 列表。
2. 找到版本 21 的路径。
3. 调用 `switchToVersion("21", jdks)`（同上）。
4. 提示重启终端。

### 6.3 `jmt download 17`（V1.6 新行为）
1. 调用 `JdkDownloadService::getSourceInfo("17")`。
2. 若 `hasZip=true` 且非 Demo，下载 ZIP 并解压安装。
3. 若 `hasZip=true` 且 Demo，打印警告后仍允许下载。
4. 若 `hasZip=false`，提示用户使用 `jmt download 17 exe`，返回。
5. 若 ZIP 下载失败，尝试官网回退（仅当未指定 `exe`）。

### 6.4 `jmt remove temp`
- 删除 `.temp` 目录，若不存在则提示。

### 6.5 `jmt env`
- 将 `jmt.exe` 所在目录加入 PATH（去重）。

### 6.6 `jmt remove`（清理）
- 根据 `ManagedPaths` 删除所有 JMT 管理的 JDK bin 路径。
- 删除 JMT 自身目录（如果 `all` 或 `env` 子命令）。
- 删除 `JAVA_HOME*`（如存在，向后兼容）。

---

## 7. 缓存机制与自愈

### 7.1 缓存文件位置
- 优先 `jmt.exe` 同级目录下的 `.jmt_cache`。

### 7.2 缓存格式
每行：`版本号|绝对路径`，UTF-16 LE 编码。

### 7.3 自愈逻辑
- 读取缓存后，对每个条目调用 `isValidJdk`，失效则移除并重写缓存。

### 7.4 并发保护
`FileLock` 在读写缓存文件时加锁。

---

## 8. 异常处理与退出码

### 8.1 异常类层次
```cpp
class JmtException : public std::runtime_error { /* ... */ };
class InvalidArgumentException : public JmtException { int exitCode() override { return 1; } };
class JdkNotFoundException : public JmtException { int exitCode() override { return 2; } };
class PermissionDeniedException : public JmtException { int exitCode() override { return 3; } };
class NetworkException : public JmtException { int exitCode() override { return 4; } };
```

### 8.2 退出码映射
| 退出码 | 含义 |
|--------|------|
| 0 | 成功 |
| 1 | 参数错误 |
| 2 | 未找到 JDK/版本 |
| 3 | 权限不足 |
| 4 | 网络或磁盘错误 |

### 8.3 全局异常捕获
`main` 最外层捕获，输出错误信息并返回对应码。

---

## 9. 编码规范

### 9.1 命名约定
- 文件名：小写下划线（`java_env_service.cpp`）。
- 类名：大驼峰（`JavaEnvService`）。
- 函数：小驼峰（`switchGlobalVersion`）。
- 变量：小驼峰，成员变量可加尾下划线。
- 常量：`k` 前缀 + 大驼峰。

### 9.2 头文件
- 使用 `#pragma once`。
- 包含顺序：自身头文件 → 标准库 → 项目头文件。

### 9.3 内存管理
- 禁止裸 `new`/`delete`，使用智能指针。
- Windows HANDLE 使用 RAII 包装。

### 9.4 字符串
- 全部使用 `std::wstring`，字面量加 `L`。
- Windows API 显式使用 `W` 版本。
- 输出通过 `PrintSuccess`/`PrintError` 等全局函数。

### 9.5 调试日志
- Debug 构建使用 `PrintDebug`，Release 禁用。

---

## 10. 测试策略

### 10.1 单元测试（Google Test）
- `PathUtilsTest`、`RegistryOperatorMock`、`JdkScanServiceTest`。

### 10.2 集成测试（手动或脚本）
- 干净虚拟机中验证 `search`、`use`、`download`、`remove temp` 等命令。

### 10.3 提权测试
- 以非管理员运行需要提权的命令，观察 UAC 弹窗和子进程执行。

---

## 11. 构建与部署

### 11.1 CMake 配置
- 标准：C++17。
- 静态链接 CRT（`/MT`）。
- 编译选项 `/utf-8`。
- 链接库：`Shlwapi.lib`、`Wininet.lib`、`Advapi32.lib`。

### 11.2 输出
- Release 构建产生 `jmt.exe`，无外部 DLL 依赖。

### 11.3 部署方式
- 手动复制 `jmt.exe` 到任意目录，运行 `jmt env` 注册 PATH。

---

## 12. 附录

### 12.1 命令速查表
| 命令 | 示例 | 说明 |
|------|------|------|
| `search` | `jmt search --force` | 扫描所有 JDK，自动配置 PATH |
| `list` | `jmt list` | 列出版本 |
| `use` | `jmt use 21` | 切换 PATH 中的 JDK 路径 |
| `env` | `jmt env` | 注册自身 PATH |
| `remove` | `jmt remove temp` | 删除下载缓存 |
| `download` | `jmt download 17` | 下载并安装 ZIP 版本 |
| `download exe` | `jmt download 8 exe` | 下载 EXE 到 .temp（不安装） |
| `version` | `jmt version` | 版本信息 |
| `shell` | `jmt shell` | 打开新窗口进入交互 |
| `rollback` | `jmt rollback 17` | 恢复回收站中的 JDK |
| `clean-trash` | `jmt clean-trash --force` | 清空回收站 |

### 12.2 目录结构（最终）
```
JMT/
├── .jmt_cache               # 运行时生成
├── .jmt_trash/              # 回收站
├── .temp/                   # 下载临时目录
├── .repo/                   # 外部资源映射
│   ├── jdk_zip_repo.txt
│   └── jdk_exe_repo.txt
├── CMakeLists.txt
└── src/
    ├── main.cpp
    ├── core/ ...
    ├── command/ ...
    ├── repl/ ...
    ├── service/ ...
    ├── infrastructure/ ...
    ├── print/ ...
    └── utils/ ...
```

### 12.3 资源映射文件格式
- 每行一个 URL，以 `#` 开头为注释。
- 程序启动时若文件不存在，自动生成一份包含内置 URL 的示例文件。

---

**文档版本**：V1.6  
**最后更新**：2026-07-06  
**维护者**：JMT 开发团队

本文档是开发的唯一技术依据，所有实现必须严格遵循。如有疑问，请提交 Issue 讨论后再修改。
```

---

以上即为整合后的完整开发文档。您可以将其保存为 `JMT 开发文档.md` 作为后续开发的标准参考。如果需要我针对其中某一项提供具体代码实现，请随时提出。
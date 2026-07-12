# JMT – Java Manager Tool V1.7

**JMT** 是一款 Windows 平台纯命令行 JDK 版本管理工具（类似 NVM for Node.js），支持全盘扫描、一键版本切换、在线下载安装、环境变量自动管理。

- C++17 开发，仅依赖 Win32 API + C++ 标准库
- 直接操作系统 PATH，无需 `JAVA_HOME`
- 多源下载（镜像 + 官方回退），多线程 Range 下载加速
- 交互式 REPL + 单次命令双模式

---

## 功能一览

| 命令 | 功能 |
|------|------|
| `jmt search` | 全盘扫描 JDK，自动配置最大版本到 PATH |
| `jmt list` | 列出已安装版本，标注当前生效版本 |
| `jmt use <ver>` | 切换 PATH 至指定版本 |
| `jmt download <ver>` | 从镜像/官方下载并自动安装 JDK |
| `jmt env` | 注册 JMT 自身到系统 PATH |
| `jmt remove` | 删除 JDK / 清理 PATH / 清空回收站 |
| `jmt data` | 导出/导入 JDK 列表 |
| `jmt rollback <ver>` | 从回收站恢复已删除的 JDK |
| `jmt shell` | 打开新终端进入交互模式 |
| `jmt version` | 显示版本信息 |
| `jmt help` | 显示帮助 |

---

## 快速开始

### 1. 部署

将 `jmt.exe` 放到任意目录，然后以管理员身份运行：

```cmd
jmt env
```

该命令将 JMT 所在目录添加到系统 PATH，之后可在任意终端使用 `jmt`。若移动了 `jmt.exe`，再次执行 `jmt env` 会自动清理旧路径。

### 2. 扫描现有 JDK

```cmd
jmt search
```

全盘扫描所有合法 JDK，自动将最大版本设为当前版本。后续使用 `--force` 参数强制重新扫描。

### 3. 切换版本

```cmd
jmt use 21
```

切换 PATH 中的 JDK bin 路径至版本 21。如果该版本不存在，会提示未找到。切换后需要重启终端使环境变量生效。

### 4. 下载安装新版本

```cmd
jmt download 17
```

从镜像源下载 JDK 17 的 ZIP 包，自动解压安装到 `D:\Program Files\Java\jdk-17` 并设为当前版本。

---

## 命令详解

### `jmt search [--force]`

全盘扫描 SSD 上所有合法 JDK（校验 `bin\java.exe` + `bin\javac.exe` + `lib` 目录），结果缓存到 `.jmt_cache`。

- **若 PATH 中已有 JDK**：显示扫描结果和当前版本，不自动切换
- **若 PATH 中无 JDK**：自动将最大版本加入 PATH
- `--force`：忽略缓存，强制全盘重新扫描

扫描范围：常见 Java 安装目录（`C:\Program Files\Java` 等）+ 所有非系统驱动器，递归深度 3 层。

### `jmt list`

从缓存读取并显示所有已识别 JDK。当前生效版本绿色高亮。缓存失效时自动触发重新扫描。

### `jmt use <version>`

切换 PATH 至指定版本。执行流程：
1. 从缓存查找指定版本路径
2. 删除 PATH 中所有 JDK bin 路径（以 `bin` 结尾且包含 `jdk` 的条目）
3. 移除 Oracle javapath（如存在）
4. 添加目标版本的 `\bin` 路径
5. 广播 `WM_SETTINGCHANGE` 通知系统

### `jmt env`

将 `jmt.exe` 所在目录注册到 PATH（先删旧条目再去重追加）。自动尝试系统 PATH（HKLM），权限不足时降级到用户 PATH（HKCU）。

### `jmt remove <subcommand>`

| 子命令 | 功能 |
|--------|------|
| `env` | 从 PATH 中移除 JMT 自身目录 |
| `all` | 完全清理：清除 JDK PATH + 删除回收站 + 删除下载缓存 + 清理 JAVA_HOME |
| `temp` | 删除 `.temp` 下载缓存目录 |
| `trash` | 永久清空 `.trash` 回收站 |
| `<版本号>` | 删除指定版本 JDK（移至回收站），自动处理 PATH 切换 |

`remove <版本>` 行为：
- 若删除的是当前版本：自动切换到剩余的最大版本（若无其他版本则清除 JDK PATH）
- 若非当前版本：清理 PATH 中残留的该版本 bin 路径
- JDK 目录移至 `.trash\jdk-<版本>_<时间戳>`
- 支持 `--user` / `--sys` 参数指定 PATH 操作目标

### `jmt download <version> [flags]`

| 参数 | 行为 |
|------|------|
| 默认（无参数） | 镜像 ZIP → 镜像 EXE → 官方 ZIP，三级回退 |
| `--mirror` | 仅从镜像源下载，不回退官方 |
| `exe` | 强制下载 EXE 安装包到 `.temp`，不自动安装 |
| `java` | 强制从官方 Adoptium API 下载 |

下载策略：
1. **镜像 ZIP**：优先尝试华为云等国内镜像的 ZIP 包，自动解压安装
2. **镜像 EXE**：无可用 ZIP 时尝试 EXE 下载（保存到 `.temp`，提示手动安装）
3. **官方源**：通过 Adoptium API 获取下载链接并下载 ZIP
4. 所有 ZIP 解压后自动修复嵌套目录结构

若目标版本已存在，提示是否覆盖：确认后将旧版本移至回收站，自动切换 PATH，再下载新版本。

### `jmt data`

```
jmt data output    # 导出当前 JDK 列表到 .data\ver_out.txt
jmt data input     # 从 .data\ver_out.txt 导入并安装 JDK
```

`data output`：扫描所有 JDK，以 `版本号|安装路径` 格式写入 `.data\ver_out.txt`。

`data input`：逐条读取 `ver_out.txt`，自动跳过已存在的 JDK，对不存在的路径询问用户是否下载安装。批量安装后自动刷新缓存。

### `jmt rollback <version | list>`

```
jmt rollback 21       # 恢复版本 21 到原始路径
jmt rollback list     # 查看回收站中所有可恢复版本
```

- 从 `.trash` 目录下查找匹配版本的备份（按创建时间取最新）
- 读取 `.original_path` 元数据确定原始安装路径
- 优先 `MoveFileW` 快速恢复，跨卷时自动使用 `xcopy` 提权复制

### `jmt shell`

打开一个新的 CMD 窗口，自动进入 JMT 交互模式（相当于在新窗口中执行 `jmt`）。

### `jmt version`

显示版本号：`JMT v1.7 (build 2026.07.13)`

---

## 交互模式

直接运行 `jmt.exe`（不带参数）进入交互式 REPL：

```
jmt> search
jmt> list
jmt> use 21
jmt> exit
```

支持 `exit` / `quit` 退出，`Ctrl+C` 捕获后重新显示提示符。

---

## 下载源管理

JMT 内置了华为云镜像源（JDK 6~26），支持通过外部文件扩展。

启动时自动在 `jmt.exe` 同级目录生成 `.repo/` 目录：

```
.repo/
├── jdk_zip_repo.txt    # ZIP 下载源列表
└── jdk_exe_repo.txt    # EXE 下载源列表
```

**自定义源**：编辑上述文件，每行一个 URL，`#` 开头为注释。版本号从 URL 中自动提取。外部 URL 优先级低于内置源。

---

## 运行时目录

```
jmt.exe 同级目录：
├── .jmt_cache       # JDK 扫描缓存（自动生成 + 自愈）
├── .trash/           # 回收站（删除时移入，支持回退）
├── .temp/            # 下载临时文件（ZIP 解压后删除，EXE 保留）
├── .repo/            # 外部镜像源映射
│   ├── jdk_zip_repo.txt
│   └── jdk_exe_repo.txt
└── .data/            # 导出数据
    └── ver_out.txt   # data output 生成
```

---

## 退出码

| 退出码 | 含义 |
|--------|------|
| 0 | 成功 |
| 1 | 参数错误 / 未知命令 |
| 2 | 未找到 JDK / 版本 |
| 3 | 权限不足 |
| 4 | 网络或磁盘错误 |

---

## 注意事项

**管理员权限**：修改系统 PATH 需要管理员权限。JMT 在检测到需要提权时会自动通过 UAC 弹窗递归重启。若提权失败，自动降级到用户 PATH（仅影响当前用户）。

**终端重启**：环境变量修改后需重启终端使新 PATH 生效。

**缓存维护**：手动移动/删除 JDK 后执行 `jmt search --force` 强制刷新缓存。

**PATH 管理策略**：JMT 通过启发式检测（路径以 `bin` 结尾且包含 `jdk`）识别和管理 JDK 路径，切换时清除所有匹配条目再添加新条目。不再使用 `JAVA_HOME`。

---

## 构建

### 依赖

- Visual Studio Build Tools 2022（或其他支持 C++17 的 MSVC）
- CMake 3.15+

### 构建步骤

```bash
# Release 构建
cmake -S . -B build
cmake --build build --config Release

# Debug 构建
cmake --build build --config Debug
```

输出 `jmt.exe`，无外部 DLL 依赖（静态链接 CRT）。

### 链接库

- `Shlwapi.lib` - Shell 路径 API
- `Wininet.lib` / `Winhttp.lib` - HTTP 下载
- `Advapi32.lib` - 注册表操作 + 提权检测

---

## 项目结构

```
src/
├── main.cpp                      # 入口：初始化、提权调度、命令分发
├── app.rc                        # 版本资源
├── core/                         # 核心抽象
│   ├── command_base.hpp
│   └── command_registry.cpp/hpp
├── command/                      # 12 个命令实现
│   ├── search/list/use/env/remove/download
│   ├── version/shell/help/rollback/data
├── service/                      # 业务逻辑
│   ├── jdk_scan_service          # 扫描 + 缓存
│   ├── java_env_service          # PATH 管理
│   ├── jdk_download_service      # 下载 + 源管理
│   └── jmt_path_service          # 自注册 PATH
├── infrastructure/               # Win32 封装
│   ├── registry_operator         # 注册表读写 + 自动降级
│   ├── path_utils                # PATH 解析标准化
│   ├── elevation_helper          # 提权检测 + runas 启动
│   └── file_lock                 # 文件锁
├── network/
│   └── multi_thread_downloader   # HTTP Range 分片下载
├── print/                        # 彩色输出 + 进度条
└── utils/                        # 文件系统 + 字符串工具
```

---

## 许可证

MIT License
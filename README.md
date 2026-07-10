# JMT – Java Manager Tool (V1.6)

**JMT** 是一款专为 Windows 平台打造的纯命令行 JDK 版本管理工具，对标 NVM 的设计理念，帮助您轻松管理多个 Java 版本，实现一键切换、自动环境配置。

**V1.6 重磅更新**：
- ✅ 下载更稳定：优先使用系统 `curl`，国内用户友好
- ✅ 环境变量更直接：不再依赖 `JAVA_HOME`，直接将 JDK 的 `bin` 路径写入 `PATH`，切换更干净
- ✅ 下载更可控：`jmt download` 默认仅下载 ZIP 包，若无 ZIP 则提示您手动下载 EXE，避免自动安装带来的不确定性
- ✅ 清理更全面：新增 `jmt remove temp` 一键清除下载缓存
- ✅ 源管理更灵活：内置常见镜像源，同时支持通过 `.repo` 目录自定义源

---

## ✨ 主要功能

- **全盘扫描** – 自动搜索 SSD 上的所有合法 JDK，并生成缓存，秒级响应。
- **版本切换** – 通过 `jmt use <version>` 一键切换 `PATH` 中的 JDK 路径（直接操作系统 PATH，立即生效）。
- **环境修复** – 自动清理其他 JDK 路径，确保只有一个 JDK 的 `bin` 在 PATH 中（由 JMT 管理）。
- **缓存自愈** – 智能检测缓存中的无效路径，自动剔除失效 JDK。
- **自注册 PATH** – 执行 `jmt env` 将工具目录加入系统 PATH（先删除旧条目再添加），全局可用。
- **下载安装** – 支持从镜像源或官方下载 JDK（ZIP 自动解压安装，EXE 仅下载到 `.temp` 手动运行）。
- **环境重置** – `jmt remove` 一键清除所有 JMT 创建的 PATH 条目，`remove temp` 清理下载缓存。
- **交互模式** – 直接运行 `jmt.exe`（无参数）进入交互式命令行，持续接受命令。

---

## 📦 安装

### 方式一：极简部署（推荐）
直接将 `jmt.exe` 复制到 `C:\Windows\System32` 目录，即可在任意终端中使用 `jmt` 命令（无需额外注册）。

### 方式二：手动注册（需管理员权限）
1. 将 `jmt.exe` 放在您喜欢的文件夹（如 `D:\Program Files\JMT`）。
2. 打开 **管理员** 命令提示符，进入该目录，执行：
   ```
   jmt env
   ```
   该命令会将当前目录添加到系统 PATH（若权限不足则自动降级到用户 PATH），之后您可以在任意终端使用 `jmt`。
3. **注意**：如果移动了 `jmt.exe` 的位置，再次执行 `jmt env` 会自动清理旧路径并添加新路径。

---

## 📖 命令手册

### 0. 无参数启动（交互模式）
直接双击 `jmt.exe` 或在终端输入 `jmt`（不带任何参数），进入交互式命令行：
```
=====================================
Java Manager Tool v1.6
=====================================
Usage:
  search [--force]    Scan all valid JDK & auto configure PATH
  list                Show all installed Java & current version
  use <num>           Switch PATH to specified JDK version
  env                 Add JMT directory to system PATH
  remove              Clean JMT-managed PATH entries
  remove temp         Delete .temp download cache
  download <num>      Download & install JDK (ZIP preferred)
  download <num> exe  Download EXE installer only (to .temp)
  rollback <ver>      Restore a deleted JDK from trash
  clean-trash         Permanently delete all trashed JDKs
  version             Show JMT version
  shell               Open new CMD with JMT interactive
  help [cmd]          Show help
  exit                Exit interactive mode

jmt>
```

---

### 1. `jmt search [--force]`
全盘扫描 SSD，查找所有合法 JDK，并生成缓存文件 `.jmt_cache`。  
**自动将最大版本（或唯一版本）的 `bin` 路径加入系统 PATH**，并移除其他旧版本（由 JMT 管理的）条目。
- `--force`：忽略缓存，强制重新扫描整个硬盘。

**示例输出**：
```
[INFO] 开始全盘扫描SSD，请稍候...
[INFO] 扫描完成，共找到 2 个JDK版本
[SUCCESS] 已切换 PATH 至 JDK 21（D:\Program Files\Java\jdk-21\bin）
[WARN] 请重启终端使 PATH 生效（或新开终端）
```

---

### 2. `jmt list`
显示当前已识别的所有 JDK 版本，并高亮标出当前生效的版本（绿色）。  
（当前生效版本即 PATH 中实际使用的 JDK，通过检查 `java -version` 或解析 PATH 判断。）

---

### 3. `jmt use <版本号>`
将 `PATH` 切换至指定版本的 JDK（如 `17`、`21`）。
- 该命令会从 PATH 中删除所有由 JMT 管理的其他 JDK `bin` 路径，然后添加目标版本的 `bin` 路径。
- 切换后 **需要重启终端** 才能在新终端中生效。

**示例**：
```
jmt use 21
[SUCCESS] 已切换 PATH 至 JDK 21（D:\Program Files\Java\jdk-21\bin）
[WARN] 请重启终端使环境变量生效！
```

---

### 4. `jmt env`
将 `jmt.exe` 所在的文件夹添加到系统 PATH 中（需管理员权限）。该命令会**先删除 PATH 中所有与该目录相同的旧条目，再追加一次**，确保路径最新且无重复。若系统级写入失败（权限不足），自动降级到用户级。

---

### 5. `jmt remove`
清除所有由 JMT 管理的环境配置：
- `jmt remove`（无参数）：默认清除所有 `JAVA_HOME*`（向后兼容）以及 PATH 中由 JMT 管理的 JDK `bin` 路径（根据内部管理列表）。
- `jmt remove env`：仅清理 JMT 自身目录的 PATH 条目（相当于撤销 `env`）。
- `jmt remove all`：完全清理（包含上述所有 + 删除管理列表）。
- **🔴 新增 `jmt remove temp`**：删除 `.temp` 下载缓存目录（若存在）。

---

### 6. `jmt download <版本号>`
自动下载指定版本的 JDK 并安装（默认优先尝试 ZIP 格式，若失败则回退官网）。

**行为细节（V1.6 新规）**：
- 若存在 ZIP 源（内置或 `.repo`），下载并解压到 `Java\jdk-<ver>` 目录，自动更新 PATH。
- 若 ZIP 源为 **Demo 包**，会打印醒目警告，但仍允许下载（您可自行判断）。
- 若 **没有 ZIP 源**，则不会自动下载 EXE，而是提示您使用 `jmt download <ver> exe` 手动下载 EXE 到 `.temp`，并手动运行安装。
- 所有下载文件（ZIP、EXE）均保存在 `.temp` 目录，ZIP 解压后删除，EXE 保留。

**示例**：
```
jmt download 17
[INFO] 找到 ZIP 源: https://mirrors.huaweicloud.com/openjdk/17/openjdk-17_windows-x64_bin.zip
[INFO] 正在下载 JDK 17 ...
[SUCCESS] JDK 17 已安装到 D:\Program Files\JMT\Java\jdk-17
[INFO] 正在更新 PATH ...
```

若为 Demo 包：
```
[WARN] ⚠️ 此版本为 DEMO 包，仅包含示例代码和演示功能！
[INFO] 是否继续下载？(y/n)
```

若无 ZIP 源：
```
[INFO] 未找到 ZIP 源，请使用 'jmt download 8 exe' 下载 EXE 安装程序（将保存到 .temp 目录，需手动安装）
```

### 7. `jmt download <版本号> exe`
强制下载指定版本的 EXE 安装程序到 `.temp` 目录（不自动安装），并提示用户手动运行。

**示例**：
```
jmt download 8 exe
[INFO] 正在下载 EXE 安装程序到 .temp ...
[SUCCESS] EXE 已保存到: D:\Program Files\JMT\.temp\jdk-8u202-windows-x64.exe
[WARN] 请手动运行该 EXE 安装 JDK 8，安装完成后执行 'jmt search' 刷新配置。
```

---

### 8. 其他命令
- `jmt version` – 显示 JMT 版本号。
- `jmt shell` – 在新的 CMD 窗口中打开 JMT 交互模式。
- `jmt rollback <版本>` – 恢复回收站中已删除的 JDK（需先有备份）。
- `jmt clean-trash [--force]` – 永久清空回收站（`.jmt_trash`）。

---

## ⚠️ 注意事项

- **管理员权限**：修改系统 PATH 需要管理员权限。若权限不足，工具会自动降级到用户 PATH（仅影响当前用户）。
- **终端重启**：环境变量修改后，**必须重启终端**（或重新打开命令提示符/PowerShell）才能使新 PATH 生效。
- **缓存问题**：如果手动移动或删除了 JDK 目录，请执行 `jmt search --force` 强制刷新缓存。
- **下载源**：内置了华为云等国内镜像，若您有特定源，可在 `.repo/jdk_zip_repo.txt` 中添加自定义 URL（程序启动时自动生成示例文件）。
- **PATH 管理**：JMT 会维护一个内部管理列表（注册表 `HKCU\Software\JMT\ManagedPaths`），所有通过 `search`/`use`/`download` 添加的 JDK `bin` 路径都会被记录，`remove` 时会统一清理，避免残留。

---

## 🔧 退出码（供脚本调用）

| 退出码 | 含义                       |
|--------|----------------------------|
| 0      | 命令执行成功               |
| 1      | 参数错误或未知命令         |
| 2      | 未找到指定 JDK 版本        |
| 3      | 权限不足（系统/用户变量均写入失败） |
| 4      | 网络错误或磁盘空间不足（下载失败） |

---

## 🛠 开发与构建

本项目使用 C++17 + Win32 API 开发，依赖 Visual Studio Build Tools 和 CMake。

### 构建步骤（CLion）
1. 打开项目，CMake 会自动加载。
2. 选择 Release 配置，点击 Build。
3. 生成的 `jmt.exe` 位于 `build/bin/Release/`。

### 命令行构建
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

---

## 📄 许可证
[MIT License](LICENSE)

---

**Enjoy managing Java versions with JMT!** 🚀
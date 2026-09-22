# JMT – Java Manager Tool V2.0

**JMT** 是一款 Windows 平台纯命令行 JDK 版本管理工具（类似 NVM for Node.js）：全盘扫描本机 JDK、一键切换版本、镜像/官方多源下载安装、环境变量自动维护。

- C++17 开发，仅依赖 Win32 API + C++ 标准库，静态链接 CRT，单 exe 分发
- 直接维护 PATH 中的 JDK `bin` 条目，不使用 `JAVA_HOME`
- 多源下载：Adoptium 镜像 + 华为云自动回退，ZIP 自动安装 / 安装包手动安装，官方直链兜底；curl / 多线程 Range 双下载引擎
- 交互式 REPL（无参运行 `jmt`）+ 单次命令双模式
- 删除先进回收站（`.trash`），可用 `rollback` 还原

当前版本：**Java Manager Tool v2.0 (build 2026.09.23)**（可通过 `jmt version` 查看）

---

## 功能一览

| 命令 | 功能 | 需要管理员权限 |
|------|------|----------------|
| `jmt search [--force]` | 扫描本机 JDK，PATH 中无 JDK 时自动切换到最大版本 | 是 |
| `jmt list` | 列出已识别版本，标注当前生效版本 | 否 |
| `jmt use <ver>` | 切换 PATH 中的 JDK `bin` 条目到指定版本 | 是 |
| `jmt download <ver>` | 下载并安装 JDK（ZIP 自动安装 / 安装包提示手动安装），自动设为当前版本 | 是 |
| `jmt env` | 注册 JMT 自身目录到 PATH | 是 |
| `jmt remove <子命令>` | 删除 JDK / 清理环境 / 清空回收站 | 是 |
| `jmt rollback [ver\|list]` | 从回收站恢复已删除的 JDK（不带参数即查看可回退版本） | 恢复需要，查看不需要 |
| `jmt version` | 显示版本信息 | 否 |
| `jmt help [命令]` | 显示帮助或指定命令详情 | 否 |

---

## 快速开始

### 1. 部署

把 `jmt.exe` 放到任意目录，以管理员身份运行：

```cmd
jmt env
```

该命令把 JMT 所在目录追加到系统 PATH（先删除同名旧条目再去重追加）。移动了 `jmt.exe` 之后再次执行 `jmt env` 即可修正路径。

> 提权行为：需要管理员权限的命令若当前未提权，JMT 会通过 UAC 重新以管理员身份启动，命令在**新弹出的窗口**中执行（执行完停留等待按键），原窗口立即以退出码 `0` 返回。

### 2. 扫描现有 JDK

```cmd
jmt search
```

扫描常见 Java 安装目录 + 所有固定磁盘（递归 3 层），结果写入缓存 `.jmt_cache`：

- PATH 中**已有** JMT 管理的 JDK：只报告扫描结果和当前版本，不改环境变量
- PATH 中**没有** JDK：自动把最大版本写入 PATH

手动移动/删除过 JDK 目录后，用 `jmt search --force` 忽略缓存强制重扫。

### 3. 切换版本

```cmd
jmt use 21
```

把 PATH 中所有 JDK `bin` 条目清掉，再追加目标版本的 `\bin`，最后广播 `WM_SETTINGCHANGE`。切换后请重启终端（或新开终端）使环境变量生效。

### 4. 下载安装新版本

```cmd
jmt download 17
```

按「Adoptium 镜像 → 华为云 → 官方直链」顺序自动回退；能拿到 ZIP 就自动解压安装并设为当前版本，只有安装包时下载后提示手动安装。默认安装根目录是**第一个可用的 `D:`~`Z:` 盘**下的 `Program Files\Java`，通常即 `D:\Program Files\Java\jdk-17`；若全部不可用则退回 `C:\Program Files\Java`。

---

## 命令详解

### `jmt search [--force]`

扫描并（在需要时）自动配置 JDK。

- 扫描范围：`C:\Program Files\Java`、`C:\Program Files (x86)\Java`、`C:\jdk`、`D:\Java`、`E:\Java`、`D:\jdk` 等常见目录，以及所有固定磁盘根目录，递归深度 3 层
- 排除目录：`C:\Windows`、`C:\ProgramData`、`C:\System Volume Information`、`$Recycle.Bin`、`System Volume Information`、`Recovery`、`Temp`
- 合法 JDK 判定：存在 `bin\java.exe` + `bin\javac.exe` + `lib` 目录，且目录名不是 `jre`
- 版本号来源：优先读 `release` 文件中的 `JAVA_VERSION`（`1.8.x` 归一化为 `8`），否则从路径名匹配 `jdk1.8` / `jdk-17` / `jdk17`
- 结果写缓存 `.jmt_cache`；`--force` 忽略缓存强制重扫

### `jmt list`

读取缓存（缓存缺失、条目失效或旧格式缓存时自动重扫）并列出全部版本，**按真实版本从高到低**排序，当前生效版本高亮显示。

版本号直接取自该 JDK 的 `release` 文件（`JAVA_VERSION`），不再被截断成主版本：

```
Java 22  -> D:\Program Files\Java\jdk-22
Java 17.0.9 (当前生效)  -> D:\Program Files\Java\jdk-17
Java 1.8.0_202  -> D:\Program Files\Java\jdk1.8.0_202
```

### `jmt use <version>`

1. 从缓存（必要时先扫描）按版本号精确匹配 JDK 路径
2. 删除 PATH 中所有「以 `bin` 结尾且路径包含 `jdk`」的条目
3. 移除 Oracle `javapath` 条目
4. 追加目标版本的 `\bin`
5. 广播 `WM_SETTINGCHANGE`

版本号不存在时返回退出码 `2`。

版本号支持三种写法（**不再要求只写主版本**）：

| 写法 | 例子 | 说明 |
|------|------|------|
| 完整版本 | `jmt use 17.0.9` | 精确匹配 |
| 主版本 / 前缀 | `jmt use 17`、`jmt use 17.0` | 命中多个时**自动选最高**并打印候选 |
| 旧版别名 | `jmt use 8`、`jmt use 1.8`、`jmt use 8u202` | 都指向 `1.8.0_202` |

其它：`--exact` 只接受完整版本；版本不存在时**不会弹 UAC**，直接报错并返回 `2`。

### `jmt env`

把 `jmt.exe` 所在目录注册到 PATH：先删除同名旧条目（忽略大小写与末尾斜杠），再去重追加，最后广播环境变更。

### `jmt remove <子命令>`

| 子命令 | 行为 |
|--------|------|
| `env` | 从 PATH 中移除 JMT 自身目录 |
| `all` | 完全清理：清空 JDK PATH 并恢复 Oracle javapath → 移除 JMT 目录 → 删除 `.trash` → 删除 `.temp` → 删除 `.jmt_cache` → 删除所有 `JAVA_HOME*` 变量（需按 `y` 确认） |
| `temp` | 删除 `.temp` 下载缓存目录 |
| `trash` | 永久清空回收站 `.trash`（需按 `y` 确认） |
| `<版本号>` | 把该版本目录移入 `.trash\jdk-<版本>_<时间戳>`，并清理相关 PATH 条目；版本写法与 `use` 相同（`remove 17.0.9` / `remove 17`） |

`remove <版本号>` 的 PATH 处理：

- 删的是当前生效版本：自动切换到剩余版本中的最大值；没有其它版本时清空 JDK PATH
- 删的不是当前版本：仅移除 PATH 中该版本的 `bin` 残留条目
- 结束前还会删除 `JAVA_HOME<版本>` 变量并更新缓存

移动失败（跨卷）时自动降级为「复制 + 删除原目录」，并在回收站目录写入 `.original_path` 元数据供 `rollback` 还原。

### `jmt download <version>`

没有开关参数：**能下载到 ZIP 就自动解压安装，只能拿到安装包（EXE/MSI）就下载后提示手动安装**，源按优先级自动回退。

```cmd
jmt download 21      # ZIP 可用 → 自动装好并设为当前版本
jmt download 8       # 8 只有 EXE → 下载安装程序并提示手动安装
```

版本参数支持完整版本：`jmt download 17.0.2` 会安装到 `jdk-17.0.2`（与同主版本的其它补丁目录并存），并校验实际版本是否满足请求。

### 下载流程

```
1. 解析  向 api.adoptium.net 问该主版本最新的 jdk 产物 → 权威文件名 + 官方直链
2. 构建  用该文件名按各镜像的目录布局拼候选链接（不同源布局不同，见下表）
3. 探测  对每个候选 Range 取 4 字节，用 Content-Type + 魔数判真假
4. 下载  ZIP 通过探测 → curl 下载 → 校验 ZIP 魔数 → 解压 → 自动安装
5. 兜底  ZIP 全部不可用 → 下载安装包到 .temp → 提示用户手动安装
```

第 3 步的探测不是多余的：部分镜像对不存在的文件返回 **HTTP 200 + text/html 的浏览器校验页**（实测中科大），只看状态码会把假页面当成功，白下 100+MB。

### 源与覆盖范围（2026-09 实测）

| 顺序 | 源 | 链接布局 | 覆盖 |
|------|-----|----------|------|
| 1 | 南京大学 `mirrors.nju.edu.cn/adoptium/` | `<base>/<主版本>/jdk/x64/windows/<文件名>` | Adoptium 全量：8、11、16+（仅最新补丁） |
| 2 | 清华 TUNA `mirrors.tuna.tsinghua.edu.cn/Adoptium/` | 同上（注意站点路径是**大写** `Adoptium`） | 同南大 |
| 3 | 华为云 `mirrors.huaweicloud.com/openjdk/` | `<base>/<主版本>/openjdk-<主版本>_windows-x64_bin.zip` | 12~26，但只有该大版本**初始 GA 包** |
| 4 | 华为云旧版 `repo.huaweicloud.com/java/jdk/` | 固定链接 | 6、7、8、9、10 的 EXE（手动安装） |
| 5 | Adoptium 官方 GitHub Releases | metadata 给出的直链 | 兜底，国内较慢 |

排除的源：**中科大**（取文件返回浏览器校验页）、**阿里云 / 北外**（无 adoptium 目录，404）、**NJU 单数域**（302 指向自身）。

速度实测（各取前 4MB，三轮均值）：清华 4.44、南大 4.16、华为云 4.04 MB/s —— 几乎持平，所以优先级按**覆盖面与版本新鲜度**排序，而不是按速度。

典型输出：

```
目标版本: 11
正在解析 Java 11 的下载信息...
最新产物: OpenJDK11U-jdk_x64_windows_hotspot_11.0.32.1_1.zip
查找可自动安装的压缩包（ZIP），候选 3 个源
[1/3] 南京大学镜像
  探测通过（ZIP），开始下载...
下载完成: 190.2 MB，平均 14.9 MB/s
JDK 11 安装成功（修复嵌套结构）
```

只有安装包时：

```
没有可自动安装的压缩包，改用安装程序（需手动安装）
正在下载安装程序（华为云（Oracle 旧版 EXE））...
下载完成: 211.6 MB，平均 11.3 MB/s
安装程序已下载: D:\...\.temp\jmt_download_bf54713e.exe
请手动运行上面的安装程序完成 JDK 8 安装，然后执行 'jmt search' 刷新缓存
```

### 版本覆盖面与限制

- `8` / `11` / `16` 及以上：Adoptium 镜像有 ZIP，**自动安装**
- `12` ~ `26`：即使 Adoptium 没有（12~15），华为云 GA 也提供 ZIP（初始 GA 包，非最新补丁）
- `8`：除 ZIP 外还提供 EXE，ZIP 失败时下载安装程序手动装
- `6` / `7` / `9` / `10`：没有 ZIP，但华为云旧库有安装程序 → **下载后手动安装**

```cmd
jmt download 6      # 只有 EXE：下载 6u45 安装程序并提示手动安装
```

实测各版本安装程序均可下载：6u45（60MB）、7u80（147MB）、8u202（222MB）、9.0.1、10.0.2。

### 下载时的其它行为

- 目标版本已存在时询问是否覆盖：确认后把旧目录移入回收站、必要时切换 PATH，再重新下载
- ZIP 下载后校验 `PK\x03\x04` 魔数，再用 PowerShell `Expand-Archive` 解压
- 解压结果为单层嵌套目录时自动上移内容并删除空壳目录
- 遇到限速/拒绝（HTTP 429/503/403）会**停止向该主机继续请求**并临时拉黑，不会换引擎重复请求叠加压力
- 安装成功后强制刷新缓存并把新版本设为当前版本

### `jmt rollback [version|list]`

```cmd
jmt rollback          # 查看回收站中可恢复的版本及原始路径（不带参数 = 下面的 list）
jmt rollback list     # 同上，显式写法
jmt rollback 17.0.9   # 恢复指定版本到原始路径
```

- 在 `.trash\jdk-<版本>_*` 中按创建时间取最新条目
- 读取 `.original_path` 得到原始路径（会清理控制字符与末尾斜杠）
- 原路径已存在时拒绝覆盖；父目录不存在时自动递归创建
- 优先 `MoveFileW`，失败则用提权 `xcopy /E /I /Y` 复制并删除回收站副本
- 恢复成功后强制重扫并刷新缓存
- 查看列表不需要管理员权限，且失败/为空时都会给出下一步提示

### `jmt version`

输出 `Java Manager Tool v2.0 (build 2026.09.23)`。在交互模式下前面会有一个空行（由 REPL 统一添加，见「交互模式」）。

### `jmt help [命令]`

不带参数显示分组帮助；带命令名时显示该命令说明，并对 `search` / `download` / `remove` / `rollback` 追加子命令或参数说明。

---

## 交互模式

不带任何参数运行 `jmt.exe` 进入 REPL：

```
Java Manager Tool  v2.0  ( build  2026.09.23 )

Usage:

JDK Management:
  search              Scan and auto-setup JDK (--force)
  list                List installed Java versions
  use <version>       Switch to a specific JDK version (--exact)
  download <version>  Download JDK and auto-setup (ZIP auto / installer manual)
  remove              Remove JDK or clean env (env/all/temp/trash/<version>)
  rollback            Restore a deleted JDK from trash (list/<version>)

Environment / Other:
  env                 Register JMT directory in PATH
  version             Show JMT version
  help [command]      Show help
  exit                Exit interactive mode

jmt> list

已安装的 Java 版本：
  Java 25  -> D:\Program Files\Java\jdk-25
  Java 22 (当前生效)  -> D:\Program Files\Java\jdk-22
  Java 17.0.9  -> D:\Program Files\Java\jdk-17
  Java 1.8.0_202  -> D:\Program Files\Java\jdk1.8.0_202
版本总数：4
当前生效版本: 22

jmt> version

Java Manager Tool v2.0 (build 2026.09.23)

jmt> exit
```

- `exit` / `quit` 退出；`Ctrl+C` 被捕获后仅重新显示提示符
- 每条命令的输出前都会空一行，让「命令行」与「结果」分得开
- 命令解析支持双引号包裹带空格的参数
- 需要提权的命令在 REPL 内由各命令自行触发提权（会另开窗口执行）

---

## 下载源管理

**不需要维护任何源列表**：`.repo` 目录与内置映射表已整体移除，JMT 也不再有可供编辑的源文件。

链接的生成方式按源类型区分（`src/jdk/download_sources.cpp` 的 `kSources`，数组顺序即优先级）：

| 源类型 | 链接构建 |
|--------|----------|
| Adoptium 镜像 | `<base>/<主版本>/jdk/x64/windows/<官方文件名>`（文件名来自 metadata） |
| 华为云 GA | `<base>/<主版本>/openjdk-<主版本>_windows-x64_bin.zip`（按大版本自己拼） |
| 华为云旧版 | 固定链接表（安装程序，需手动安装） |

官方文件名来自 `https://api.adoptium.net/v3/assets/latest/<版本>/hotspot?architecture=x64&image_type=jdk&os=windows&vendor=eclipse` 的 `name` 字段（同时取 `link` 作官方直链兜底）。

新增/更换镜像只需改 `kSources` 一处；各站路径大小写不同（南大 `/adoptium/`、清华 `/Adoptium/`）必须在表里分别写对。

下载引擎：优先调用系统 `curl.exe`（`-L --progress-bar --retry 2 --connect-timeout 15 --max-time 900`），失败时回退到内置多线程下载器（WinHTTP Range 分片，连接数跟随节流策略、超时 60 秒）。curl 的输出被 JMT 捕获用于读取 HTTP 状态码与进度，因此不会污染 JMT 的输出流。

**下载前的探测**：每个候选先 `-r 0-3` 取 4 字节，校验 `Content-Type` 不含 html 且魔数为 `PK`（ZIP）或 `MZ`（安装包）。这是必需的——镜像站的浏览器校验页与错误页常返回 HTTP 200 + text/html，只看状态码必然误判。

### 镜像友好（防止被镜像站封 IP）

下载链路内置节流策略，默认参数（定义在 `network/host_throttle.hpp`）：

| 策略 | 默认值 | 说明 |
|------|--------|------|
| 单主机并发 | **2** | 分片连接数跟随该值（不再是 4~32） |
| 同主机最小间隔 | 1 秒 | 相邻两次请求之间自动等待 |
| 重试退避 | 1s → 2s → 4s（±30% 抖动） | 避免多实例同步重试 |
| 请求预算 | 单 URL ≤6 次、单主机 ≤10 次、单次命令 ≤60 次 | 超预算立即停止，并在失败摘要里说明 |
| 限速信号 | 429 / 503 / 403 → **拉黑该主机 10 分钟** | 到期自动恢复；被拉黑的主机在尝试列表中直接跳过，且不再降级到分片引擎 |
| User-Agent | `JMT/2.0 (Windows; +https://github.com/sczerofox/JMT)` | 便于镜像方识别与放行 |

其它：`.temp` 里的下载文件按 URL 哈希唯一命名（多源/多实例不互相覆盖）；**下载过程中按 Ctrl+C 可中断**（会终止 curl 子进程并清理半成品）。

### 下载时的进度显示

下载期间应当能看到（以 `jmt download 17` 为例）：

```
目标版本: 11
正在解析 Java 11 的下载信息...
最新产物: OpenJDK11U-jdk_x64_windows_hotspot_11.0.32.1_1.zip
查找可自动安装的压缩包（ZIP），候选 3 个源
[1/3] 南京大学镜像
  探测通过（ZIP），开始下载...
下载中: 37%    已下载 70.5 MB    已用时 8 秒       ← 控制台上原地刷新
下载中: 25%    已下载 47.6 MB    已用时 5 秒
下载中: 50%    已下载 95.2 MB    已用时 11 秒
下载中: 76%    已下载 146.5 MB    已用时 10 秒
下载完成: 190.4 MB，平均 8.7 MB/s           ← 结束后汇总（字节数/平均速度）
JDK 11 安装成功
```

- 实时进度行（就是上面那条不带任何前缀的「下载中」行）在**真实控制台**里原地刷新；输出被重定向到文件时它不会出现，但**里程碑行（每 25%）与每 10 秒一次的普通进度行**都会写入文件，因此日志里也能确认下载在推进。
- 两种模式不会同时出现：控制台里只保留原地刷新那一行；重定向/管道里只保留普通进度行。
- 服务端不返回总长度时没有百分比，此时每 5 秒输出一行「正在连接... 已用时 N 秒    已下载 X MB」（一旦开始收到数据就变成「正在下载...」）。
- 每一行都带**已下载字节数**与**已用时**，不会再出现「看不出在不在下载」的情况。
- Debug 构建会额外打印一行 `curl: HTTP 200，190.4 MB，用时 22 秒，平均 8.7 MB/s，进度峰值 100%`（不参与等级着色）。

---

## 运行时目录

均位于 `jmt.exe` 同级目录，可整体删除（`jmt remove all` 会一并清理）：

```
├── .jmt_cache              # JDK 扫描缓存（UTF-16 LE，每行 版本号|路径）
├── .trash/                  # 回收站，jdk-<版本>_<时间戳> + .original_path
└── .temp/                   # 下载临时文件（ZIP 解压后删除，安装包保留供手动安装）
```

多线程下载的分块临时文件写在系统临时目录（`%TEMP%\jmt_part_<n>.tmp`），下载完成或失败后自动清理。

---

## 退出码

| 退出码 | 含义 |
|--------|------|
| 0 | 成功 |
| 1 | 参数错误 / 未知命令 |
| 2 | 未找到 JDK / 版本 |
| 3 | 权限不足（提权失败或注册表写入失败） |
| 4 | 网络或磁盘错误 |

---

## 注意事项

**管理员权限**：修改 PATH 需要管理员权限。JMT 检测到未提权时会通过 UAC 重新启动自身，命令在新窗口中执行，原窗口立即以 `0` 返回；提权失败时返回 `3`。

**终端重启**：环境变量修改通过广播 `WM_SETTINGCHANGE` 通知系统，已经打开的终端仍需重启才能读到新 PATH。

**PATH 管理策略**：JMT 用启发式规则识别 JDK 条目（路径以 `bin` 结尾且包含 `jdk`，忽略大小写与末尾斜杠），切换时先清空所有匹配条目再追加新条目；不使用 `JAVA_HOME`（`remove all` 会顺带清理它）。

**缓存维护**：缓存条目读取时逐条校验，失效条目自动剔除并重写；需要彻底重扫时用 `jmt search --force`。

**输出约定**：输出不再带 `[INFO]` / `[SUCCESS]` / `[WARN]` / `[ERROR]` 之类的文字前缀，等级只由**颜色**表达——绿=成功、黄=警告、红=错误、默认色=普通信息。输出被重定向到文件或管道时没有颜色，此时正文就是纯文本。

### 已知限制

以下行为来自当前源码实现，与直觉或旧版文档可能不同（已修复的历史问题列在末尾）：

1. **交互模式提权仍会另开窗口**：提权已统一到 `ElevationGate`（命令用元数据声明是否需要管理员权限），但成功提权后命令在**新窗口**执行并停留等待按键，REPL 会话本身继续留在原窗口。
2. **提权提示语在阶段 1 统一**：固定为「需要管理员权限，正在请求提权...」（原先 main 用的是「此操作需要管理员权限，正在请求...」）。
3. **镜像只保留最新补丁**：南大 / 清华的 Adoptium 目录里每个大版本通常只有最新补丁（如 8 只有 8u504b01），历史补丁不在镜像中；`jmt download <完整版本>` 拿到的是该主版本最新版，安装后按 `versionSatisfied` 校验，不一致会放弃该源。
4. **华为云 GA 只有初始包**：`mirrors.huaweicloud.com/openjdk/<主版本>/` 提供的是该大版本 GA 包（非最新补丁），且没有目录索引，无法枚举补丁版本；它只作为 Adoptium 未覆盖版本（12~15）的补充。
5. **镜像列表是编译期常量**：更换镜像或调整优先级需要改 `src/jdk/download_sources.cpp` 的 `kSources` 并重新编译；镜像站改版（如路径大小写变化）会让该站探测失败并自动回退到下一个。

已修复（保留记录，避免回退）：

- **输出可重定向**：stdout 不是控制台时改用 `WriteFile` 输出 UTF-8 + `CRLF`，`jmt help > out.txt` 有内容；控制台下的 ANSI 上色不变。
- **版本号单一来源**：应用名 / 版本号 / 构建日期集中在 `include/common/version.hpp`，`jmt version` 与 REPL banner 共用，不再各写一份。
- **用户级环境变量修复**：注册表用户级改用 `HKCU\Environment`（此前用户分支不可用）；`remove env` / `remove all` 写入失败会报错并返回 `3`。（作用域开关 `--user/--sys` 已在后续版本移除，需要提权的命令现在统一走正常提权流程。）
- **下载按「ZIP 优先、安装包兜底」自动决策**：不再需要 `exe` 开关；能下到 ZIP 就自动解压安装，只有安装包（EXE/MSI）时下载到 `.temp` 并提示手动安装。
- **下载源不再需要维护清单**：`.repo` 外部源文件与内置映射表已整体移除，改由 Adoptium API 的文件名 + 镜像站布局实时拼链接（详见「下载源管理」）；`--source` / `--mirror` / `java` 参数与交互式选源菜单一并删除，改为按源优先级自动回退。
- **下载前探测**：每个候选先取 4 字节验魔数，避免把镜像站的 HTML 校验页（HTTP 200）当成安装包白下 100+MB。

版本模型（当前阶段）新增：

- **不再截断成主版本**：`list` / `use` / `search` 的显示与比较都用真实版本（`17.0.9`、`1.8.0_202`、`22`）；「最大版本」按 feature → interim → update → patch 比较（旧实现用 `std::stoi` 会把 17.0.2 与 17.0.9 视为相等）。
- **`use` / `remove` 接受具体版本**：完整版本、主版本、前缀、`8u202` 别名都支持；命中多条自动取最高并打印候选。
- **缓存真的生效了**：修复了「读缓存时持独占文件锁、跨句柄读取被拒 → 每次都全盘扫描」的老问题；缓存加 schema 标记（v2），旧缓存自动重扫。

下载子系统（进行中）已落地：

- **镜像友好节流**：并发上限、请求间隔、三级请求预算、指数退避 + 抖动、429/503/403 拉黑（见上面表格）；
- **输出不被 curl 污染**：curl 静默 + 状态码解析，重定向到文件时不会混入 curl 进度条；
- **Ctrl+C 取消下载**：REPL 与单次命令模式都能中断并清理 `.temp` 半成品。

尚未完成（下一批）：统一安装管线（消除 ZIP/EXE/官方三份重复逻辑）、SHA-256 校验与解压路径安全校验、失败源持久化拉黑、官方源精确补丁版本（`/v3/binary/version/...`）。

---

## 构建

### 依赖

- Visual Studio Build Tools（MSVC，支持 C++17）
- CMake 3.15+

### 构建步骤

```bash
cmake -S . -B build                 # 配置（默认同时构建测试）
cmake --build build --config Release
cmake --build build --config Debug
```

输出 `jmt.exe`，无外部 DLL 依赖（静态链接 CRT，`MSVC_RUNTIME_LIBRARY=MultiThreaded`）。

### 单元测试与集成测试

业务代码（`src/main.cpp` 之外的全部源文件）编译为静态库 `jmt_core`，测试可执行文件链接同一份实现：

```bash
# Ninja 单配置生成器
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

# 只跑某一组
ctest --test-dir build -R unit.path_utils --output-on-failure
build/jmt_tests.exe --suite path_utils
build/jmt_tests.exe --list

# 关闭测试构建
cmake -S . -B build -DJMT_BUILD_TESTS=OFF
```

现有 4 组用例：`unit.string_helper`、`unit.path_utils`、`unit.version_parse`、`integration.cli_smoke`。约定：`tests/unit/` 用例不触碰注册表/PATH/网络；`tests/integration/` 只调用只读命令（`version`、`help`、未知命令）。`tests/` 已被 `.gitignore` 排除，仓库不发布测试代码；`CMakeLists.txt` 会先判断该目录是否存在，再决定是否注册测试目标。

### 链接库

- `Shlwapi.lib` - Shell 路径 API
- `Wininet.lib` / `Winhttp.lib` - HTTP 下载
- `Advapi32.lib` - 注册表操作与提权检测

---

## 项目结构

头文件与实现分离：`include/` 是头文件搜索根目录，`src/` 按模块一一对应，包含时统一写模块前缀（如 `#include "jdk/jdk_scan_service.hpp"`）。

```
include/                          src/
├── app/                          ├── app/              # 组合根：路径 / 上下文 / 运行时 / 提权门禁
│   ├── app_paths.hpp             ├── command/          # 9 个命令实现
│   ├── app_context.hpp           ├── jdk/              # 业务服务层（实例类，注入端口）
│   ├── app_runtime.hpp           ├── platform/         # 端口适配器：控制台输出 / 注册表 / 提权
│   └── elevation_gate.hpp        ├── system/           # 无状态 Win32 工具
├── command/                      ├── network/          # 下载节流 + curl 输出解析 + 多线程下载器
│   ├── command_base.hpp          ├── console/          # REPL
│   ├── command_registry.hpp      └── main.cpp          # 入口：装配、分发（调 AppRuntime）
│   └── *_command.hpp
├── platform/                     resources/
│   ├── output.hpp                ├── app.rc            # 图标资源
│   ├── registry.hpp              └── app.ico
│   ├── elevator.hpp
│   └── win_registry.hpp
├── jdk/
│   ├── jdk_scan_service.hpp
│   ├── java_env_service.hpp
│   ├── jdk_download_service.hpp
│   ├── download_sources.hpp      # 下载源清单与候选链接构建
│   ├── version_match.hpp
│   └── jmt_path_service.hpp
├── system/{path_utils,file_lock,utils}.hpp
├── network/{multi_thread_downloader,host_throttle,curl_output}.hpp
├── console/{repl_engine,repl_utils}.hpp
└── common/{string_helper,exit_code,result,version}.hpp
```

模块依赖方向：`common` → `platform` / `system` / `network` → `jdk` → `command` → `app`，`main.cpp` 只做装配与分发；端口层不含 `windows.h`，命令与服务只依赖端口。

---

## 许可证

MIT License

#include "jdk/jdk_scan_service.hpp"
#include "common/java_version.hpp"
#include "system/file_lock.hpp"
#include "system/path_utils.hpp"
#include "system/utils.hpp"
#include "common/string_helper.hpp"
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <regex>
#include <set>
#include <windows.h>

static const std::set<std::wstring> excludedDirs = {
        L"C:\\Windows", L"C:\\ProgramData", L"C:\\System Volume Information",
        L"$Recycle.Bin", L"System Volume Information", L"Recovery", L"Temp"
};

// 缓存 schema：写入时带上；读取时缺少该标记说明是旧缓存（只存主版本），需要重扫
static const std::wstring kCacheHeader = L"#jmt-cache-v2";

namespace {

// 判定阈值：只用来排除 0 字节 / 明显是占位文件的假货，不要求与发行版精确一致。
constexpr uint64_t kMinExeBytes = 1024;          // java.exe / javac.exe / jar.exe
constexpr uint64_t kMinDllBytes = 64 * 1024;     // jvm.dll
constexpr uint64_t kMinJarBytes = 64 * 1024;     // lib\tools.jar（JDK 8）
constexpr uint64_t kMinJmodBytes = 64 * 1024;    // jmods\java.base.jmod
constexpr uint64_t kMinModulesBytes = 1024 * 1024;  // lib\modules

struct ReleaseInfo {
    std::wstring javaVersion;   // JAVA_VERSION
    std::wstring implementor;   // IMPLEMENTOR（JDK 8 早期版本没有这一项）
};

std::wstring toLowerCopy(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(::towlower(ch)); });
    return text;
}

std::wstring trimCopy(const std::wstring& text) {
    size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == L' ' || text[begin] == L'\t' || text[begin] == L'\r' || text[begin] == L'\n')) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           (text[end - 1] == L' ' || text[end - 1] == L'\t' || text[end - 1] == L'\r' || text[end - 1] == L'\n')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::wstring lastComponent(const std::wstring& path) {
    std::wstring trimmed = path;
    while (!trimmed.empty() && (trimmed.back() == L'\\' || trimmed.back() == L'/')) {
        trimmed.pop_back();
    }
    const size_t pos = trimmed.find_last_of(L"\\/");
    return pos == std::wstring::npos ? trimmed : trimmed.substr(pos + 1);
}

// release 是 JDK/JRE 自带的小文本文件（KEY="VALUE" 每行一项），
// 比“目录里有没有 java.exe”可靠得多：版本、实现方、目标架构都以它为准。
bool parseReleaseContent(const std::wstring& content, ReleaseInfo& info) {
    size_t begin = 0;
    while (begin <= content.size()) {
        const size_t end = content.find(L'\n', begin);
        const std::wstring line =
                trimCopy(content.substr(begin, end == std::wstring::npos ? std::wstring::npos : end - begin));
        if (!line.empty()) {
            const size_t equals = line.find(L'=');
            if (equals != std::wstring::npos) {
                const std::wstring key = toLowerCopy(trimCopy(line.substr(0, equals)));
                std::wstring value = trimCopy(line.substr(equals + 1));
                if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
                    value = value.substr(1, value.size() - 2);
                }
                if (key == L"java_version") {
                    info.javaVersion = value;
                } else if (key == L"implementor") {
                    info.implementor = value;
                }
            }
        }
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    return !info.javaVersion.empty();
}

bool isFileAtLeast(const std::wstring& path, uint64_t minBytes) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;
    const uint64_t size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return size >= minBytes;
}

bool readReleaseInfo(const std::wstring& path, ReleaseInfo& info) {
    // 先查属性再读文件：全盘扫描时绝大多数目录没有 release，避免每个目录都真的去打开文件
    const std::wstring releasePath = JoinPath(path, L"release");
    if (!isFileAtLeast(releasePath, 1)) return false;
    const std::wstring content = ReadFileText(releasePath);
    return !content.empty() && parseReleaseContent(content, info);
}

// 只认 PE 文件头（MZ）：防止把说明文件、下载残留改名成 java.exe 后蒙混过关。
bool isPeFileAtLeast(const std::wstring& path, uint64_t minBytes) {
    if (!isFileAtLeast(path, minBytes)) return false;

    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;

    unsigned char magic[2] = {0, 0};
    DWORD read = 0;
    const BOOL ok = ReadFile(handle, magic, 2, &read, nullptr);
    CloseHandle(handle);
    return ok && read == 2 && magic[0] == 'M' && magic[1] == 'Z';
}

// JVM 动态库位置随版本/位数不同：JDK 9+ 在 bin\server，JDK 8 及更早通常在 jre\bin\server
// （32 位构建可能只有 client），这里把这几种合法位置都认下来。
bool hasJvmDll(const std::wstring& path) {
    static const wchar_t* candidates[] = {
            L"bin\\server\\jvm.dll",
            L"bin\\client\\jvm.dll",
            L"jre\\bin\\server\\jvm.dll",
            L"jre\\bin\\client\\jvm.dll",
    };
    for (const wchar_t* candidate : candidates) {
        if (isFileAtLeast(JoinPath(path, candidate), kMinDllBytes)) return true;
    }
    return false;
}

// IDE 自带的运行时（JetBrains Runtime）也长着 java.exe/javac.exe/lib，
// 但它是 IDE 的私有运行环境，会被 IDE 升级覆盖，不应该被 JMT 收录或切到 PATH。
bool isIdeBundledRuntime(const std::wstring& dirName, const std::wstring& implementor) {
    const std::wstring lowerName = toLowerCopy(dirName);
    if (lowerName == L"jbr" || lowerName == L"jbrsdk") return true;
    return toLowerCopy(implementor).find(L"jetbrains") != std::wstring::npos;
}

// release 里的 JAVA_VERSION 可能带 -ea / +build 后缀；先按原样解析，失败再剥后缀。
JavaVersion parseReleaseVersion(const std::wstring& raw) {
    JavaVersion parsed = JavaVersion::parse(raw);
    if (parsed.valid()) return parsed;

    const size_t dash = raw.find(L'-');
    if (dash != std::wstring::npos) {
        parsed = JavaVersion::parse(raw.substr(0, dash));
    }
    return parsed;
}

}  // namespace

static void ScanDirectoryRecursive(const std::wstring& path, int depth, int maxDepth,
                                   std::vector<std::pair<std::wstring, std::wstring>>& result,
                                   std::set<std::wstring>& seen, int& found, const std::wstring& drive) {
    if (depth > maxDepth) return;
    if (!IsDirectory(path)) return;

    std::wstring normalizedPath = path;
    if (!normalizedPath.empty() && normalizedPath.back() == L'\\')
        normalizedPath.pop_back();
    if (excludedDirs.find(normalizedPath) != excludedDirs.end())
        return;

    const JdkInfo info = JdkScanService::inspect(path);
    if (info.valid) {
        // 按路径去重：同一版本装了多份时（如 jdk-17 与 jdk-17.0.9 都是 17.0.9）都保留
        const std::wstring key = PathUtils::normalize(path);
        if (seen.insert(key).second) {
            result.push_back({info.version, path});
            found++;
        }
        // JDK 目录内部不会再套 JDK，找到后不必继续向下递归
        return;
    }

    std::wstring searchPath = path + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring subName = fd.cFileName;
            if (subName == L"." || subName == L"..") continue;
            std::wstring subPath = JoinPath(path, subName);
            ScanDirectoryRecursive(subPath, depth + 1, maxDepth, result, seen, found, drive);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

JdkInfo JdkScanService::inspect(const std::wstring& path) {
    JdkInfo info;
    if (!IsDirectory(path)) {
        info.reason = L"路径不是目录";
        return info;
    }

    const std::wstring dirName = lastComponent(path);
    if (toLowerCopy(dirName) == L"jre") {
        info.reason = L"这是 JRE（目录名 jre），不含编译器";
        return info;
    }

    // 1. release 是所有正常 JDK 都有的“身份证”，版本也以它为准
    ReleaseInfo release;
    if (!readReleaseInfo(path, release)) {
        info.reason = L"缺少 release 文件或其中没有 JAVA_VERSION";
        return info;
    }

    const JavaVersion version = parseReleaseVersion(release.javaVersion);
    if (!version.valid()) {
        info.reason = L"release 中的 JAVA_VERSION 无法解析: " + release.javaVersion;
        return info;
    }

    // 2. IDE 私有运行时不是独立 JDK
    if (isIdeBundledRuntime(dirName, release.implementor)) {
        info.reason = L"IDE 内置的 JetBrains Runtime，不作为 JDK 管理";
        return info;
    }

    // 3. 必须同时具备“运行 + 编译 + 打包”三件套，且都是真正的 PE 可执行文件
    const std::wstring javaExe = JoinPath(path, L"bin\\java.exe");
    const std::wstring javacExe = JoinPath(path, L"bin\\javac.exe");
    const std::wstring jarExe = JoinPath(path, L"bin\\jar.exe");
    if (!isPeFileAtLeast(javaExe, kMinExeBytes)) {
        info.reason = L"bin\\java.exe 缺失或不是有效的 Windows 可执行文件";
        return info;
    }
    if (!isPeFileAtLeast(javacExe, kMinExeBytes)) {
        info.reason = L"bin\\javac.exe 缺失或不是有效的 Windows 可执行文件（不是 JDK）";
        return info;
    }
    if (!isPeFileAtLeast(jarExe, kMinExeBytes)) {
        info.reason = L"bin\\jar.exe 缺失（不是完整的 JDK）";
        return info;
    }

    if (version.feature >= 9) {
        // JDK 9+：模块化运行时 + JMOD 开发镜像 + HotSpot 虚拟机
        if (!isFileAtLeast(JoinPath(path, L"lib\\modules"), kMinModulesBytes)) {
            info.reason = L"lib\\modules 缺失（不是完整的 JDK 运行时）";
            return info;
        }
        if (!isFileAtLeast(JoinPath(path, L"jmods\\java.base.jmod"), kMinJmodBytes)) {
            info.reason = L"jmods\\java.base.jmod 缺失（只有运行时，没有开发镜像）";
            return info;
        }
        if (!hasJvmDll(path)) {
            info.reason = L"jvm.dll 缺失（无法启动 Java 虚拟机）";
            return info;
        }
    } else {
        // JDK 6/7/8：tools.jar 是 JDK 专属，jvm.dll 在私有 JRE 里
        if (!isFileAtLeast(JoinPath(path, L"lib\\tools.jar"), kMinJarBytes)) {
            info.reason = L"lib\\tools.jar 缺失（不是 JDK，可能只是 JRE）";
            return info;
        }
        if (!hasJvmDll(path)) {
            info.reason = L"jvm.dll 缺失（无法启动 Java 虚拟机）";
            return info;
        }
    }

    info.valid = true;
    info.version = release.javaVersion;
    info.implementor = release.implementor;
    return info;
}

bool JdkScanService::isValidJdk(const std::wstring& path) {
    return inspect(path).valid;
}

std::wstring JdkScanService::extractVersion(const std::wstring& path) {
    // 1. 优先从 release 文件读取 JAVA_VERSION（权威来源，原样返回，不再截断成主版本）
    ReleaseInfo release;
    if (readReleaseInfo(path, release)) {
        const JavaVersion version = parseReleaseVersion(release.javaVersion);
        if (version.valid()) {
            return version.raw;   // "17.0.9" / "22" / "1.8.0_202"
        }
    }

    // 2. release 不可用时从路径中提取（兼容 jdk-17.0.2 / jdk1.8.0_202 / jdk17 / openjdk-11.0.2）
    //    注意：inspect() 要求 release 存在，因此真实扫描不会走到这里；
    //    保留该分支是为了让 extractVersion 在没有文件系统访问（单测）时仍可解析路径。
    std::wregex pathPattern(L"(?:jdk|openjdk)[-_]?(\\d+(?:[uU]\\d+)?(?:\\.\\d+)*(?:_\\d+)?)");
    std::wsmatch pathMatch;
    if (std::regex_search(path, pathMatch, pathPattern)) {
        const JavaVersion version = JavaVersion::parse(pathMatch[1].str());
        if (version.valid()) {
            return version.raw;
        }
    }
    return L"";
}

std::wstring JdkScanService::serializeCache(const std::vector<VersionCandidate>& jdks) {
    std::wstring content = kCacheHeader + L"\n";
    for (const auto& [ver, path] : jdks) {
        content += ver + L"|" + path + L"\n";
    }
    return content;
}

bool JdkScanService::parseCache(const std::wstring& content, std::vector<VersionCandidate>& out) {
    out.clear();
    if (content.rfind(kCacheHeader, 0) != 0) {
        return false;   // 旧格式（无 schema 标记），调用方应重新扫描
    }
    const auto lines = StringHelper::split(content, L'\n', false);
    for (const auto& line : lines) {
        if (line.empty() || line[0] == L'#') continue;
        const auto parts = StringHelper::split(line, L'|', false);
        if (parts.size() >= 2) {
            out.push_back({parts[0], parts[1]});
        }
    }
    return true;
}

// 写入缓存（定义在 scanJdks 之前，确保可见）
void JdkScanService::writeCache(const std::vector<std::pair<std::wstring, std::wstring>>& jdks) {
    FileLock lock(paths_.cacheFile);
    if (!lock.tryLock()) {
        out_.line(OutputLevel::Warning, L"无法获取缓存文件锁，跳过缓存写入");
        return;
    }

    const std::wstring content = serializeCache(jdks);

    if (!lock.writeAllText(content)) {
        out_.line(OutputLevel::Error, L"写入缓存文件失败: " + paths_.cacheFile);
    } else {
        out_.line(OutputLevel::Debug, L"缓存写入成功");
    }
}

std::vector<std::pair<std::wstring, std::wstring>> JdkScanService::scanJdks(
        bool force,
        bool silent) {
    std::vector<std::pair<std::wstring, std::wstring>> result;

    if (!force) {
        // 读取不加锁：写侧由 FileLock 串行化，读侧读到半成品时解析会失败并回退到重扫。
        // （此前读操作在持锁句柄之外进行，会被字节范围锁拒绝，导致缓存永远失效、每次全盘扫描）
        const std::wstring content = ReadFileText(paths_.cacheFile);
        if (!content.empty()) {
            std::vector<VersionCandidate> cached;
            if (parseCache(content, cached)) {
                bool cacheDirty = false;
                for (const auto& [ver, path] : cached) {
                    // 缓存只负责“已知路径”，是否仍是 JDK 以及真实版本必须重新判定：
                    // 这样原地升级、目录被替换、旧规则误收录的 JBR 都会在本次自愈。
                    const JdkInfo info = inspect(path);
                    if (info.valid) {
                        result.push_back({info.version, path});
                        if (info.version != ver) {
                            cacheDirty = true;
                            out_.line(OutputLevel::Debug,
                                      L"缓存版本已变化: " + path + L"（" + ver + L" -> " + info.version + L"）");
                        }
                    } else {
                        cacheDirty = true;
                        out_.line(OutputLevel::Debug,
                                  L"缓存条目已失效: " + path + L"（" + info.reason + L"）");
                    }
                }
                if (cacheDirty) {
                    writeCache(result);
                }
                return result;
            }
            out_.line(OutputLevel::Debug, L"缓存缺少 schema 标记（旧格式），重新扫描");
        }
    }

    out_.line(OutputLevel::Info, L"开始全盘扫描SSD，请稍候...");
    auto drives = GetAvailableDrives();
    std::set<std::wstring> seen;
    int found = 0;

    std::vector<std::wstring> commonPaths = {
            L"C:\\Program Files\\Java",
            L"C:\\Program Files (x86)\\Java",
            L"D:\\Java",
            L"E:\\Java",
            L"C:\\jdk",
            L"D:\\jdk"
    };
    for (const auto& p : commonPaths) {
        if (IsDirectory(p)) {
            ScanDirectoryRecursive(p, 0, 3, result, seen, found, L"common");
        }
    }

    for (const auto& drive : drives) {
        ScanDirectoryRecursive(drive, 0, 3, result, seen, found, drive);
    }

    out_.clearProgress();

    // 输出所有找到的 JDK 路径（受 silent 控制）
    if (!silent && !result.empty()) {
        for (const auto& [ver, path] : result) {
            out_.line(OutputLevel::Info, L"找到 JDK: " + path);
        }
    }

    out_.line(OutputLevel::Info, L"扫描完成，共找到 " + std::to_wstring(found) + L" 个JDK版本");
    writeCache(result);
    return result;
}

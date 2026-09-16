#include "jdk/jdk_scan_service.hpp"
#include "common/java_version.hpp"
#include "system/file_lock.hpp"
#include "system/path_utils.hpp"
#include "system/utils.hpp"
#include "common/string_helper.hpp"
#include <algorithm>
#include <regex>
#include <set>

static const std::set<std::wstring> excludedDirs = {
        L"C:\\Windows", L"C:\\ProgramData", L"C:\\System Volume Information",
        L"$Recycle.Bin", L"System Volume Information", L"Recovery", L"Temp"
};

// 缓存 schema：写入时带上；读取时缺少该标记说明是旧缓存（只存主版本），需要重扫
static const std::wstring kCacheHeader = L"#jmt-cache-v2";

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

    if (JdkScanService::isValidJdk(path)) {
        // 按路径去重：同一版本装了多份时（如 jdk-17 与 jdk-17.0.9 都是 17.0.9）都保留
        const std::wstring key = PathUtils::normalize(path);
        if (seen.insert(key).second) {
            std::wstring ver = JdkScanService::extractVersion(path);
            if (!ver.empty()) {
                result.push_back({ver, path});
                found++;
            }
        }
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

bool JdkScanService::isValidJdk(const std::wstring& path) {
    if (!IsDirectory(path)) return false;

    // 硬性排除目录名为 jre 的路径
    std::wstring dirName = path;
    size_t pos = dirName.find_last_of(L'\\');
    if (pos != std::wstring::npos) dirName = dirName.substr(pos + 1);
    std::transform(dirName.begin(), dirName.end(), dirName.begin(), ::towlower);
    if (dirName == L"jre") return false;

    // 基本骨架检查：必须存在 java.exe、javac.exe 和 lib 目录
    if (GetFileAttributesW(JoinPath(path, L"bin\\java.exe").c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    if (GetFileAttributesW(JoinPath(path, L"bin\\javac.exe").c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    if (!IsDirectory(JoinPath(path, L"lib"))) return false;

    return true;
}

std::wstring JdkScanService::extractVersion(const std::wstring& path) {
    // 1. 优先从 release 文件读取 JAVA_VERSION（原样返回，不再截断成主版本）
    std::wstring releasePath = JoinPath(path, L"release");
    std::wstring content = ReadFileText(releasePath);
    if (!content.empty()) {
        std::wregex pattern(L"JAVA_VERSION=\"([^\"]+)\"");
        std::wsmatch match;
        if (std::regex_search(content, match, pattern)) {
            const JavaVersion version = JavaVersion::parse(match[1].str());
            if (version.valid()) {
                return version.raw;   // "17.0.9" / "22" / "1.8.0_202"
            }
        }
    }

    // 2. 从路径中提取（兼容 jdk-17.0.2 / jdk1.8.0_202 / jdk17 / openjdk-11.0.2）
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
                for (const auto& [ver, path] : cached) {
                    if (JdkScanService::isValidJdk(path)) {
                        result.push_back({ver, path});
                    } else {
                        out_.line(OutputLevel::Debug, L"Cache entry invalid: " + path);
                    }
                }
                if (result.size() != cached.size()) {
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

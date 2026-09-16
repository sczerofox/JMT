#include "jdk/jdk_scan_service.hpp"
#include "system/file_lock.hpp"
#include "system/utils.hpp"
#include "common/string_helper.hpp"
#include "console/console_progress.hpp"
#include "console/color_print.hpp"
#include <algorithm>
#include <regex>
#include <set>

static const std::set<std::wstring> excludedDirs = {
        L"C:\\Windows", L"C:\\ProgramData", L"C:\\System Volume Information",
        L"$Recycle.Bin", L"System Volume Information", L"Recovery", L"Temp"
};

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
        std::wstring ver = JdkScanService::extractVersion(path);
        if (!ver.empty() && seen.find(ver) == seen.end()) {
            seen.insert(ver);
            result.push_back({ver, path});
            found++;
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
    // 1. 优先从 release 文件读取
    std::wstring releasePath = JoinPath(path, L"release");
    std::wstring content = ReadFileText(releasePath);
    if (!content.empty()) {
        std::wregex pattern(L"JAVA_VERSION=\"(\\d+)(?:\\.(\\d+))?");
        std::wsmatch match;
        if (std::regex_search(content, match, pattern)) {
            int major = std::stoi(match[1].str());
            if (major == 1 && match.size() > 2 && !match[2].str().empty())
                return match[2].str();  // "8"
            else
                return match[1].str();  // "17"
        }
    }

    // 2. 从路径中提取（兼容 jdk1.8.0_202, jdk-17, jdk17）
    std::wregex pathPattern(L"jdk(?:1\\.(\\d+)|[-_]?(\\d+))");
    std::wsmatch pathMatch;
    if (std::regex_search(path, pathMatch, pathPattern)) {
        if (pathMatch[1].matched)
            return pathMatch[1].str();   // "8"
        else if (pathMatch[2].matched)
            return pathMatch[2].str();   // "17"
    }
    return L"";
}

// 写入缓存（定义在 scanJdks 之前，确保可见）
void JdkScanService::writeCache(const std::vector<std::pair<std::wstring, std::wstring>>& jdks,
                                const std::wstring& cachePath) {
    FileLock lock(cachePath);
    if (!lock.tryLock()) {
        PrintWarning(L"无法获取缓存文件锁，跳过缓存写入");
        return;
    }

    std::wstring content;
    for (const auto& [ver, path] : jdks) {
        content += ver + L"|" + path + L"\n";
    }

    if (!lock.writeAllText(content)) {
        PrintError(L"写入缓存文件失败: " + cachePath);
    } else {
        PrintDebug(L"缓存写入成功");
    }
}

// 修正：定义为类的静态成员函数
std::vector<std::pair<std::wstring, std::wstring>> JdkScanService::scanJdks(
        bool force,
        const std::wstring& cachePath,
        bool silent) {
    std::vector<std::pair<std::wstring, std::wstring>> result;

    if (!force) {
        FileLock lock(cachePath);
        if (lock.tryLock()) {
            std::wstring content = ReadFileText(cachePath);
            if (!content.empty()) {
                auto lines = StringHelper::split(content, L'\n', false);
                for (auto& line : lines) {
                    auto parts = StringHelper::split(line, L'|', false);
                    if (parts.size() >= 2) {
                        std::wstring ver = parts[0];
                        std::wstring path = parts[1];
                        if (JdkScanService::isValidJdk(path)) {
                            result.push_back({ver, path});
                        } else {
                            PrintDebug(L"Cache entry invalid: " + path);
                        }
                    }
                }
                if (result.size() != lines.size()) {
                    JdkScanService::writeCache(result, cachePath);
                }
                return result;
            }
        }
    }

    PrintInfo(L"开始全盘扫描SSD，请稍候...");
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

    ConsoleProgress::ClearLine();

    // 输出所有找到的 JDK 路径（受 silent 控制）
    if (!silent && !result.empty()) {
        for (const auto& [ver, path] : result) {
            PrintInfo(L"找到 JDK: " + path);
        }
    }

    PrintInfo(L"扫描完成，共找到 " + std::to_wstring(found) + L" 个JDK版本");
    JdkScanService::writeCache(result, cachePath);
    return result;
}

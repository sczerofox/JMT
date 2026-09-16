// 确保 UNICODE 已定义（否则 URL_COMPONENTS 字段为 char* 而非 wchar_t*）
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <winhttp.h>
#include "jdk/jdk_download_service.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "console/color_print.hpp"
#include "system/utils.hpp"
#include "network/multi_thread_downloader.hpp"
#include <wininet.h>
#include <memory>
#include <regex>
#include <vector>
#include <map>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <sstream>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "winhttp.lib")

namespace fs = std::filesystem;

// ---------- 静态成员定义 ----------
std::map<std::wstring, JdkDownloadService::UrlList> JdkDownloadService::zipMap_;
std::map<std::wstring, JdkDownloadService::UrlList> JdkDownloadService::exeMap_;

// ---------- 前置声明 ----------
bool IsValidZipFile(const std::wstring& path);

// ---------- 辅助函数：获取 .temp 目录（与 exe 同级） ----------
static std::wstring GetTempDir() {
    std::wstring exeDir = GetExeDirectory();
    std::wstring tempDir = JoinPath(exeDir, L".temp");
    if (!IsDirectory(tempDir)) {
        CreateDirectoryW(tempDir.c_str(), nullptr);
    }
    return tempDir;
}

// 从 URL 提取文件名（去掉查询参数）
static std::wstring ExtractFileNameFromUrl(const std::wstring& url) {
    size_t pos = url.find_last_of(L'/');
    if (pos == std::wstring::npos) return L"";
    std::wstring fileName = url.substr(pos + 1);
    size_t qpos = fileName.find(L'?');
    if (qpos != std::wstring::npos) {
        fileName = fileName.substr(0, qpos);
    }
    return fileName;
}

// 修复嵌套目录：若 targetDir 下只有一个子目录且该子目录是有效的 JDK，则将其内容上移并删除空目录
static bool FixNestedJdkDirectory(const std::wstring& targetDir) {
    std::vector<fs::path> subDirs;
    try {
        for (const auto& entry : fs::directory_iterator(targetDir)) {
            if (fs::is_directory(entry.status())) {
                subDirs.push_back(entry.path());
            }
        }
    } catch (const std::exception& e) {
        PrintDebug(L"FixNestedJdkDirectory: 遍历目录异常 " + ToWideString(e.what()));
        return false;
    }

    if (subDirs.size() != 1) {
        PrintDebug(L"FixNestedJdkDirectory: 子目录数量不为1，实际=" + std::to_wstring(subDirs.size()));
        return false;
    }

    fs::path subPath = subDirs[0];
    if (!JdkScanService::isValidJdk(subPath.wstring())) {
        PrintDebug(L"FixNestedJdkDirectory: 子目录不是有效的 JDK");
        return false;
    }

    // 移动子目录内容到 targetDir
    try {
        for (const auto& entry : fs::directory_iterator(subPath)) {
            fs::path dest = fs::path(targetDir) / entry.path().filename();
            if (fs::exists(dest)) {
                fs::remove_all(dest);  // 若同名存在则删除（一般不会）
            }
            fs::rename(entry.path(), dest);
        }
        fs::remove(subPath);  // 删除空子目录
        PrintDebug(L"FixNestedJdkDirectory: 移动成功");
        return true;
    } catch (const std::exception& e) {
        PrintDebug(L"FixNestedJdkDirectory: 移动失败 " + ToWideString(e.what()));
        return false;
    }
}

// ---------- 辅助函数：生成临时文件路径（在 .temp 下） ----------
static std::wstring GetTempDownloadPath(const std::wstring& ext = L"tmp") {
    std::wstring tempDir = GetTempDir();
    return JoinPath(tempDir, L"jmt_download." + ext);
}

// ---------- 辅助函数：使用 curl.exe 下载 ----------
static bool DownloadFileWithCurl(const std::wstring& url, const std::wstring& destPath) {
    std::wstring cleanUrl = url;
    cleanUrl.erase(std::remove_if(cleanUrl.begin(), cleanUrl.end(),
                                  [](wchar_t ch) { return ch <= 0x20; }), cleanUrl.end());

    wchar_t curlPath[MAX_PATH];
    DWORD len = SearchPathW(nullptr, L"curl.exe", nullptr, MAX_PATH, curlPath, nullptr);
    if (len == 0) {
        wcscpy_s(curlPath, L"C:\\Windows\\System32\\curl.exe");
        if (!IsFile(curlPath)) {
            return false;
        }
    }

    std::wstring cmdLine = L"\"" + std::wstring(curlPath) + L"\" -L --retry 3 -o \"" + destPath + L"\" \"" + cleanUrl + L"\" --progress-bar";

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(nullptr, (LPWSTR)cmdLine.c_str(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &si, &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
        DeleteFileW(destPath.c_str());
        return false;
    }

    return IsFile(destPath);
}

// ---------- 辅助函数：多线程下载 ----------
static bool DownloadFileWithMultiThread(const std::wstring& url, const std::wstring& destPath) {
    std::wstring cleanUrl = url;
    cleanUrl.erase(std::remove_if(cleanUrl.begin(), cleanUrl.end(),
                                  [](wchar_t ch) { return ch <= 0x20; }), cleanUrl.end());

    MultiThreadDownloader downloader;
    downloader.setConnections(4);
    downloader.setTimeout(60);
    downloader.setRetryCount(3);

    bool success = false;
    std::wstring errorMsg;

    PrintInfo(L"使用多线程加速下载...");
    success = downloader.download(cleanUrl, destPath,
                                  [](const DownloadProgress& prog) {
                                      if (prog.percent >= 0) {
                                          PrintProgress(L"下载进度: " + std::to_wstring(prog.percent) + L"%");
                                      }
                                  },
                                  [&](const std::wstring& err) {
                                      errorMsg = err;
                                  }
    );

    if (!success) {
        PrintError(L"多线程下载失败: " + errorMsg);
        DeleteFileW(destPath.c_str());
        return false;
    }

    if (!IsFile(destPath)) {
        PrintError(L"下载文件不存在");
        return false;
    }

    try {
        auto size = fs::file_size(destPath);
        if (size < 1024 * 1024) {
            PrintWarning(L"文件大小异常（" + std::to_wstring(size / 1024) + L" KB），可能不是有效的 JDK 安装包");
            DeleteFileW(destPath.c_str());
            return false;
        }
        std::wstring ext = destPath.substr(destPath.find_last_of(L'.') + 1);
        if (ext == L"zip" && !IsValidZipFile(destPath)) {
            PrintWarning(L"下载的文件不是有效的 ZIP 文件");
            DeleteFileW(destPath.c_str());
            return false;
        }
        PrintInfo(L"下载完成，共 " + std::to_wstring(size / 1024) + L" KB");
        return true;
    } catch (const std::exception&) {
        PrintError(L"获取文件大小失败");
        DeleteFileW(destPath.c_str());
        return false;
    }
}

// ---------- 统一的下载入口 ----------
static bool DownloadFile(const std::wstring& url, const std::wstring& destPath) {
    if (DownloadFileWithCurl(url, destPath)) {
        return true;
    }
    PrintWarning(L"curl 下载失败，尝试多线程下载...");
    return DownloadFileWithMultiThread(url, destPath);
}

// ---------- 解压 ZIP ----------
static bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir) {
    // 确保目标目录存在
    CreateDirectoryW(destDir.c_str(), nullptr);

    std::wstring cmd = L"powershell -Command \"Expand-Archive -Path '" + zipPath + L"' -DestinationPath '" + destDir + L"' -Force; if ($?) { exit 0 } else { exit 1 }\"";
    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(nullptr, (LPWSTR)cmd.c_str(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        PrintDebug(L"ExtractZip: CreateProcess 失败");
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
        PrintDebug(L"ExtractZip: PowerShell 退出码 " + std::to_wstring(exitCode));
        return false;
    }
    return true;
}

// ---------- 校验 ZIP 文件头 ----------
bool IsValidZipFile(const std::wstring& path) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    BYTE buffer[4];
    DWORD read = 0;
    BOOL ok = ReadFile(hFile, buffer, 4, &read, nullptr);
    CloseHandle(hFile);
    if (!ok || read != 4) return false;
    return (buffer[0] == 0x50 && buffer[1] == 0x4B && buffer[2] == 0x03 && buffer[3] == 0x04);
}

// ============================================================
// ========== 资源映射管理（第二步核心功能） ==================
// ============================================================

// ---------- 获取 .repo 目录 ----------
static std::wstring GetRepoDir() {
    std::wstring exeDir = GetExeDirectory();
    std::wstring repoDir = JoinPath(exeDir, L".repo");
    if (!IsDirectory(repoDir)) {
        CreateDirectoryW(repoDir.c_str(), nullptr);
    }
    return repoDir;
}

// ---------- 解析文本文件（每行一个 URL） ----------
static std::vector<std::wstring> ReadUrlListFromFile(const std::wstring& filePath) {
    std::vector<std::wstring> urls;
    std::wstring content = ReadFileText(filePath);
    if (content.empty()) return urls;

    std::wstringstream ss(content);
    std::wstring line;
    while (std::getline(ss, line)) {
        // 去除首尾空白
        size_t start = line.find_first_not_of(L" \t\r\n");
        if (start == std::wstring::npos) continue;
        size_t end = line.find_last_not_of(L" \t\r\n");
        line = line.substr(start, end - start + 1);
        if (line.empty()) continue;
        // 跳过注释行
        if (line[0] == L'#') continue;
        urls.push_back(line);
    }
    return urls;
}

// ---------- 从 URL 中提取版本号 ----------
static std::wstring ExtractVersionFromUrl(const std::wstring& url) {
    // 尝试匹配模式：/jdk/11.0.2/ 或 /openjdk/17.0.2/ 等
    std::wregex pattern(L"[/-](\\d+)(?:\\.\\d+)*[/_-]");
    std::wsmatch match;
    if (std::regex_search(url, match, pattern) && match.size() > 1) {
        return match[1].str();
    }
    // 如果匹配失败，尝试更宽松的匹配
    std::wregex pattern2(L"(\\d+)(?:\\.\\d+)*");
    if (std::regex_search(url, match, pattern2) && match.size() > 1) {
        return match[1].str();
    }
    return L"";
}

// ---------- 加载外部映射文件 ----------
void JdkDownloadService::loadExternalMappings() {
    std::wstring repoDir = GetRepoDir();

    // 加载 zip 映射
    std::wstring zipFile = JoinPath(repoDir, L"jdk_zip_repo.txt");
    auto zipUrls = ReadUrlListFromFile(zipFile);
    for (const auto& url : zipUrls) {
        std::wstring ver = ExtractVersionFromUrl(url);
        if (!ver.empty()) {
            zipMap_[ver].push_back(url);
        }
    }

    // 加载 exe 映射
    std::wstring exeFile = JoinPath(repoDir, L"jdk_exe_repo.txt");
    auto exeUrls = ReadUrlListFromFile(exeFile);
    for (const auto& url : exeUrls) {
        std::wstring ver = ExtractVersionFromUrl(url);
        if (!ver.empty()) {
            exeMap_[ver].push_back(url);
        }
    }
}

// ---------- 初始化内置映射 ----------
void JdkDownloadService::initBuiltinMappings() {
    // ---- ZIP 源 ----
    // 来自您的 jdk_zip_repo.txt
    zipMap_[L"11"].push_back(L"https://repo.huaweicloud.com/java/jdk/11+28/jdk-11_windows-x64_bin.zip");
    zipMap_[L"11"].push_back(L"https://repo.huaweicloud.com/java/jdk/11.0.1+13/jdk-11.0.1_windows-x64_bin.zip");
    zipMap_[L"11"].push_back(L"https://repo.huaweicloud.com/java/jdk/11.0.2+7/jdk-11.0.2_windows-x64_bin.zip");
    zipMap_[L"11"].push_back(L"https://mirrors.huaweicloud.com/openjdk/java-jse-ri/jdk11/openjdk-11+28_windows-x64_bin.zip");
    zipMap_[L"11"].push_back(L"https://mirrors.huaweicloud.com/openjdk/11.0.1/openjdk-11.0.1_windows-x64_bin.zip");
    zipMap_[L"11"].push_back(L"https://mirrors.huaweicloud.com/openjdk/11.0.2/openjdk-11.0.2_windows-x64_bin.zip");

    zipMap_[L"12"].push_back(L"https://repo.huaweicloud.com/java/jdk/12+33/jdk-12_windows-x64_bin.zip");
    zipMap_[L"12"].push_back(L"https://repo.huaweicloud.com/java/jdk/12.0.1+12/jdk-12.0.1_windows-x64_bin.zip");
    zipMap_[L"12"].push_back(L"https://repo.huaweicloud.com/java/jdk/12.0.2+10/jdk-12.0.2_windows-x64_bin.zip");
    zipMap_[L"12"].push_back(L"https://mirrors.huaweicloud.com/openjdk/java-jse-ri/jdk12/openjdk-12+32_windows-x64_bin.zip");
    zipMap_[L"12"].push_back(L"https://mirrors.huaweicloud.com/openjdk/12/openjdk-12_windows-x64_bin.zip");
    zipMap_[L"12"].push_back(L"https://mirrors.huaweicloud.com/openjdk/12.0.1/openjdk-12.0.1_windows-x64_bin.zip");
    zipMap_[L"12"].push_back(L"https://mirrors.huaweicloud.com/openjdk/12.0.2/openjdk-12.0.2_windows-x64_bin.zip");

    zipMap_[L"13"].push_back(L"https://repo.huaweicloud.com/java/jdk/13+33/jdk-13_windows-x64_bin.zip");
    zipMap_[L"13"].push_back(L"https://mirrors.huaweicloud.com/openjdk/13/openjdk-13_windows-x64_bin.zip");
    zipMap_[L"13"].push_back(L"https://mirrors.huaweicloud.com/openjdk/13.0.1/openjdk-13.0.1_windows-x64_bin.zip");
    zipMap_[L"13"].push_back(L"https://mirrors.huaweicloud.com/openjdk/13.0.2/openjdk-13.0.2_windows-x64_bin.zip");

    zipMap_[L"14"].push_back(L"https://mirrors.huaweicloud.com/openjdk/14/openjdk-14_windows-x64_bin.zip");
    zipMap_[L"14"].push_back(L"https://mirrors.huaweicloud.com/openjdk/14.0.1/openjdk-14.0.1_windows-x64_bin.zip");
    zipMap_[L"14"].push_back(L"https://mirrors.huaweicloud.com/openjdk/14.0.2/openjdk-14.0.2_windows-x64_bin.zip");

    zipMap_[L"15"].push_back(L"https://mirrors.huaweicloud.com/openjdk/15/openjdk-15_windows-x64_bin.zip");
    zipMap_[L"15"].push_back(L"https://mirrors.huaweicloud.com/openjdk/15.0.1/openjdk-15.0.1_windows-x64_bin.zip");
    zipMap_[L"15"].push_back(L"https://mirrors.huaweicloud.com/openjdk/15.0.2/openjdk-15.0.2_windows-x64_bin.zip");

    zipMap_[L"16"].push_back(L"https://mirrors.huaweicloud.com/openjdk/16/openjdk-16_windows-x64_bin.zip");
    zipMap_[L"16"].push_back(L"https://mirrors.huaweicloud.com/openjdk/16.0.1/openjdk-16.0.1_windows-x64_bin.zip");
    zipMap_[L"16"].push_back(L"https://mirrors.huaweicloud.com/openjdk/16.0.2/openjdk-16.0.2_windows-x64_bin.zip");

    zipMap_[L"17"].push_back(L"https://mirrors.huaweicloud.com/openjdk/17/openjdk-17_windows-x64_bin.zip");
    zipMap_[L"17"].push_back(L"https://mirrors.huaweicloud.com/openjdk/17.0.1/openjdk-17.0.1_windows-x64_bin.zip");
    zipMap_[L"17"].push_back(L"https://mirrors.huaweicloud.com/openjdk/17.0.2/openjdk-17.0.2_windows-x64_bin.zip");

    zipMap_[L"18"].push_back(L"https://mirrors.huaweicloud.com/openjdk/18/openjdk-18_windows-x64_bin.zip");
    zipMap_[L"18"].push_back(L"https://mirrors.huaweicloud.com/openjdk/18.0.1/openjdk-18.0.1_windows-x64_bin.zip");
    zipMap_[L"18"].push_back(L"https://mirrors.huaweicloud.com/openjdk/18.0.1.1/openjdk-18.0.1.1_windows-x64_bin.zip");
    zipMap_[L"18"].push_back(L"https://mirrors.huaweicloud.com/openjdk/18.0.2/openjdk-18.0.2_windows-x64_bin.zip");
    zipMap_[L"18"].push_back(L"https://mirrors.huaweicloud.com/openjdk/18.0.2.1/openjdk-18.0.2.1_windows-x64_bin.zip");

    zipMap_[L"19"].push_back(L"https://mirrors.huaweicloud.com/openjdk/19/openjdk-19_windows-x64_bin.zip");
    zipMap_[L"19"].push_back(L"https://mirrors.huaweicloud.com/openjdk/19.0.1/openjdk-19.0.1_windows-x64_bin.zip");
    zipMap_[L"19"].push_back(L"https://mirrors.huaweicloud.com/openjdk/19.0.2/openjdk-19.0.2_windows-x64_bin.zip");

    zipMap_[L"20"].push_back(L"https://mirrors.huaweicloud.com/openjdk/20/openjdk-20_windows-x64_bin.zip");
    zipMap_[L"20"].push_back(L"https://mirrors.huaweicloud.com/openjdk/20.0.1/openjdk-20.0.1_windows-x64_bin.zip");
    zipMap_[L"20"].push_back(L"https://mirrors.huaweicloud.com/openjdk/20.0.2/openjdk-20.0.2_windows-x64_bin.zip");

    zipMap_[L"21"].push_back(L"https://mirrors.huaweicloud.com/openjdk/21/openjdk-21_windows-x64_bin.zip");
    zipMap_[L"21"].push_back(L"https://mirrors.huaweicloud.com/openjdk/21.0.1/openjdk-21.0.1_windows-x64_bin.zip");
    zipMap_[L"21"].push_back(L"https://mirrors.huaweicloud.com/openjdk/21.0.2/openjdk-21.0.2_windows-x64_bin.zip");

    zipMap_[L"22"].push_back(L"https://mirrors.huaweicloud.com/openjdk/22/openjdk-22_windows-x64_bin.zip");
    zipMap_[L"22"].push_back(L"https://mirrors.huaweicloud.com/openjdk/22.0.1/openjdk-22.0.1_windows-x64_bin.zip");
    zipMap_[L"22"].push_back(L"https://mirrors.huaweicloud.com/openjdk/22.0.2/openjdk-22.0.2_windows-x64_bin.zip");

    zipMap_[L"23"].push_back(L"https://mirrors.huaweicloud.com/openjdk/23/openjdk-23_windows-x64_bin.zip");
    zipMap_[L"23"].push_back(L"https://mirrors.huaweicloud.com/openjdk/23.0.1/openjdk-23.0.1_windows-x64_bin.zip");
    zipMap_[L"23"].push_back(L"https://mirrors.huaweicloud.com/openjdk/23.0.2/openjdk-23.0.2_windows-x64_bin.zip");

    zipMap_[L"24"].push_back(L"https://mirrors.huaweicloud.com/openjdk/24/openjdk-24_windows-x64_bin.zip");
    zipMap_[L"24"].push_back(L"https://mirrors.huaweicloud.com/openjdk/24.0.1/openjdk-24.0.1_windows-x64_bin.zip");
    zipMap_[L"24"].push_back(L"https://mirrors.huaweicloud.com/openjdk/24.0.2/openjdk-24.0.2_windows-x64_bin.zip");

    zipMap_[L"25"].push_back(L"https://mirrors.huaweicloud.com/openjdk/25/openjdk-25_windows-x64_bin.zip");
    zipMap_[L"25"].push_back(L"https://mirrors.huaweicloud.com/openjdk/25.0.1/openjdk-25.0.1_windows-x64_bin.zip");
    zipMap_[L"25"].push_back(L"https://mirrors.huaweicloud.com/openjdk/25.0.2/openjdk-25.0.2_windows-x64_bin.zip");

    zipMap_[L"26"].push_back(L"https://mirrors.huaweicloud.com/openjdk/26/openjdk-26_windows-x64_bin.zip");
    zipMap_[L"26"].push_back(L"https://mirrors.huaweicloud.com/openjdk/26.0.1/openjdk-26.0.1_windows-x64_bin.zip");

    // ---- EXE 源 ----
    // 来自您的 jdk_exe_repo.txt
    exeMap_[L"6"].push_back(L"https://repo.huaweicloud.com/java/jdk/6u45-b06/jdk-6u45-windows-x64.exe");
    exeMap_[L"7"].push_back(L"https://repo.huaweicloud.com/java/jdk/7u80-b15/jdk-7u80-windows-x64.exe");
    exeMap_[L"8"].push_back(L"https://repo.huaweicloud.com/java/jdk/8u202-b08/jdk-8u202-windows-x64.exe");
    exeMap_[L"9"].push_back(L"https://repo.huaweicloud.com/java/jdk/9.0.1+11/jdk-9.0.1_windows-x64_bin.exe");
    exeMap_[L"10"].push_back(L"https://repo.huaweicloud.com/java/jdk/10.0.2+13/jdk-10.0.2_windows-x64_bin.exe");
    exeMap_[L"11"].push_back(L"https://repo.huaweicloud.com/java/jdk/11.0.2+9/jdk-11.0.2_windows-x64_bin.exe");
    exeMap_[L"12"].push_back(L"https://repo.huaweicloud.com/java/jdk/12.0.2+10/jdk-12.0.2_windows-x64_bin.exe");
    exeMap_[L"13"].push_back(L"https://repo.huaweicloud.com/java/jdk/13+33/jdk-13_windows-x64_bin.exe");
}

// ---------- 判断是否为 Demo 包 ----------
bool JdkDownloadService::isDemoPackage(const std::wstring& url) {
    std::wstring lowerUrl = url;
    std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::towlower);
    return lowerUrl.find(L"-demos") != std::wstring::npos;
}

// ---------- 查找 zip URL ----------
std::wstring JdkDownloadService::findZipUrl(const std::wstring& version) {
    auto it = zipMap_.find(version);
    if (it != zipMap_.end() && !it->second.empty()) {
        // 优先返回外部文件中的 URL（但当前我们已经合并了，所以直接返回第一个）
        return it->second[0];
    }
    return L"";
}

// ---------- 查找 exe URL ----------
std::wstring JdkDownloadService::findExeUrl(const std::wstring& version) {
    auto it = exeMap_.find(version);
    if (it != exeMap_.end() && !it->second.empty()) {
        return it->second[0];
    }
    return L"";
}

// ---------- 重新加载映射 ----------
void JdkDownloadService::reloadMappings() {
    zipMap_.clear();
    exeMap_.clear();

    // 1. 加载内置映射
    initBuiltinMappings();

    // 2. 确保外部文件存在（若不存在则从内置生成）
    ensureExternalMappingFiles();

    // 3. 加载外部映射（追加到内置之后）
    loadExternalMappings();
}

// ---------- 获取安装根目录 ----------
static std::wstring GetInstallRoot() {
    std::vector<wchar_t> drives = { L'D', L'E', L'F', L'G', L'H', L'I', L'J', L'K', L'L', L'M',
                                    L'N', L'O', L'P', L'Q', L'R', L'S', L'T', L'U', L'V', L'W', L'X', L'Y', L'Z' };
    for (wchar_t drive : drives) {
        std::wstring path = std::wstring(1, drive) + L":\\Program Files\\Java";
        if (IsDirectory(path) || CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
            return path;
        }
    }
    std::wstring fallback = L"C:\\Program Files\\Java";
    CreateDirectoryW(fallback.c_str(), nullptr);
    return fallback;
}

// ---------- 获取官方下载信息 ----------
static bool GetOfficialDownloadInfo(const std::wstring& version, std::wstring& outFinalUrl, std::wstring& outFileName) {
    std::wstring apiUrl = L"https://api.adoptium.net/v3/binary/latest/" + version +
                          L"/ga/windows/x64/jdk/hotspot/normal/eclipse";

    HINTERNET hSession = WinHttpOpen(L"JMT/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    // 解析 URL
    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {0};
    wchar_t urlPath[1024] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(apiUrl.c_str(), 0, 0, &urlComp)) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 禁用自动重定向，手动处理
    DWORD disableRedirects = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DISABLE_FEATURE, &disableRedirects, sizeof(disableRedirects));

    // 设置超时（可选）
    DWORD timeout = 30000; // 30秒
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 检查状态码
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (statusCode < 300 || statusCode >= 400) {
        PrintDebug(L"GetOfficialDownloadInfo: 状态码非重定向 " + std::to_wstring(statusCode));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 获取 Location 头
    wchar_t location[2048] = {0};
    size = sizeof(location);
    if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION,
                             WINHTTP_HEADER_NAME_BY_INDEX, location, &size, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring url = location;
    url.erase(std::remove_if(url.begin(), url.end(),
                             [](wchar_t ch) { return ch <= 0x20; }), url.end());

    if (url.empty()) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 提取文件名
    size_t pos = url.find_last_of(L'/');
    if (pos != std::wstring::npos) {
        outFileName = url.substr(pos + 1);
        size_t qpos = outFileName.find(L'?');
        if (qpos != std::wstring::npos) outFileName = outFileName.substr(0, qpos);
    } else {
        outFileName = L"";
    }

    outFinalUrl = url;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return true;
}

// ============================================================
// ========== 公共接口（使用新的映射逻辑） ====================
// ============================================================

std::wstring JdkDownloadService::downloadAndInstall(const std::wstring& version,
                                                    const std::wstring& installRoot) {
    std::wstring root = installRoot.empty() ? GetInstallRoot() : installRoot;
    std::wstring targetDir = JoinPath(root, L"jdk-" + version);

    if (IsDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
        PrintInfo(L"JDK " + version + L" 已安装在 " + targetDir);
        return targetDir;
    }

    // ---- 1. 查找 ZIP 源 ----
    std::wstring zipUrl = findZipUrl(version);
    if (!zipUrl.empty()) {
        std::wstring tempFile = GetTempDownloadPath(L"zip");
        PrintInfo(L"找到 ZIP 源: " + zipUrl);

        if (isDemoPackage(zipUrl)) {
            PrintWarning(L"⚠️ 检测到当前版本为 DEMO 包，仅包含示例代码和演示功能");
            PrintInfo(L"如需完整 JDK，请尝试其他源或使用官方下载");
        }

        PrintInfo(L"正在下载 JDK " + version + L"（ZIP 格式）...");
        if (DownloadFile(zipUrl, tempFile)) {
            if (ExtractZip(tempFile, targetDir)) {
                DeleteFileW(tempFile.c_str());

                // 先尝试修复嵌套目录
                if (JdkScanService::isValidJdk(targetDir)) {
                    PrintSuccess(L"JDK " + version + L" 安装成功");
                    return targetDir;
                } else if (FixNestedJdkDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
                    PrintSuccess(L"JDK " + version + L" 安装成功（修复嵌套结构）");
                    return targetDir;
                } else {
                    PrintWarning(L"解压后不是有效的 JDK，尝试其他源...");
                    fs::remove_all(targetDir);
                }
            } else {
                PrintWarning(L"解压失败，尝试其他源...");
                DeleteFileW(tempFile.c_str());
            }
        } else {
            PrintWarning(L"ZIP 源下载失败，尝试其他源...");
        }
    }

    // ---- 2. 查找 EXE 源 ----
    std::wstring exeUrl = findExeUrl(version);
    if (!exeUrl.empty()) {
        std::wstring fileName = ExtractFileNameFromUrl(exeUrl);
        if (fileName.empty()) fileName = L"jmt_download.exe";
        std::wstring tempFile = JoinPath(GetTempDir(), fileName);
        PrintInfo(L"找到 EXE 源: " + exeUrl);
        PrintWarning(L"未找到可自动安装的 ZIP 包，将下载 EXE 安装程序到 .temp 目录");
        PrintInfo(L"正在下载 EXE 安装程序...");
        if (DownloadFile(exeUrl, tempFile)) {
            PrintInfo(L"EXE 文件已保存到: " + tempFile);
            PrintWarning(L"请手动运行此 EXE 安装 JDK " + version + L"，然后运行 'jmt search' 刷新缓存");
            PrintInfo(L"建议安装路径: " + targetDir);
            return L"EXE_DOWNLOADED";
        } else {
            PrintWarning(L"EXE 源下载失败，尝试官方源...");
        }
    }

    // ---- 3. 回退到官方源（Adoptium） ----
    std::wstring officialUrl, fileName;
    if (!GetOfficialDownloadInfo(version, officialUrl, fileName) || fileName.empty()) {
        PrintError(L"无法获取官方下载信息，请检查网络或版本号是否正确");
        return L"";
    }
    std::wstring tempFile = GetTempDownloadPath(L"zip");
    PrintInfo(L"正在从官方源下载 JDK " + version + L" ...");
    if (!DownloadFile(officialUrl, tempFile)) {
        PrintError(L"官方源下载失败");
        DeleteFileW(tempFile.c_str());
        return L"";
    }
    if (!ExtractZip(tempFile, targetDir)) {
        PrintError(L"解压失败");
        DeleteFileW(tempFile.c_str());
        return L"";
    }
    DeleteFileW(tempFile.c_str());

    // 同样处理官方源下载的 ZIP 嵌套问题
    if (JdkScanService::isValidJdk(targetDir)) {
        PrintSuccess(L"JDK " + version + L" 安装成功（官方源）");
        return targetDir;
    } else if (FixNestedJdkDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
        PrintSuccess(L"JDK " + version + L" 安装成功（官方源，修复嵌套）");
        return targetDir;
    } else {
        PrintError(L"官方源解压后仍不是有效的 JDK，请手动处理");
        fs::remove_all(targetDir);
        return L"";
    }
}

std::wstring JdkDownloadService::downloadFromMirror(const std::wstring& version,
                                                    const std::wstring& installRoot) {
    std::wstring root = installRoot.empty() ? GetInstallRoot() : installRoot;
    std::wstring targetDir = JoinPath(root, L"jdk-" + version);

    if (IsDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
        PrintInfo(L"JDK " + version + L" 已安装在 " + targetDir);
        return targetDir;
    }

    // 优先 ZIP
    std::wstring zipUrl = findZipUrl(version);
    if (!zipUrl.empty()) {
        std::wstring tempFile = GetTempDownloadPath(L"zip");
        if (isDemoPackage(zipUrl)) {
            PrintWarning(L"⚠️ 此版本为 DEMO 包，仅包含示例代码");
        }
        PrintInfo(L"正在从镜像下载 ZIP...");
        if (DownloadFile(zipUrl, tempFile) && ExtractZip(tempFile, targetDir)) {
            DeleteFileW(tempFile.c_str());
            if (JdkScanService::isValidJdk(targetDir)) {
                PrintSuccess(L"JDK " + version + L" 安装成功（镜像）");
                return targetDir;
            } else if (FixNestedJdkDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
                PrintSuccess(L"JDK " + version + L" 安装成功（镜像，修复嵌套）");
                return targetDir;
            }
        }
        DeleteFileW(tempFile.c_str());
    }

    // 回退 EXE
    std::wstring exeUrl = findExeUrl(version);
    if (!exeUrl.empty()) {
        std::wstring fileName = ExtractFileNameFromUrl(exeUrl);
        if (fileName.empty()) fileName = L"jmt_download.exe";
        std::wstring tempFile = JoinPath(GetTempDir(), fileName);
        PrintInfo(L"未找到 ZIP，下载 EXE 到 .temp...");
        if (DownloadFile(exeUrl, tempFile)) {
            PrintInfo(L"EXE 已保存到: " + tempFile);
            PrintWarning(L"请手动安装并运行 'jmt search'");
            return L"EXE_DOWNLOADED";
        }
    }

    PrintError(L"镜像源中未找到版本 " + version + L" 的可用资源");
    return L"";
}

void JdkDownloadService::ensureExternalMappingFiles() {
    std::wstring repoDir = GetRepoDir();   // 返回 exeDir/.repo

    // 创建 .repo 目录（如果不存在）
    if (!IsDirectory(repoDir)) {
        if (!CreateDirectoryW(repoDir.c_str(), nullptr)) {
            PrintDebug(L"无法创建 .repo 目录: " + repoDir);
            return;
        }
    }

    // 检测文件是否为 UTF-16 LE 编码（老版本遗留问题），是则删除重建
    auto checkAndFixEncoding = [](const std::wstring& path) {
        if (!IsFile(path)) return;
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return;
        BYTE bom[2] = {0};
        DWORD read = 0;
        if (ReadFile(hFile, bom, 2, &read, nullptr) && read == 2) {
            if (bom[0] == 0xFF && bom[1] == 0xFE) {
                CloseHandle(hFile);
                hFile = INVALID_HANDLE_VALUE;
                DeleteFileW(path.c_str());
                PrintDebug(L"检测到 UTF-16 LE 编码，已删除并准备重建: " + path);
                return;
            }
        }
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    };

    // ---------- 处理 ZIP 映射文件 ----------
    std::wstring zipFilePath = JoinPath(repoDir, L"jdk_zip_repo.txt");
    checkAndFixEncoding(zipFilePath);
    if (!IsFile(zipFilePath)) {
        std::wstring content;
        content += L"# JDK ZIP mirror list\n";
        content += L"# One URL per line. Lines starting with # are comments.\n";
        content += L"# Version number is auto-extracted from URL (major version).\n";
        content += L"# Example: https://mirrors.huaweicloud.com/openjdk/17/openjdk-17_windows-x64_bin.zip\n";
        content += L"#          -> recognized as version 17\n\n";

        for (const auto& [ver, urls] : zipMap_) {
            for (const auto& url : urls) {
                content += url + L"\n";
            }
        }

        if (!WriteFileText(zipFilePath, content)) {
            PrintDebug(L"写入 ZIP 映射文件失败: " + zipFilePath);
        } else {
            PrintDebug(L"已生成 ZIP 映射文件: " + zipFilePath);
        }
    }

    // ---------- 处理 EXE 映射文件 ----------
    std::wstring exeFilePath = JoinPath(repoDir, L"jdk_exe_repo.txt");
    checkAndFixEncoding(exeFilePath);
    if (!IsFile(exeFilePath)) {
        std::wstring content;
        content += L"# JDK EXE installer list\n";
        content += L"# One URL per line. Lines starting with # are comments.\n";
        content += L"# Version number is auto-extracted from URL (major version).\n";
        content += L"# Example: https://repo.huaweicloud.com/java/jdk/8u202-b08/jdk-8u202-windows-x64.exe\n";
        content += L"#          -> recognized as version 8\n\n";

        for (const auto& [ver, urls] : exeMap_) {
            for (const auto& url : urls) {
                content += url + L"\n";
            }
        }

        if (!WriteFileText(exeFilePath, content)) {
            PrintDebug(L"写入 EXE 映射文件失败: " + exeFilePath);
        } else {
            PrintDebug(L"已生成 EXE 映射文件: " + exeFilePath);
        }
    }
}

std::wstring JdkDownloadService::downloadFromOfficial(const std::wstring& version,
                                                      const std::wstring& installRoot) {
    std::wstring root = installRoot.empty() ? GetInstallRoot() : installRoot;
    std::wstring targetDir = JoinPath(root, L"jdk-" + version);

    if (IsDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
        PrintInfo(L"JDK " + version + L" 已安装在 " + targetDir);
        return targetDir;
    }

    // 获取官方下载信息
    std::wstring officialUrl, fileName;
    if (!GetOfficialDownloadInfo(version, officialUrl, fileName) || fileName.empty()) {
        PrintError(L"无法获取官方下载信息，请检查网络或版本号是否正确");
        return L"";
    }

    // 下载到 .temp 目录（使用原始文件名或通用名）
    std::wstring tempFile = JoinPath(GetTempDir(), fileName);
    if (tempFile.empty() || tempFile.find(L".zip") == std::wstring::npos) {
        // 如果文件名不是 .zip，补充后缀
        tempFile = GetTempDownloadPath(L"zip");
    }

    PrintInfo(L"正在从官方源下载 JDK " + version + L" ...");
    if (!DownloadFile(officialUrl, tempFile)) {
        PrintError(L"官方源下载失败");
        DeleteFileW(tempFile.c_str());
        return L"";
    }

    if (!ExtractZip(tempFile, targetDir)) {
        PrintError(L"解压失败");
        DeleteFileW(tempFile.c_str());
        return L"";
    }
    DeleteFileW(tempFile.c_str());

    // 检查是否为有效 JDK，若不是则尝试修复嵌套目录
    if (JdkScanService::isValidJdk(targetDir)) {
        PrintSuccess(L"JDK " + version + L" 安装成功（官方源）");
        return targetDir;
    } else if (FixNestedJdkDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
        PrintSuccess(L"JDK " + version + L" 安装成功（官方源，修复嵌套）");
        return targetDir;
    } else {
        PrintError(L"官方源解压后不是有效的 JDK，请手动处理");
        fs::remove_all(targetDir);
        return L"";
    }
}

// ---------- 静态初始化（在 main 中调用） ----------
// 注意：需要在 main 或程序启动时调用一次 reloadMappings()
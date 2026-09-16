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
#include "common/cancel_token.hpp"
#include "network/curl_output.hpp"
#include "common/java_version.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "platform/output.hpp"
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



// ---------- 辅助函数：获取 .temp 目录（路径来自 AppPaths） ----------
std::wstring JdkDownloadService::tempDirectory() {
    if (!IsDirectory(paths_.tempDir)) {
        CreateDirectoryW(paths_.tempDir.c_str(), nullptr);
    }
    return paths_.tempDir;
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
bool JdkDownloadService::fixNestedJdkDirectory(const std::wstring& targetDir) {
    std::vector<fs::path> subDirs;
    try {
        for (const auto& entry : fs::directory_iterator(targetDir)) {
            if (fs::is_directory(entry.status())) {
                subDirs.push_back(entry.path());
            }
        }
    } catch (const std::exception& e) {
        out_.line(OutputLevel::Debug, L"FixNestedJdkDirectory: 遍历目录异常 " + ToWideString(e.what()));
        return false;
    }

    if (subDirs.size() != 1) {
        out_.line(OutputLevel::Debug, L"FixNestedJdkDirectory: 子目录数量不为1，实际=" + std::to_wstring(subDirs.size()));
        return false;
    }

    fs::path subPath = subDirs[0];
    if (!JdkScanService::isValidJdk(subPath.wstring())) {
        out_.line(OutputLevel::Debug, L"FixNestedJdkDirectory: 子目录不是有效的 JDK");
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
        out_.line(OutputLevel::Debug, L"FixNestedJdkDirectory: 移动成功");
        return true;
    } catch (const std::exception& e) {
        out_.line(OutputLevel::Debug, L"FixNestedJdkDirectory: 移动失败 " + ToWideString(e.what()));
        return false;
    }
}

// ---------- 辅助函数：生成临时文件路径（在 .temp 下，按 URL 唯一化） ----------
std::wstring JdkDownloadService::tempDownloadPath(const std::wstring& ext, const std::wstring& url) {
    // 用 URL 的哈希做后缀，避免多个源/多个实例复写同一个文件
    uint32_t hash = 2166136261u;
    for (wchar_t ch : url) {
        hash ^= static_cast<uint32_t>(ch);
        hash *= 16777619u;
    }
    wchar_t suffix[16];
    swprintf_s(suffix, L"%08x", hash);
    return JoinPath(tempDirectory(), L"jmt_download_" + std::wstring(suffix) + L"." + ext);
}

// 统一的 User-Agent：镜像方可以据此识别 JMT（而不是无名的 curl）
static const wchar_t* kJmtUserAgent = L"JMT/1.7 (Windows; +https://github.com/sczerofox/JMT)";

// ---------- 辅助函数：使用 curl.exe 下载 ----------
bool JdkDownloadService::downloadFileWithCurl(const std::wstring& url, const std::wstring& destPath,
                                              int& outStatus, int64_t& outBytes, int& outSpeedBps) {
    outStatus = 0;
    outBytes = 0;
    outSpeedBps = 0;
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

    // 镜像友好：连接/总超时、标识 UA、重试降到 2 次。
    // 进度用 --progress-bar 但输出被我们接管（见下方循环），因此既能看到进度也不会污染 JMT 的输出流。
    std::wstring cmdLine = L"\"" + std::wstring(curlPath) + L"\" -L -s -S --progress-bar"
                           L" --retry 2 --connect-timeout 15 --max-time 900"
                           L" -A \"" + kJmtUserAgent + L"\""
                           L" -o \"" + destPath + L"\" \"" + cleanUrl + L"\""
                           L" -w \"%{http_code} %{size_download} %{time_total} %{speed_download}\"";

    // 捕获子进程输出：既拿到 HTTP 状态码，也避免 curl 的进度/错误信息污染我们的输出流
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &attributes, 4096)) {
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, (LPWSTR)cmdLine.c_str(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(readEnd);
        CloseHandle(writeEnd);
        return false;
    }
    CloseHandle(writeEnd);

    std::string captured;
    char buffer[512];
    DWORD read = 0;
    int lastPercent = -1;
    int lastMilestone = 0;
    int maxPercent = -1;
    const int64_t startedAt = static_cast<int64_t>(GetTickCount64());

    // 边等边读：把 curl 的进度条解析成 JMT 自己的进度显示；期间响应 Ctrl+C
    while (WaitForSingleObject(pi.hProcess, 300) == WAIT_TIMEOUT) {
        if (globalCancelState().cancelled()) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
            break;
        }
        DWORD available = 0;
        while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            if (available > sizeof(buffer)) available = sizeof(buffer);
            if (!ReadFile(readEnd, buffer, available, &read, nullptr) || read == 0) break;
            captured.append(buffer, read);
        }
        const int percent = curl_output::parsePercent(captured);
        if (percent >= 0 && percent != lastPercent) {
            lastPercent = percent;
            if (percent > maxPercent) maxPercent = percent;
            const int elapsedMs = static_cast<int>(GetTickCount64() - startedAt);
            out_.progress(L"下载中: " + std::to_wstring(percent) + L"%  （已用时 " +
                          std::to_wstring(elapsedMs / 1000) + L" 秒）");
            // 里程碑行：普通输出，重定向到文件时同样可见
            if (const int milestone = curl_output::milestoneCrossed(lastMilestone, percent); milestone > 0) {
                lastMilestone = milestone;
                out_.line(OutputLevel::Info, L"下载进度: " + std::to_wstring(milestone) + L"%（已用时 " +
                                             std::to_wstring(elapsedMs / 1000) + L" 秒）");
            }
        } else if (percent < 0 && lastPercent < 0) {
            // 还没拿到百分比（连接阶段/服务端不报总长）：每 5 秒给一次心跳，避免看起来像卡住
            const int elapsedMs = static_cast<int>(GetTickCount64() - startedAt);
            if (elapsedMs % 5000 < 300) {
                out_.progress(L"正在连接/下载... 已用时 " + std::to_wstring(elapsedMs / 1000) + L" 秒");
                out_.line(OutputLevel::Info, L"正在连接/下载... 已用时 " +
                                             std::to_wstring(elapsedMs / 1000) + L" 秒");
            }
        }
    }

    // 收尾把剩余输出读完
    while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        captured.append(buffer, read);
    }
    out_.clearProgress();

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(readEnd);

    if (globalCancelState().cancelled()) {
        DeleteFileW(destPath.c_str());
        out_.line(OutputLevel::Warning, L"下载已取消");
        return false;
    }

    // -w 的收尾统计放在最后一行：状态码 / 字节数 / 用时 / 速度
    const curl_output::Stats stats = curl_output::parseStats(captured);
    if (stats.parsed) {
        outStatus = stats.httpStatus;
        outBytes = stats.bytes;
        outSpeedBps = stats.speedBps;
    }

    if (!captured.empty() && stats.parsed) {
        // Debug 级别：完整输出（含 curl 的进度条残迹）只在 Debug 构建可见
        out_.line(OutputLevel::Debug, L"curl: HTTP " + std::to_wstring(stats.httpStatus) + L"，" +
                                       ToWideString(curl_output::formatBytes(stats.bytes)) + L"，用时 " +
                                       std::to_wstring(stats.elapsedMs / 1000) + L" 秒，" +
                                       ToWideString(curl_output::formatSpeed(stats.speedBps)) +
                                       L"，进度峰值 " + (maxPercent >= 0 ? std::to_wstring(maxPercent) + L"%" : L"无"));
    }

    if (exitCode != 0) {
        DeleteFileW(destPath.c_str());
        return false;
    }

    return IsFile(destPath);
}

// ---------- 辅助函数：多线程下载 ----------
bool JdkDownloadService::downloadFileWithMultiThread(const std::wstring& url, const std::wstring& destPath,
                                                     int& outStatus, int64_t& outBytes, int& outSpeedBps) {
    outStatus = 0;   // WinHTTP 路径不解析 HTTP 状态码，按传输层失败处理
    outBytes = 0;
    outSpeedBps = 0;
    const int64_t startedAt = static_cast<int64_t>(GetTickCount64());
    std::wstring cleanUrl = url;
    cleanUrl.erase(std::remove_if(cleanUrl.begin(), cleanUrl.end(),
                                  [](wchar_t ch) { return ch <= 0x20; }), cleanUrl.end());

    MultiThreadDownloader downloader;
    // 并发上限跟节流策略保持一致（默认 2）：单主机不会出现 4~32 条并发连接
    downloader.setConnections(throttle_.policy().maxConcurrentPerHost);
    downloader.setTimeout(60);
    downloader.setRetryCount(2);

    bool success = false;
    std::wstring errorMsg;

    out_.line(OutputLevel::Info, L"使用多线程加速下载...");
    success = downloader.download(cleanUrl, destPath,
                                  [this](const DownloadProgress& prog) {
                                      if (prog.percent >= 0) {
                                          out_.progress(L"下载进度: " + std::to_wstring(prog.percent) + L"%");
                                      }
                                  },
                                  [&](const std::wstring& err) {
                                      errorMsg = err;
                                  }
    );

    if (!success) {
        out_.line(OutputLevel::Error, L"多线程下载失败: " + errorMsg);
        DeleteFileW(destPath.c_str());
        return false;
    }

    if (!IsFile(destPath)) {
        out_.line(OutputLevel::Error, L"下载文件不存在");
        return false;
    }

    try {
        auto size = fs::file_size(destPath);
        if (size < 1024 * 1024) {
            out_.line(OutputLevel::Warning, L"文件大小异常（" + std::to_wstring(size / 1024) + L" KB），可能不是有效的 JDK 安装包");
            DeleteFileW(destPath.c_str());
            return false;
        }
        std::wstring ext = destPath.substr(destPath.find_last_of(L'.') + 1);
        if (ext == L"zip" && !isValidZipFile(destPath)) {
            out_.line(OutputLevel::Warning, L"下载的文件不是有效的 ZIP 文件");
            DeleteFileW(destPath.c_str());
            return false;
        }
        out_.line(OutputLevel::Info, L"下载完成，共 " + std::to_wstring(size / 1024) + L" KB");
        outBytes = static_cast<int64_t>(size);
        const int64_t elapsedMs = static_cast<int64_t>(GetTickCount64()) - startedAt;
        if (elapsedMs > 0) {
            outSpeedBps = static_cast<int>(outBytes * 1000 / elapsedMs);
        }
        return true;
    } catch (const std::exception&) {
        out_.line(OutputLevel::Error, L"获取文件大小失败");
        DeleteFileW(destPath.c_str());
        return false;
    }
}

// ---------- 统一的下载入口 ----------
bool JdkDownloadService::downloadFile(const std::wstring& url, const std::wstring& destPath,
                                      int& outStatus, int64_t& outBytes, int& outSpeedBps) {
    outBytes = 0;
    outSpeedBps = 0;
    // 镜像友好：所有网络请求都必须先通过节流器（并发/间隔/预算/拉黑）
    std::wstring denyReason;
    if (!throttle_.acquire(url, denyReason)) {
        out_.line(OutputLevel::Warning, L"跳过该源: " + denyReason);
        outStatus = 0;
        return false;
    }

    bool success = downloadFileWithCurl(url, destPath, outStatus, outBytes, outSpeedBps);
    if (!success) {
        // 限速/拒绝类信号（429/503/403）不再换引擎重试，避免叠加请求
        if (outStatus == 429 || outStatus == 503 || outStatus == 403) {
            throttle_.noteResult(url, outStatus, false);
            throttle_.release(url);
            out_.line(OutputLevel::Warning, L"该源返回 HTTP " + std::to_wstring(outStatus) +
                                            L"（限速/拒绝），停止向该主机继续请求");
            return false;
        }
        out_.line(OutputLevel::Warning, L"curl 下载失败，尝试多线程下载...");
        success = downloadFileWithMultiThread(url, destPath, outStatus, outBytes, outSpeedBps);
    }

    throttle_.noteResult(url, outStatus, success);
    throttle_.release(url);
    return success;
}

// ---------- 解压 ZIP ----------
bool JdkDownloadService::extractZip(const std::wstring& zipPath, const std::wstring& destDir) {
    // 确保目标目录存在
    CreateDirectoryW(destDir.c_str(), nullptr);

    std::wstring cmd = L"powershell -Command \"Expand-Archive -Path '" + zipPath + L"' -DestinationPath '" + destDir + L"' -Force; if ($?) { exit 0 } else { exit 1 }\"";
    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(nullptr, (LPWSTR)cmd.c_str(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        out_.line(OutputLevel::Debug, L"ExtractZip: CreateProcess 失败");
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
        out_.line(OutputLevel::Debug, L"ExtractZip: PowerShell 退出码 " + std::to_wstring(exitCode));
        return false;
    }
    return true;
}

// ---------- 校验 ZIP 文件头 ----------
bool JdkDownloadService::isValidZipFile(const std::wstring& path) {
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
std::wstring JdkDownloadService::repoDirectory() {
    if (!IsDirectory(paths_.repoDir)) {
        CreateDirectoryW(paths_.repoDir.c_str(), nullptr);
    }
    return paths_.repoDir;
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
    std::wstring repoDir = repoDirectory();

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

// ---------- 源列表（内置在前、外部追加；空列表表示该版本无此类型源） ----------
const JdkDownloadService::UrlList& JdkDownloadService::zipUrlsFor(const std::wstring& version) {
    static const UrlList kEmpty;
    const auto it = zipMap_.find(version);
    return it == zipMap_.end() ? kEmpty : it->second;
}

const JdkDownloadService::UrlList& JdkDownloadService::exeUrlsFor(const std::wstring& version) {
    static const UrlList kEmpty;
    const auto it = exeMap_.find(version);
    return it == exeMap_.end() ? kEmpty : it->second;
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
bool JdkDownloadService::officialDownloadInfo(const std::wstring& version, std::wstring& outFinalUrl, std::wstring& outFileName) {
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
        out_.line(OutputLevel::Debug, L"GetOfficialDownloadInfo: 状态码非重定向 " + std::to_wstring(statusCode));
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
    return executePlan(DownloadPlan::build(DownloadMode::Default, false,
                                           filterUrlsForVersion(zipUrlsFor(version), version),
                                           filterUrlsForVersion(exeUrlsFor(version), version)),
                       version, installRoot);
}

std::wstring JdkDownloadService::downloadFromMirror(const std::wstring& version,
                                                    const std::wstring& installRoot) {
    return executePlan(DownloadPlan::build(DownloadMode::MirrorOnly, false,
                                           filterUrlsForVersion(zipUrlsFor(version), version),
                                           filterUrlsForVersion(exeUrlsFor(version), version)),
                       version, installRoot);
}

void JdkDownloadService::ensureExternalMappingFiles() {
    std::wstring repoDir = repoDirectory();   // 返回 exeDir/.repo

    // 创建 .repo 目录（如果不存在）
    if (!IsDirectory(repoDir)) {
        if (!CreateDirectoryW(repoDir.c_str(), nullptr)) {
            out_.line(OutputLevel::Debug, L"无法创建 .repo 目录: " + repoDir);
            return;
        }
    }

    // 检测文件是否为 UTF-16 LE 编码（老版本遗留问题），是则删除重建
    auto checkAndFixEncoding = [this](const std::wstring& path) {
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
                out_.line(OutputLevel::Debug, L"检测到 UTF-16 LE 编码，已删除并准备重建: " + path);
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
            out_.line(OutputLevel::Debug, L"写入 ZIP 映射文件失败: " + zipFilePath);
        } else {
            out_.line(OutputLevel::Debug, L"已生成 ZIP 映射文件: " + zipFilePath);
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
            out_.line(OutputLevel::Debug, L"写入 EXE 映射文件失败: " + exeFilePath);
        } else {
            out_.line(OutputLevel::Debug, L"已生成 EXE 映射文件: " + exeFilePath);
        }
    }
}

std::wstring JdkDownloadService::downloadFromOfficial(const std::wstring& version,
                                                      const std::wstring& installRoot) {
    return executePlan(DownloadPlan::build(DownloadMode::OfficialOnly, false, {}, {}),
                       version, installRoot);
}

std::wstring JdkDownloadService::downloadInstallerOnly(const std::wstring& version) {
    return executePlan(DownloadPlan::build(DownloadMode::Default, true, {},
                                           filterUrlsForVersion(exeUrlsFor(version), version)),
                       version, L"");
}

// ============================================================
// ========== 计划执行 =========================================
// ============================================================

std::wstring JdkDownloadService::executePlan(const DownloadPlan& plan,
                                             const std::wstring& version,
                                             const std::wstring& installRoot) {
    const std::wstring root = installRoot.empty() ? GetInstallRoot() : installRoot;
    const std::wstring targetDir = JoinPath(root, L"jdk-" + version);

    if (IsDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
        out_.line(OutputLevel::Info, L"JDK " + version + L" 已安装在 " + targetDir);
        return targetDir;
    }

    // demo 包直接跳过（此前只提示但仍然下载）
    for (const auto& demoUrl : plan.skippedDemoUrls) {
        out_.line(OutputLevel::Warning, L"已跳过 DEMO 包（仅含示例代码）: " + demoUrl);
    }

    if (plan.steps.empty()) {
        out_.line(OutputLevel::Error,
                  plan.installerOnly
                          ? L"版本 " + version + L" 没有可下载的 EXE 安装包"
                          : L"没有可用于版本 " + version + L" 的下载源");
        return L"";
    }

    for (size_t i = 0; i < plan.steps.size(); ++i) {
        if (globalCancelState().cancelled()) {
            out_.line(OutputLevel::Warning, L"下载已取消，停止尝试其余源");
            return L"";
        }
        const DownloadStep& step = plan.steps[i];
        const std::wstring order = L"（源 " + std::to_wstring(i + 1) + L"/" +
                                   std::to_wstring(plan.steps.size()) + L"）";

        // 镜像友好：被临时拉黑的主机直接跳过，不再产生任何请求
        if (!step.url.empty() && throttle_.isHostBlocked(HostThrottle::hostOf(step.url))) {
            out_.line(OutputLevel::Warning, L"跳过源" + order + L"：主机 " +
                                            HostThrottle::hostOf(step.url) + L" 因限速/拒绝访问被临时拉黑");
            continue;
        }

        switch (step.kind) {
            case DownloadStepKind::MirrorZip:
                out_.line(OutputLevel::Info, L"尝试 ZIP 源" + order);
                if (tryZipSource(step.url, version, targetDir)) {
                    if (versionSatisfied(targetDir, version)) {
                        return targetDir;
                    }
                }
                break;
            case DownloadStepKind::MirrorExe: {
                out_.line(OutputLevel::Info, L"尝试 EXE 源" + order);
                const std::wstring exeResult = tryExeSource(step.url, version, targetDir);
                if (exeResult == L"EXE_DOWNLOADED") {
                    return exeResult;
                }
                break;
            }
            case DownloadStepKind::OfficialZip:
                out_.line(OutputLevel::Info, L"尝试官方源" + order);
                if (tryOfficialZip(version, targetDir)) {
                    if (versionSatisfied(targetDir, version)) {
                        return targetDir;
                    }
                }
                break;
        }
    }

    out_.line(OutputLevel::Error, L"所有下载源均失败，版本 " + version + L" 未安装");
    out_.line(OutputLevel::Info, L"本次共发出 " + std::to_wstring(throttle_.totalRequests()) +
                                 L" 次请求（预算 " + std::to_wstring(throttle_.policy().maxRequestsTotal) +
                                 L" 次）；已按镜像友好策略限速与退避");
    const std::vector<std::wstring> blocked = throttle_.blockedHosts();
    if (!blocked.empty()) {
        std::wstring list;
        for (const auto& host : blocked) {
            if (!list.empty()) list += L", ";
            list += host;
        }
        out_.line(OutputLevel::Warning, L"以下主机已被临时拉黑（稍后自动恢复）: " + list);
    }
    return L"";
}

bool JdkDownloadService::tryZipSource(const std::wstring& url, const std::wstring& version,
                                      const std::wstring& targetDir) {
    const std::wstring tempFile = tempDownloadPath(L"zip", url);
    out_.line(OutputLevel::Info, L"正在下载 JDK " + version + L"（ZIP）: " + url);
    int httpStatus = 0;
    int64_t downloadedBytes = 0;
    int speedBps = 0;
    if (!downloadFile(url, tempFile, httpStatus, downloadedBytes, speedBps)) {
        out_.line(OutputLevel::Warning, L"该 ZIP 源下载失败（HTTP " + std::to_wstring(httpStatus) +
                                        L"），尝试下一个源...");
        return false;
    }
    out_.line(OutputLevel::Info, L"下载完成: " + ToWideString(curl_output::formatBytes(downloadedBytes)) +
                                 L"，平均 " + ToWideString(curl_output::formatSpeed(speedBps)));
    if (!extractZip(tempFile, targetDir)) {
        out_.line(OutputLevel::Warning, L"解压失败，尝试下一个源...");
        DeleteFileW(tempFile.c_str());
        return false;
    }
    DeleteFileW(tempFile.c_str());

    if (JdkScanService::isValidJdk(targetDir)) {
        out_.line(OutputLevel::Success, L"JDK " + version + L" 安装成功");
        return true;
    }
    if (fixNestedJdkDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
        out_.line(OutputLevel::Success, L"JDK " + version + L" 安装成功（修复嵌套结构）");
        return true;
    }
    out_.line(OutputLevel::Warning, L"解压后不是有效的 JDK，尝试下一个源...");
    fs::remove_all(targetDir);
    return false;
}

std::wstring JdkDownloadService::tryExeSource(const std::wstring& url, const std::wstring& version,
                                              const std::wstring& targetDir) {
    std::wstring fileName = ExtractFileNameFromUrl(url);
    if (fileName.empty()) fileName = L"jmt_download.exe";
    const std::wstring tempFile = JoinPath(tempDirectory(), fileName);

    out_.line(OutputLevel::Info, L"正在下载 EXE 安装程序: " + url);
    int httpStatus = 0;
    int64_t downloadedBytes = 0;
    int speedBps = 0;
    if (!downloadFile(url, tempFile, httpStatus, downloadedBytes, speedBps)) {
        out_.line(OutputLevel::Warning, L"该 EXE 源下载失败（HTTP " + std::to_wstring(httpStatus) +
                                        L"），尝试下一个源...");
        return L"";
    }
    out_.line(OutputLevel::Info, L"下载完成: " + ToWideString(curl_output::formatBytes(downloadedBytes)) +
                                 L"，平均 " + ToWideString(curl_output::formatSpeed(speedBps)));
    out_.line(OutputLevel::Info, L"EXE 文件已保存到: " + tempFile);
    out_.line(OutputLevel::Warning, L"请手动运行此 EXE 安装 JDK " + version + L"，然后运行 'jmt search' 刷新缓存");
    out_.line(OutputLevel::Info, L"建议安装路径: " + targetDir);
    return L"EXE_DOWNLOADED";
}

bool JdkDownloadService::tryOfficialZip(const std::wstring& version, const std::wstring& targetDir) {
    std::wstring officialUrl;
    std::wstring fileName;
    if (!officialDownloadInfo(version, officialUrl, fileName) || fileName.empty()) {
        out_.line(OutputLevel::Error, L"无法获取官方下载信息，请检查网络或版本号是否正确");
        return false;
    }

    std::wstring tempFile = JoinPath(tempDirectory(), fileName);
    if (tempFile.find(L".zip") == std::wstring::npos) {
        tempFile = tempDownloadPath(L"zip", officialUrl);
    }

    out_.line(OutputLevel::Info, L"正在从官方源下载 JDK " + version + L" ...");
    int httpStatus = 0;
    int64_t downloadedBytes = 0;
    int speedBps = 0;
    if (!downloadFile(officialUrl, tempFile, httpStatus, downloadedBytes, speedBps)) {
        out_.line(OutputLevel::Error, L"官方源下载失败（HTTP " + std::to_wstring(httpStatus) + L"）");
        DeleteFileW(tempFile.c_str());
        return false;
    }
    out_.line(OutputLevel::Info, L"下载完成: " + ToWideString(curl_output::formatBytes(downloadedBytes)) +
                                 L"，平均 " + ToWideString(curl_output::formatSpeed(speedBps)));
    if (!extractZip(tempFile, targetDir)) {
        out_.line(OutputLevel::Error, L"解压失败");
        DeleteFileW(tempFile.c_str());
        return false;
    }
    DeleteFileW(tempFile.c_str());

    if (JdkScanService::isValidJdk(targetDir)) {
        out_.line(OutputLevel::Success, L"JDK " + version + L" 安装成功（官方源）");
        return true;
    }
    if (fixNestedJdkDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
        out_.line(OutputLevel::Success, L"JDK " + version + L" 安装成功（官方源，修复嵌套）");
        return true;
    }
    out_.line(OutputLevel::Error, L"官方源解压后仍不是有效的 JDK，请手动处理");
    fs::remove_all(targetDir);
    return false;
}

// ---------- 静态初始化（在 main 中调用） ----------
// 注意：需要在 main 或程序启动时调用一次 reloadMappings()

bool JdkDownloadService::versionSatisfied(const std::wstring& targetDir, const std::wstring& requested) {
    if (!JavaVersion::isFullVersionQuery(requested)) {
        return true;   // 只给主版本时不校验具体补丁版本
    }
    const std::wstring installed = JdkScanService::extractVersion(targetDir);
    if (installed.empty()) {
        return true;   // 读不到版本信息（缺少 release）时不阻断安装
    }
    if (JavaVersion::parse(installed).matches(requested)) {
        return true;
    }
    out_.line(OutputLevel::Warning,
              L"该源提供的是 " + installed + L"，与请求的 " + requested + L" 不一致，放弃该源");
    fs::remove_all(targetDir);
    return false;
}

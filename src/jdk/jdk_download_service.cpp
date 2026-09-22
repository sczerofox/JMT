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
#include "jdk/download_sources.hpp"
#include "common/cancel_token.hpp"
#include "common/version.hpp"
#include "network/curl_output.hpp"
#include "common/java_version.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "platform/output.hpp"
#include "system/utils.hpp"
#include "network/multi_thread_downloader.hpp"
#include <wininet.h>
#include <vector>
#include <string>
#include <iostream>
#include <filesystem>
#include <algorithm>

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

// 统一的 User-Agent：镜像方可以据此识别 JMT（而不是无名的 curl）。
// 版本号取自 version.hpp，避免升级时漏改这里。
static std::wstring jmtUserAgent() {
    static const std::wstring agent =
            L"JMT/" + std::wstring(jmt::kVersionNumber) +
            L" (Windows; +https://github.com/sczerofox/JMT)";
    return agent;
}

// 读取正在下载的目标文件大小（拿不到时返回 0）；用于「已下载 X MB」显示
static int64_t downloadedSizeOf(const std::wstring& path) {
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size{};
    const bool ok = GetFileSizeEx(handle, &size) != 0;
    CloseHandle(handle);
    return ok ? size.QuadPart : 0;
}

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
    // 注意：这里不能用 -s（silent 会连进度条一起关掉）；curl 的输出被我们接管到管道，
    // 因此既拿得到进度，也不会污染 JMT 的输出流。
    std::wstring cmdLine = L"\"" + std::wstring(curlPath) + L"\" -L --progress-bar"
                           L" --retry 2 --connect-timeout 15 --max-time 900"
                           L" -A \"" + jmtUserAgent() + L"\""
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
    int nextHeartbeatSec = 5;
    const int64_t startedAt = static_cast<int64_t>(GetTickCount64());
    // 只查询一次输出能力，避免在每次进度回调里重复走虚函数
    const bool liveProgress = out_.supportsProgress();

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
        const int64_t downloadedBytes = downloadedSizeOf(destPath);
        const int elapsedSec = static_cast<int>((GetTickCount64() - startedAt) / 1000);
        const std::wstring downloadedText = ToWideString(curl_output::formatBytes(downloadedBytes));

        if (percent >= 0) {
            // 服务端给了总长度：控制台上原地刷新即可；只有输出不支持原地刷新（重定向/管道）时，
            // 才每跨过 25% 或每 10 秒补一行普通输出，避免控制台上出现重复的两行
            if (percent != lastPercent) {
                lastPercent = percent;
                if (percent > maxPercent) maxPercent = percent;
                out_.progress(L"下载中: " + std::to_wstring(percent) + L"%    已下载 " + downloadedText +
                              L"    已用时 " + std::to_wstring(elapsedSec) + L" 秒");
            }
            const int milestone = curl_output::milestoneCrossed(lastMilestone, percent);
            const bool periodic = (elapsedSec >= nextHeartbeatSec);
            if (!liveProgress && (milestone > 0 || periodic)) {
                if (milestone > 0) {
                    lastMilestone = milestone;
                }
                if (periodic) {
                    nextHeartbeatSec = elapsedSec + 10;
                }
                out_.line(OutputLevel::Info, L"下载中: " + std::to_wstring(percent) + L"%    已下载 " +
                                             downloadedText + L"    已用时 " +
                                             std::to_wstring(elapsedSec) + L" 秒");
            }
        } else if (elapsedSec >= nextHeartbeatSec) {
            // 拿不到百分比（连接中/服务端不报总长）：每 5 秒一行，避免看起来像卡住
            nextHeartbeatSec = elapsedSec + 5;
            const std::wstring phase = (downloadedBytes > 0) ? L"正在下载..." : L"正在连接...";
            if (liveProgress) {
                out_.progress(phase + L" 已用时 " + std::to_wstring(elapsedSec) +
                              L" 秒    已下载 " + downloadedText);
            } else {
                out_.line(OutputLevel::Info, phase + L" 已用时 " + std::to_wstring(elapsedSec) +
                                             L" 秒    已下载 " + downloadedText);
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
    const curl_output::Stats stats = curl_output::parseStatsFromTail(captured);
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
// 取 needle 之后第一个 delim 包裹的值：extractDelimited(json, 4, L'"', L'"')
static std::wstring extractDelimited(const std::wstring& text, size_t from,
                                     wchar_t open, wchar_t close) {
    const size_t start = text.find(open, from);
    if (start == std::wstring::npos) return L"";
    const size_t end = text.find(close, start + 1);
    if (end == std::wstring::npos) return L"";
    return text.substr(start + 1, end - start - 1);
}

// 在 Adoptium assets JSON 里找想要的产物文件名（优先 zip，其次 msi），并顺带取出它的 link。
// JSON 里每个 package 形如 {"link":"...","name":"OpenJDK17U-jdk_x64_windows_hotspot_17.0.20.1_1.zip",...}，
// 所以按 "name" 定位以兼容字段顺序变化，再向回找同一条目里的 link。
static bool findFileNameInJson(const std::wstring& json, const std::wstring& version,
                               std::wstring& outName, std::wstring& outLink) {
    for (const wchar_t* ext : {L".zip", L".msi"}) {
        for (size_t pos = json.find(L"\"name\""); pos != std::wstring::npos;
             pos = json.find(L"\"name\"", pos + 1)) {
            const std::wstring name = extractDelimited(json, pos + 6, L'"', L'"');
            if (name.empty() || name.find(ext) == std::wstring::npos) continue;
            // 请求了完整版本（17.0.2）时，优先挑文件名里带该版本串的产物
            if (!version.empty() && name.find(version) == std::wstring::npos) continue;
            // 同一条目里的 link 出现在 name 之前，向回搜索
            const size_t linkPos = json.rfind(L"\"link\"", pos);
            if (linkPos == std::wstring::npos) continue;
            outLink = extractDelimited(json, linkPos + 6, L'"', L'"');
            outName = name;
            return true;
        }
    }
    return false;
}

// 向 Adoptium 官方 API 问这个版本的产物信息（权威文件名 + 官方直链）
bool JdkDownloadService::officialDownloadInfo(const std::wstring& version,
                                              std::wstring& outFileName,
                                              std::wstring& outDirectUrl) {
    // 用 assets 接口而不是 binary/installer 的 307 重定向：
    // 它直接返回 name 与 link，既拿得到权威文件名（用于拼镜像链接），
    // 又同时列出 .zip 与 .msi，不必再手工跟重定向（此前的实现还漏掉了查询串里的文件名）。
    const std::wstring apiUrl = L"https://api.adoptium.net/v3/assets/latest/" + version +
                                L"/hotspot?architecture=x64&image_type=jdk&os=windows&vendor=eclipse";

    HINTERNET hSession = WinHttpOpen(jmtUserAgent().c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
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

    const DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD timeout = 30000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    // 跟随重定向 + 显式要求 JSON：否则部分网络环境会返回 HTML
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    const wchar_t* acceptHeader = L"Accept: application/json";
    bool ok = WinHttpSendRequest(hRequest, acceptHeader, static_cast<DWORD>(wcslen(acceptHeader)),
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(hRequest, nullptr);

    std::string body;
    if (ok) {
        DWORD available = 0;
        do {
            available = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
            if (available == 0) break;
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, chunk.data(), available, &read) || read == 0) break;
            chunk.resize(read);
            body += chunk;
        } while (available > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (body.empty()) {
        out_.line(OutputLevel::Debug, L"OfficialDownloadInfo: 未取到 API 响应");
        return false;
    }

    // UTF-8 → 宽字符（文件名是 ASCII，链接也是 ASCII，直接转换即可）
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, body.c_str(), static_cast<int>(body.size()), nullptr, 0);
    if (wideLen <= 0) return false;
    std::wstring json(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, body.c_str(), static_cast<int>(body.size()), json.data(), wideLen);

    if (!findFileNameInJson(json, version, outFileName, outDirectUrl)) {
        // 完整版本（17.0.2）在 latest 接口里通常没有对应产物：退化为不限定版本来匹配
        if (!findFileNameInJson(json, L"", outFileName, outDirectUrl)) {
            out_.line(OutputLevel::Debug, L"OfficialDownloadInfo: 响应里没有 zip/msi 产物");
            return false;
        }
    }
    return !outFileName.empty();
}

// 轻量探测一个候选链接：Range 取文件头 4 字节，用「Content-Type + 魔数」判真假。
// 必要性：部分镜像对不存在的文件返回 HTTP 200 + text/html 的浏览器校验页
// （实测中科大），只看状态码会把假页面当成功，白下 100+MB。
JdkDownloadService::ProbeResult JdkDownloadService::probeUrl(const std::wstring& url, bool wantZip) {
    lastProbeLength_ = 0;

    wchar_t curlPath[MAX_PATH];
    DWORD len = SearchPathW(nullptr, L"curl.exe", nullptr, MAX_PATH, curlPath, nullptr);
    if (len == 0) {
        wcscpy_s(curlPath, L"C:\\Windows\\System32\\curl.exe");
        if (!IsFile(curlPath)) return ProbeResult::Empty;
    }

    const std::wstring headFile = tempDownloadPath(L"probe", url);
    DeleteFileW(headFile.c_str());

    const std::wstring cmdLine = L"\"" + std::wstring(curlPath) + L"\" -sL --max-time 30"
                                 L" -r 0-3 -A \"" + jmtUserAgent() + L"\""
                                 L" -o \"" + headFile + L"\" \"" + url + L"\""
                                 L" -w \"%{http_code}|%{content_type}|%{size_download}\"";

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &attributes, 4096)) return ProbeResult::Empty;
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
        return ProbeResult::Empty;
    }
    CloseHandle(writeEnd);

    std::string captured;
    char buffer[512];
    DWORD read = 0;
    // 探测响应只有一行 -w 输出（约几十字节），远小于管道缓冲，等进程退出后再读不会死锁
    WaitForSingleObject(pi.hProcess, 60000);
    while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        captured.append(buffer, read);
    }
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(readEnd);

    // 解析 -w 输出：http_code|content_type|size_download
    int httpStatus = 0;
    bool sawHtml = false;
    {
        std::string line = captured;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        const size_t lastBreak = line.find_last_of('\n');
        if (lastBreak != std::string::npos) line = line.substr(lastBreak + 1);
        const size_t firstBar = line.find('|');
        const size_t secondBar = firstBar == std::string::npos ? std::string::npos : line.find('|', firstBar + 1);
        if (firstBar != std::string::npos) {
            try { httpStatus = std::stoi(line.substr(0, firstBar)); } catch (...) { httpStatus = 0; }
        }
        if (firstBar != std::string::npos && secondBar != std::string::npos) {
            std::string type = line.substr(firstBar + 1, secondBar - firstBar - 1);
            std::transform(type.begin(), type.end(), type.begin(),
                           [](unsigned char ch) { return static_cast<char>(::tolower(ch)); });
            sawHtml = type.find("html") != std::string::npos;
        }
    }

    if (exitCode != 0 || httpStatus < 200 || httpStatus >= 300) {
        DeleteFileW(headFile.c_str());
        out_.line(OutputLevel::Debug, L"探测失败（HTTP " + std::to_wstring(httpStatus) + L"）: " + url);
        return ProbeResult::Empty;
    }

    HANDLE hFile = CreateFileW(headFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return ProbeResult::Empty;
    BYTE magic[4] = {0};
    DWORD magicRead = 0;
    const BOOL magicOk = ReadFile(hFile, magic, 4, &magicRead, nullptr);
    LARGE_INTEGER fileSize{};
    GetFileSizeEx(hFile, &fileSize);
    CloseHandle(hFile);
    DeleteFileW(headFile.c_str());
    lastProbeLength_ = fileSize.QuadPart;

    if (!magicOk || magicRead < 2) return ProbeResult::Empty;
    if (sawHtml) return ProbeResult::Html;   // 浏览器校验页 / 错误页
    const bool isZipMagic = (magic[0] == 0x50 && magic[1] == 0x4B);
    const bool isMzMagic = (magic[0] == 0x4D && magic[1] == 0x5A);
    if (wantZip) return isZipMagic ? ProbeResult::Zip : ProbeResult::Html;
    return isMzMagic ? ProbeResult::Installer : ProbeResult::Html;
}

// 安装包（MSI / EXE）都是以 MZ 开头的 PE 文件
bool JdkDownloadService::isValidInstallerFile(const std::wstring& path) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    BYTE buffer[2] = {0};
    DWORD read = 0;
    const BOOL ok = ReadFile(hFile, buffer, 2, &read, nullptr);
    CloseHandle(hFile);
    if (!ok || read != 2) return false;
    return buffer[0] == 0x4D && buffer[1] == 0x5A;   // "MZ"
}

// ============================================================
// ========== 下载与安装主流程 =================================
// ============================================================

std::wstring JdkDownloadService::downloadAndInstall(const std::wstring& version) {
    const JavaVersion parsed = JavaVersion::parse(version);
    const std::wstring majorVersion = parsed.valid() ? std::to_wstring(parsed.feature) : version;
    out_.line(OutputLevel::Info, L"目标版本: " + majorVersion);

    if (!hasZipCandidate(majorVersion) && !hasManualInstaller(majorVersion)) {
        out_.line(OutputLevel::Error, L"没有可用于 Java " + majorVersion + L" 的下载源");
        out_.line(OutputLevel::Info, L"可下载的版本：6~10（安装程序，手动安装）/ 8、11、16 及以上（ZIP，自动安装）/ 12~26（华为云 GA 包）");
        return L"";
    }

    // 第 1 步：问 Adoptium metadata。拿到的文件名用于拼 Adoptium 镜像的链接，直链作为官方源。
    // 老版本（6~10）官方没有产物，这一步会失败，但不影响旧库 EXE 源可用。
    std::wstring fileName;
    std::wstring directUrl;
    const bool haveMeta = officialDownloadInfo(majorVersion, fileName, directUrl);

    // 第 2 步：构建候选源列表
    DownloadPlan plan;
    if (!buildDownloadPlan(majorVersion, haveMeta ? fileName : L"",
                           haveMeta ? directUrl : L"", plan)) {
        out_.line(OutputLevel::Error, L"没有可用于 Java " + majorVersion + L" 的下载源");
        return L"";
    }

    // 列出可用源，让用户在交互式终端里选择（回车默认第 1 个）
    const int preferred = chooseSource(plan);

    const std::wstring root = GetInstallRoot();
    const std::wstring targetDir = JoinPath(root, L"jdk-" + majorVersion);
    if (IsDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
        out_.line(OutputLevel::Info, L"JDK " + majorVersion + L" 已安装在 " + targetDir);
        return targetDir;
    }

    // 所选源排到最前，其余仍作为后备（避免所选源临时不可用就整体失败）
    std::vector<DownloadSource> ordered = plan.sources;
    if (preferred >= 1 && preferred <= static_cast<int>(ordered.size())) {
        std::rotate(ordered.begin(), ordered.begin() + (preferred - 1), ordered.end());
    }

    // 第 3 步：按源逐个尝试。ZIP 会自动解压安装；只有安装包时提示手动安装。
    size_t index = 0;
    bool triedAnyExe = false;
    for (const auto& source : ordered) {
        ++index;
        if (globalCancelState().cancelled()) {
            out_.line(OutputLevel::Warning, L"下载已取消");
            return L"";
        }
        const std::wstring order = L"（源 " + std::to_wstring(index) + L"/" +
                                   std::to_wstring(ordered.size()) + L"）";
        out_.line(OutputLevel::Info, L"尝试源" + order + L"：" + source.name);

        for (const auto& step : source.steps) {
            if (globalCancelState().cancelled()) break;

            const std::wstring host = HostThrottle::hostOf(step.url);
            if (throttle_.isHostBlocked(host)) {
                out_.line(OutputLevel::Warning, L"跳过：主机 " + host + L" 因限速/拒绝访问被临时拉黑");
                continue;
            }
            out_.line(OutputLevel::Info, L"下载链接：" + step.url);

            if (step.kind == DownloadStepKind::Zip) {
                if (tryZipStep(step, targetDir) && versionSatisfied(targetDir, majorVersion)) {
                    return targetDir;
                }
            } else {
                triedAnyExe = true;
                std::wstring downloaded;
                if (tryManualStep(step, downloaded)) {
                    out_.line(OutputLevel::Warning, L"提示：请手动运行此安装程序安装 JDK " +
                                                    majorVersion + L"，然后运行 'jmt search' 刷新缓存");
                    out_.line(OutputLevel::Info, L"建议安装路径: " + targetDir);
                    return L"EXE_DOWNLOADED";
                }
            }
        }
    }

    out_.line(OutputLevel::Error, L"所有下载源均失败，版本 " + majorVersion + L" 未安装");
    if (!triedAnyExe) {
        out_.line(OutputLevel::Info, L"提示：该版本没有可自动安装的压缩包，也没有可用的安装程序");
    }
    out_.line(OutputLevel::Info, L"本次共发出 " + std::to_wstring(throttle_.totalRequests()) +
                                 L" 次请求（预算 " + std::to_wstring(throttle_.policy().maxRequestsTotal) + L" 次）");
    return L"";
}

// 列出可用源并让用户选择。
// 返回 1 起的源编号；回车（空输入）= 1；非交互环境返回 0 表示按顺序自动尝试全部。
int JdkDownloadService::chooseSource(const DownloadPlan& plan) {
    if (plan.sources.empty()) {
        return 0;
    }

    size_t totalSteps = 0;
    for (const auto& source : plan.sources) {
        totalSteps += source.steps.size();
    }

    out_.blank();
    out_.line(OutputLevel::Info, L"查找到可用源：" + std::to_wstring(plan.sources.size()) + L"（候选链接共 " +
                                 std::to_wstring(totalSteps) + L" 条）");
    out_.blank();
    out_.line(OutputLevel::Info, L"请选择要使用的源：");
    for (size_t i = 0; i < plan.sources.size(); ++i) {
        const auto& source = plan.sources[i];
        std::wstring line = L"             " + std::to_wstring(i + 1) + L". " + source.name;
        if (source.isOfficial) {
            line += L" (速度慢)";
        }
        // 该源能提供自动安装的 ZIP 时标注出来，便于判断选它是「装好」还是「只下载」
        bool hasZip = false;
        bool hasExe = false;
        for (const auto& step : source.steps) {
            if (step.kind == DownloadStepKind::Zip) {
                hasZip = true;
            } else {
                hasExe = true;
            }
        }
        if (hasZip) {
            line += L"（可自动安装）";
        } else if (hasExe) {
            line += L"（需手动安装）";
        }
        out_.line(OutputLevel::Info, line);
    }
    out_.blank();

    // 非交互环境（脚本/管道）不弹提示，按顺序尝试全部源
    DWORD mode = 0;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const bool interactive = input != nullptr && input != INVALID_HANDLE_VALUE &&
                             GetConsoleMode(input, &mode) != 0;
    if (!interactive) {
        out_.line(OutputLevel::Info, L"（非交互环境：按顺序自动尝试全部源）");
        return 0;
    }

    out_.line(OutputLevel::Info, L"请输入您选择的源（回车默认选择: 1 ）：");
    std::wstring input_;
    std::getline(std::wcin, input_);
    out_.blank();
    if (input_.empty()) {
        return 1;
    }
    try {
        const int choice = std::stoi(input_);
        if (choice >= 1 && choice <= static_cast<int>(plan.sources.size())) {
            return choice;
        }
    } catch (const std::exception&) {
        // 落到下面的兜底
    }
    out_.line(OutputLevel::Warning, L"输入无效，按顺序自动尝试全部源");
    return 0;
}

bool JdkDownloadService::tryZipStep(const DownloadStep& step, const std::wstring& targetDir) {
    // 先探测：避免在「HTTP 200 + text/html 假页面」上白下 100+MB
    const ProbeResult probe = probeUrl(step.url, true);
    if (probe != ProbeResult::Zip) {
        const wchar_t* reason = L"未知原因";
        switch (probe) {
            case ProbeResult::Html:      reason = L"返回的是网页而不是文件（镜像校验页或错误页）"; break;
            case ProbeResult::Empty:     reason = L"请求失败或文件不存在"; break;
            case ProbeResult::Installer: reason = L"该地址是安装包而不是压缩包"; break;
            default: break;
        }
        out_.line(OutputLevel::Warning, L"  跳过：" + std::wstring(reason));
        return false;
    }
    out_.line(OutputLevel::Info, L"  探测通过（ZIP），开始下载...");

    const std::wstring tempFile = tempDownloadPath(L"zip", step.url);
    int httpStatus = 0;
    int64_t downloadedBytes = 0;
    int speedBps = 0;
    if (!downloadFile(step.url, tempFile, httpStatus, downloadedBytes, speedBps)) {
        out_.line(OutputLevel::Warning, L"  下载失败（HTTP " + std::to_wstring(httpStatus) + L"），换下一个源");
        return false;
    }
    out_.line(OutputLevel::Info, L"下载完成: " + ToWideString(curl_output::formatBytes(downloadedBytes)) +
                                 L"，平均 " + ToWideString(curl_output::formatSpeed(speedBps)));

    // 下载后再验一次魔数（防止中途被替换成错误页）
    if (!isValidZipFile(tempFile)) {
        out_.line(OutputLevel::Warning, L"  下载到的不是有效 ZIP，换下一个源");
        DeleteFileW(tempFile.c_str());
        return false;
    }

    if (!extractZip(tempFile, targetDir)) {
        out_.line(OutputLevel::Warning, L"  解压失败，换下一个源");
        DeleteFileW(tempFile.c_str());
        return false;
    }
    DeleteFileW(tempFile.c_str());

    if (JdkScanService::isValidJdk(targetDir)) {
        const std::wstring installed = JdkScanService::extractVersion(targetDir);
        out_.line(OutputLevel::Success,
                  installed.empty() ? L"JDK 安装成功" : L"JDK " + installed + L" 安装成功");
        return true;
    }
    if (fixNestedJdkDirectory(targetDir) && JdkScanService::isValidJdk(targetDir)) {
        const std::wstring installed = JdkScanService::extractVersion(targetDir);
        out_.line(OutputLevel::Success,
                  installed.empty() ? L"JDK 安装成功（修复嵌套结构）"
                                    : L"JDK " + installed + L" 安装成功（修复嵌套结构）");
        return true;
    }
    out_.line(OutputLevel::Warning, L"  解压后不是有效的 JDK，换下一个源");
    fs::remove_all(targetDir);
    return false;
}

bool JdkDownloadService::tryManualStep(const DownloadStep& step,
                                       std::wstring& outDownloadedPath) {
    const ProbeResult probe = probeUrl(step.url, false);
    if (probe != ProbeResult::Installer) {
        out_.line(OutputLevel::Warning, L"跳过 " + step.sourceName + L"：没有可用的安装程序");
        return false;
    }

    const std::wstring tempFile = tempDownloadPath(L"exe", step.url);
    out_.line(OutputLevel::Info, L"正在下载 EXE 安装程序...");

    int httpStatus = 0;
    int64_t downloadedBytes = 0;
    int speedBps = 0;
    if (!downloadFile(step.url, tempFile, httpStatus, downloadedBytes, speedBps)) {
        out_.line(OutputLevel::Warning, L"  下载失败（HTTP " + std::to_wstring(httpStatus) + L"）");
        return false;
    }
    if (!isValidInstallerFile(tempFile)) {
        out_.line(OutputLevel::Warning, L"  下载到的不是有效安装程序");
        DeleteFileW(tempFile.c_str());
        return false;
    }
    out_.line(OutputLevel::Info, L"下载完成: " + ToWideString(curl_output::formatBytes(downloadedBytes)) +
                                 L"，平均 " + ToWideString(curl_output::formatSpeed(speedBps)));
    out_.line(OutputLevel::Info, L"安装程序已保存到: " + tempFile);
    outDownloadedPath = tempFile;
    return true;
}

// 请求了完整版本时才校验：镜像只保留最新补丁，拿到的补丁号与请求可能不同
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


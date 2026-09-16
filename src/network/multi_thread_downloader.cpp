#define NOMINMAX
#include "network/multi_thread_downloader.hpp"
#include "common/cancel_token.hpp"
#include <windows.h>
#include <winhttp.h>
#include <mutex>
#include <thread>
#include <fstream>
#include <random>
#include <chrono>

// 确保 min/max 宏被禁用
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#pragma comment(lib, "winhttp.lib")

static std::wstring GetTempFilePath(int partIndex) {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring dir = tempPath;
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    return dir + L"jmt_part_" + std::to_wstring(partIndex) + L".tmp";
}

MultiThreadDownloader::MultiThreadDownloader() = default;
MultiThreadDownloader::~MultiThreadDownloader() = default;

void MultiThreadDownloader::setConnections(int count) {
    if (count < 1) count = 1;
    if (count > 32) count = 32;
    connections_ = count;
}

void MultiThreadDownloader::setTimeout(int seconds) {
    if (seconds < 5) seconds = 5;
    timeoutSeconds_ = seconds;
}

void MultiThreadDownloader::setRetryCount(int count) {
    if (count < 0) count = 0;
    maxRetries_ = count;
}

bool MultiThreadDownloader::getFileSize(const std::wstring& url, int64_t& outSize) {
    HINTERNET hSession = WinHttpOpen(L"JMT/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    HINTERNET hConnect = WinHttpOpenRequest(hSession, L"HEAD", url.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_REFRESH);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD timeout = timeoutSeconds_ * 1000;
    WinHttpSetOption(hConnect, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hConnect, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

    if (!WinHttpSendRequest(hConnect, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hConnect, nullptr)) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD contentLength = 0;
    DWORD size = sizeof(contentLength);
    bool ok = WinHttpQueryHeaders(hConnect, WINHTTP_QUERY_CONTENT_LENGTH,
                                  WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &size, nullptr);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (ok) {
        outSize = contentLength;
        return true;
    }
    return false;
}

bool MultiThreadDownloader::downloadPart(const std::wstring& url, const PartInfo& part,
                                         int64_t& outDownloaded) {
    outDownloaded = 0;

    HINTERNET hSession = WinHttpOpen(L"JMT/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpOpenRequest(hSession, L"GET", url.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_REFRESH);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD timeout = timeoutSeconds_ * 1000;
    WinHttpSetOption(hConnect, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hConnect, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

    std::wstring headers;
    if (part.end > 0) {
        wchar_t rangeHeader[256];
        swprintf_s(rangeHeader, L"Range: bytes=%lld-%lld", part.start, part.end);
        headers = rangeHeader;
        headers += L"\r\n";
    }

    if (!WinHttpSendRequest(hConnect,
                            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : (DWORD)headers.size(),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hConnect, nullptr)) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::ofstream out(part.tempFile, std::ios::binary);
    if (!out.is_open()) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD bytesReadChunk = 0;
    unsigned char buffer[65536];
    int64_t totalRead = 0;

    while (true) {
        if (cancelled_ || globalCancelState().cancelled()) {
            out.close();
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        if (!WinHttpReadData(hConnect, buffer, sizeof(buffer), &bytesReadChunk)) break;
        if (bytesReadChunk == 0) break;
        out.write(reinterpret_cast<const char*>(buffer), bytesReadChunk);
        totalRead += bytesReadChunk;
    }

    out.close();
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    outDownloaded = totalRead;

    if (part.end > 0 && totalRead != (part.end - part.start + 1)) {
        return false;
    }
    return true;
}

bool MultiThreadDownloader::mergeParts(const std::wstring& destPath,
                                       const std::vector<PartInfo>& parts) {
    std::ofstream dest(destPath, std::ios::binary | std::ios::trunc);
    if (!dest.is_open()) return false;

    for (const auto& part : parts) {
        std::ifstream src(part.tempFile, std::ios::binary);
        if (!src.is_open()) {
            dest.close();
            return false;
        }
        dest << src.rdbuf();
        src.close();
    }
    dest.close();
    return true;
}

void MultiThreadDownloader::cleanTempFiles(const std::vector<PartInfo>& parts) {
    for (const auto& part : parts) {
        DeleteFileW(part.tempFile.c_str());
    }
}

int MultiThreadDownloader::calcPercent(int64_t total, int64_t downloaded) {
    if (total <= 0) return -1;
    return static_cast<int>((downloaded * 100) / total);
}

bool MultiThreadDownloader::download(const std::wstring& url,
                                     const std::wstring& destPath,
                                     ProgressCallback onProgress,
                                     ErrorCallback onError) {
    cancelled_ = false;

    int64_t totalSize = 0;
    bool hasSize = getFileSize(url, totalSize);
    if (!hasSize) {
        totalSize = -1;
    }

    int connCount = connections_;
    if (totalSize <= 0 || totalSize < 10 * 1024 * 1024) {
        connCount = 1;
    }

    std::vector<PartInfo> parts;
    if (totalSize > 0) {
        int64_t partSize = totalSize / connCount;
        for (int i = 0; i < connCount; ++i) {
            PartInfo part;
            part.index = i;
            part.start = i * partSize;
            if (i == connCount - 1) {
                part.end = totalSize - 1;
            } else {
                part.end = (i + 1) * partSize - 1;
            }
            part.tempFile = GetTempFilePath(i);
            parts.push_back(part);
        }
    } else {
        PartInfo part;
        part.index = 0;
        part.start = 0;
        part.end = -1;
        part.tempFile = GetTempFilePath(0);
        parts.push_back(part);
    }

    std::vector<std::thread> threads;
    std::mutex progressMutex;
    int64_t totalDownloaded = 0;
    int completedParts = 0;
    bool hasError = false;
    std::wstring errorMsg;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 300);

    for (auto& part : parts) {
        std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));

        threads.emplace_back([this, &url, &part, &progressMutex, &totalDownloaded,
                                     &completedParts, &hasError, &errorMsg, onProgress, totalSize]() {
            int64_t downloaded = 0;
            bool ok = false;
            for (int retry = 0; retry <= maxRetries_; ++retry) {
                if (cancelled_ || globalCancelState().cancelled()) break;
                if (this->downloadPart(url, part, downloaded)) {
                    ok = true;
                    break;
                }
                Sleep(1000 * (retry + 1));
            }

            std::lock_guard<std::mutex> lock(progressMutex);
            if (ok) {
                totalDownloaded += downloaded;
                completedParts++;
            } else {
                hasError = true;
                errorMsg = L"下载分块 " + std::to_wstring(part.index) + L" 失败";
            }

            if (onProgress) {
                DownloadProgress prog;
                prog.totalSize = totalSize;
                prog.downloadedSize = totalDownloaded;
                prog.percent = calcPercent(totalSize, totalDownloaded);
                prog.speed = 0;
                onProgress(prog);
            }
        });
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    if (cancelled_ || globalCancelState().cancelled()) {
        cleanTempFiles(parts);
        if (onError) onError(L"下载已取消");
        return false;
    }

    if (hasError || completedParts != static_cast<int>(parts.size())) {
        cleanTempFiles(parts);
        if (onError) onError(errorMsg.empty() ? L"下载失败" : errorMsg);
        return false;
    }

    if (!mergeParts(destPath, parts)) {
        cleanTempFiles(parts);
        if (onError) onError(L"合并文件失败");
        return false;
    }

    cleanTempFiles(parts);

    if (onProgress) {
        DownloadProgress prog;
        prog.totalSize = totalSize;
        prog.downloadedSize = totalSize;
        prog.percent = 100;
        prog.speed = 0;
        onProgress(prog);
    }

    return true;
}

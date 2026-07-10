#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <cstdint>   // 可选，用于 uint8_t

// 下载进度回调
struct DownloadProgress {
    int64_t totalSize;
    int64_t downloadedSize;
    int speed;
    int percent;
};

using ProgressCallback = std::function<void(const DownloadProgress&)>;
using ErrorCallback = std::function<void(const std::wstring& errorMsg)>;

class MultiThreadDownloader {
public:
    MultiThreadDownloader();
    ~MultiThreadDownloader();

    void setConnections(int count);
    void setTimeout(int seconds);
    void setRetryCount(int count);

    bool download(
            const std::wstring& url,
            const std::wstring& destPath,
            ProgressCallback onProgress = nullptr,
            ErrorCallback onError = nullptr
    );

    void cancel();

private:
    struct PartInfo {
        int index;
        int64_t start;
        int64_t end;
        std::wstring tempFile;
        bool completed;
        int retryCount;
    };

    int connections_ = 4;
    int timeoutSeconds_ = 30;
    int maxRetries_ = 3;
    std::atomic<bool> cancelled_{false};

    // 获取文件大小（HEAD 请求）
    bool getFileSize(const std::wstring& url, int64_t& outSize);
    // 下载单个分块（线程函数）
    bool downloadPart(const std::wstring& url, const PartInfo& part, int64_t& outDownloaded);
    // 合并分块
    bool mergeParts(const std::wstring& destPath, const std::vector<PartInfo>& parts);
    // 清理临时文件
    void cleanTempFiles(const std::vector<PartInfo>& parts);
    // 计算进度
    int calcPercent(int64_t total, int64_t downloaded);

    // 底层 HTTP GET 请求（带 Range）
    bool httpGetRange(const std::wstring& url, int64_t start, int64_t end,
                      std::vector<unsigned char>& outBuffer, int64_t& outBytesRead);
};
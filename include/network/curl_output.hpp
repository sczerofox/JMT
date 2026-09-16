#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

// curl 输出的解析工具（纯函数，便于单测）：
//  1) --progress-bar 的百分比：从 "####   45.3%" 里取最后一个百分比
//  2) -w "%{http_code} %{size_download} %{time_total} %{speed_download}" 的收尾统计
namespace curl_output {

// 找不到百分比时返回 -1
inline int parsePercent(const std::string& text) {
    int lastPercent = -1;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') continue;
        // 向前回退，取出百分号前的整数（允许 45.3 这种小数，取整数部分）
        int end = static_cast<int>(i);
        int begin = end;
        while (begin > 0 && (std::isdigit(static_cast<unsigned char>(text[begin - 1])) ||
                             text[begin - 1] == '.')) {
            --begin;
        }
        if (begin == end) continue;
        const int value = std::atoi(text.substr(begin, end - begin).c_str());
        if (value >= 0 && value <= 100) {
            lastPercent = value;
        }
    }
    return lastPercent;
}

struct Stats {
    bool parsed = false;
    int httpStatus = 0;
    int64_t bytes = 0;
    int elapsedMs = 0;
    int speedBps = 0;
};

// 解析 -w 输出（形如 "200 47185920 12.345 3822345"）
inline Stats parseStats(const std::string& text) {
    Stats stats;
    size_t index = 0;
    auto nextNumber = [&text, &index]() -> std::string {
        while (index < text.size() &&
               !std::isdigit(static_cast<unsigned char>(text[index])) && text[index] != '-' &&
               text[index] != '.') {
            ++index;
        }
        const size_t begin = index;
        while (index < text.size() &&
               (std::isdigit(static_cast<unsigned char>(text[index])) || text[index] == '.' ||
                text[index] == '-')) {
            ++index;
        }
        return text.substr(begin, index - begin);
    };

    const std::string status = nextNumber();
    const std::string bytes = nextNumber();
    const std::string seconds = nextNumber();
    const std::string speed = nextNumber();
    if (status.empty() || bytes.empty()) {
        return stats;
    }

    stats.httpStatus = std::atoi(status.c_str());
    stats.bytes = std::atoll(bytes.c_str());
    if (!seconds.empty()) {
        stats.elapsedMs = static_cast<int>(std::atof(seconds.c_str()) * 1000.0);
    }
    if (!speed.empty()) {
        stats.speedBps = std::atoi(speed.c_str());
    }
    stats.parsed = true;
    return stats;
}

// 从完整输出里取「最后一个非空行」再解析：curl 的 -w 统计写在最后一行，
// 前面可能混着进度条输出（含各种数字），因此不能从头读。
inline Stats parseStatsFromTail(const std::string& text) {
    size_t end = text.size();
    for (int lines = 0; lines < 8 && end > 0; ++lines) {
        const size_t begin = text.find_last_of("\r\n", end - 1);
        const std::string line = (begin == std::string::npos)
                                         ? text.substr(0, end)
                                         : text.substr(begin + 1, end - begin - 1);
        const Stats stats = parseStats(line);
        if (stats.parsed) {
            return stats;
        }
        if (begin == std::string::npos) {
            break;
        }
        end = begin;
    }
    return Stats{};
}

// 人类可读的字节数：12.3 MB
inline std::string formatBytes(int64_t bytes) {
    char buffer[64];
    if (bytes >= 1024LL * 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));
    } else if (bytes >= 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lld KB", static_cast<long long>(bytes / 1024));
    }
    return buffer;
}

// 人类可读的速度：3.2 MB/s
inline std::string formatSpeed(int bytesPerSecond) {
    char buffer[64];
    if (bytesPerSecond >= 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB/s", static_cast<double>(bytesPerSecond) / (1024.0 * 1024));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%d KB/s", bytesPerSecond / 1024);
    }
    return buffer;
}

// 进度里程碑：返回这次新跨过的里程碑（25/50/75/100），没有跨过则返回 0。
// 用途：console 上用覆盖式的实时进度条，同时每跨过一档就打印一行普通输出，
// 这样即使输出被重定向（或终端不支持覆盖式刷新）也能看到下载在推进。
inline int milestoneCrossed(int lastMilestone, int percent) {
    if (percent < 0) return 0;
    int crossed = 0;
    for (int milestone : {25, 50, 75, 100}) {
        if (percent >= milestone && lastMilestone < milestone) {
            crossed = milestone;
        }
    }
    return crossed;
}

}  // namespace curl_output

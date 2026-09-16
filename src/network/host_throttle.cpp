#define NOMINMAX   // 避免 windows.h 的 min/max 宏与 std::min/std::max 冲突

#include "network/host_throttle.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>

HostThrottle::HostThrottle()
        : policy_{},
          sleeper_([](int milliseconds) { Sleep(static_cast<DWORD>(milliseconds)); }),
          clock_([] { return static_cast<int64_t>(GetTickCount64()); }) {}

HostThrottle::HostThrottle(ThrottlePolicy policy, Sleeper sleeper, Clock clock)
        : policy_(policy),
          sleeper_(sleeper ? std::move(sleeper)
                           : Sleeper([](int milliseconds) { Sleep(static_cast<DWORD>(milliseconds)); })),
          clock_(clock ? std::move(clock)
                       : Clock([] { return static_cast<int64_t>(GetTickCount64()); })) {}

std::wstring HostThrottle::hostOf(const std::wstring& url) {
    const size_t schemeEnd = url.find(L"://");
    if (schemeEnd == std::wstring::npos) return L"local";
    const std::wstring scheme = url.substr(0, schemeEnd);
    if (scheme == L"file") return L"local";

    size_t begin = schemeEnd + 3;
    size_t end = url.find_first_of(L"/?#", begin);
    std::wstring host = url.substr(begin, end == std::wstring::npos ? std::wstring::npos : end - begin);
    const size_t colon = host.find(L':');
    if (colon != std::wstring::npos) host = host.substr(0, colon);
    std::transform(host.begin(), host.end(), host.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(::towlower(ch)); });
    return host.empty() ? L"local" : host;
}

int64_t HostThrottle::nowMs() {
    return clock_();
}

int HostThrottle::nextJitterMs(int baseMs) {
    // 简单 LCG：只为制造抖动，不需要密码学质量
    jitterState_ = jitterState_ * 1664525u + 1013904223u;
    const int span = baseMs * policy_.jitterPercent / 100;
    if (span <= 0) return 0;
    return static_cast<int>(jitterState_ % static_cast<uint32_t>(2 * span + 1)) - span;
}

int HostThrottle::requestsForUrl(const std::wstring& url) const {
    const auto it = urlRequests_.find(url);
    return it == urlRequests_.end() ? 0 : it->second;
}

int HostThrottle::requestsForHost(const std::wstring& host) const {
    const auto it = hostRequests_.find(host);
    return it == hostRequests_.end() ? 0 : it->second;
}

int HostThrottle::activeForHost(const std::wstring& host) const {
    const auto it = activePerHost_.find(host);
    return it == activePerHost_.end() ? 0 : it->second;
}

bool HostThrottle::isHostBlocked(const std::wstring& host) {
    const auto it = blockedUntil_.find(host);
    if (it == blockedUntil_.end()) return false;
    if (nowMs() >= it->second) {
        blockedUntil_.erase(it);       // 拉黑到期，重新尝试
        return false;
    }
    return true;
}

std::vector<std::wstring> HostThrottle::blockedHosts() {
    std::vector<std::wstring> hosts;
    for (const auto& [host, until] : blockedUntil_) {
        if (nowMs() < until) hosts.push_back(host);
    }
    return hosts;
}

bool HostThrottle::acquire(const std::wstring& url, std::wstring& reason) {
    const std::wstring host = hostOf(url);

    if (isHostBlocked(host)) {
        reason = L"主机 " + host + L" 因限速/拒绝访问被临时拉黑";
        return false;
    }
    if (totalRequests_ >= policy_.maxRequestsTotal) {
        reason = L"已达单次命令的请求预算上限（" + std::to_wstring(policy_.maxRequestsTotal) + L" 次）";
        return false;
    }
    if (requestsForHost(host) >= policy_.maxRequestsPerHost) {
        reason = L"主机 " + host + L" 请求次数已达上限（" + std::to_wstring(policy_.maxRequestsPerHost) + L" 次）";
        return false;
    }
    if (requestsForUrl(url) >= policy_.maxRequestsPerUrl) {
        reason = L"该 URL 请求次数已达上限（" + std::to_wstring(policy_.maxRequestsPerUrl) + L" 次）";
        return false;
    }

    // 同一主机的并发上限：等待空闲槽，超过 maxWaitMs 就放弃
    int waitedMs = 0;
    while (activeForHost(host) >= policy_.maxConcurrentPerHost) {
        if (waitedMs >= policy_.maxWaitMs) {
            reason = L"主机 " + host + L" 并发已满，等待超时";
            return false;
        }
        sleeper_(50);
        waitedMs += 50;
    }

    // 同一主机的最小请求间隔
    const int64_t now = nowMs();
    const auto last = lastRequestAt_.find(host);
    if (last != lastRequestAt_.end()) {
        const int64_t elapsed = now - last->second;
        const int64_t waitMs = static_cast<int64_t>(policy_.minIntervalMs) - elapsed;
        if (waitMs > 0) {
            sleeper_(static_cast<int>(waitMs));
        }
    }

    ++totalRequests_;
    ++urlRequests_[url];
    ++hostRequests_[host];
    ++activePerHost_[host];
    lastRequestAt_[host] = nowMs();
    return true;
}

void HostThrottle::release(const std::wstring& url) {
    const std::wstring host = hostOf(url);
    auto it = activePerHost_.find(host);
    if (it != activePerHost_.end() && it->second > 0) {
        --it->second;
    }
}

void HostThrottle::noteResult(const std::wstring& url, int httpStatus, bool success) {
    const std::wstring host = hostOf(url);
    if (success) {
        failedAttempts_[url] = 0;
        return;
    }
    ++failedAttempts_[url];

    // 429（Too Many Requests）/503（服务不可用）/403（拒绝访问）→ 立刻拉黑该主机
    if (httpStatus == 429 || httpStatus == 503 || httpStatus == 403) {
        blockedUntil_[host] = nowMs() + policy_.throttleBlockMs;
    }
}

int HostThrottle::backoffMs(const std::wstring& url) {
    const auto it = failedAttempts_.find(url);
    const int failures = it == failedAttempts_.end() ? 0 : it->second;
    const int shift = std::min(failures, 3);
    const int baseMs = policy_.retryBaseMs << shift;    // 1s → 2s → 4s → 8s
    return std::max(0, baseMs + nextJitterMs(baseMs));
}

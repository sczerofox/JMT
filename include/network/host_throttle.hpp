#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// 镜像站点友好策略：限制同一主机的并发与频率、给重试加指数退避、
// 对 429/403 这类「限速/拒绝」信号立即拉黑该主机一段时间，避免被镜像封 IP。
struct ThrottlePolicy {
    int maxConcurrentPerHost = 2;    // 同一主机同时最多几个请求（1 最保守）
    int minIntervalMs = 1000;        // 同一主机相邻两次请求的最小间隔
    int retryBaseMs = 1000;          // 退避基数：1s → 2s → 4s
    int jitterPercent = 30;          // 退避抖动（±30%），避免多实例同步重试
    int maxRequestsPerUrl = 6;       // 单个 URL 的请求上限
    int maxRequestsPerHost = 10;     // 单个主机的请求上限
    int maxRequestsTotal = 60;       // 单次命令的总请求预算
    int maxWaitMs = 30000;           // 等待并发槽的最长时间，超时即放弃该请求
    int throttleBlockMs = 600000;    // 429/403 后拉黑主机 10 分钟
};

// 时间与等待都可注入：单测里用假的 sleeper/clock 断言等待序列，不会真的睡。
class HostThrottle {
public:
    using Sleeper = std::function<void(int milliseconds)>;
    using Clock = std::function<int64_t()>;   // 毫秒时间戳

    HostThrottle();
    HostThrottle(ThrottlePolicy policy, Sleeper sleeper, Clock clock);

    [[nodiscard]] const ThrottlePolicy& policy() const { return policy_; }

    // 请求前调用：必要时按策略等待。返回 false 时 reason 说明拒绝原因（预算用尽/主机被拉黑/并发已满）。
    [[nodiscard]] bool acquire(const std::wstring& url, std::wstring& reason);

    // 请求结束（无论成败）调用，释放并发槽
    void release(const std::wstring& url);

    // 结果反馈：httpStatus 为 0 表示传输层失败；429/503/403 会拉黑主机
    void noteResult(const std::wstring& url, int httpStatus, bool success);

    // 下一次重试建议等待的时间（指数退避 + 抖动）
    [[nodiscard]] int backoffMs(const std::wstring& url);

    [[nodiscard]] int totalRequests() const { return totalRequests_; }
    [[nodiscard]] int requestsForUrl(const std::wstring& url) const;
    [[nodiscard]] int requestsForHost(const std::wstring& host) const;
    [[nodiscard]] int activeForHost(const std::wstring& host) const;
    [[nodiscard]] bool isHostBlocked(const std::wstring& host);
    [[nodiscard]] std::vector<std::wstring> blockedHosts();

    // https://host:443/a/b.zip → host；file:///C:/x → local
    static std::wstring hostOf(const std::wstring& url);

private:
    ThrottlePolicy policy_;
    Sleeper sleeper_;
    Clock clock_;

    int totalRequests_ = 0;
    std::map<std::wstring, int> urlRequests_;
    std::map<std::wstring, int> hostRequests_;
    std::map<std::wstring, int> activePerHost_;
    std::map<std::wstring, int64_t> lastRequestAt_;
    std::map<std::wstring, int> failedAttempts_;
    std::map<std::wstring, int64_t> blockedUntil_;
    uint32_t jitterState_ = 0x9E3779B9u;

    [[nodiscard]] int64_t nowMs();
    [[nodiscard]] int nextJitterMs(int baseMs);
};

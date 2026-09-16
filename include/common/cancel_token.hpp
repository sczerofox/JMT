#pragma once

#include <atomic>

// 全局取消开关：Ctrl+C 置位后，下载引擎/管线在下一个检查点退出并清理临时文件。
// 用全局实例是为了让控制台信号处理器（只有 void() 签名）也能触达下载流程。
struct CancelState {
    std::atomic<bool> requested{false};

    void cancel() { requested.store(true); }
    [[nodiscard]] bool cancelled() const { return requested.load(); }
    void reset() { requested.store(false); }
};

inline CancelState& globalCancelState() {
    static CancelState state;
    return state;
}

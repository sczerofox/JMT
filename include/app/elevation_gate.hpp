#pragma once

#include <string>
#include <vector>

#include "common/exit_code.hpp"

struct AppContext;   // 只用于传引用，避免在头文件里拉入整个上下文定义

struct ElevationDecision {
    bool proceed;    // true  = 继续在当前进程执行命令
                     // false = 已启动提权进程（或提权失败），调用方应停止执行
    ExitCode code;   // proceed 为 false 时的退出码；提权成功启动时为 Ok
};

// 全仓唯一的提权入口：拼接命令行、请求 UAC、统一提示语与失败退出码。
// 此前 main.cpp 与 7 个命令各自复制了一份等价实现，语义已经分叉。
class ElevationGate {
public:
    // 元数据驱动：命令声明 requiresElevation() 后由调用方（main / REPL）统一调用
    [[nodiscard]] static ElevationDecision ensure(bool requiresElevation,
                                                  const std::vector<std::wstring>& args,
                                                  AppContext& ctx);

    // 无条件请求提权（供 rollback <版本> 这类按子命令提权的命令使用）
    [[nodiscard]] static ElevationDecision requestElevation(const std::vector<std::wstring>& args,
                                                            AppContext& ctx);

    // 与旧实现逐字一致：参数含空格时加引号，用空格连接；args[0] 即命令名
    [[nodiscard]] static std::wstring buildCommandLine(const std::vector<std::wstring>& args);
};

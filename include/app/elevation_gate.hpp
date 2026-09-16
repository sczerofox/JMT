#pragma once

#include <string>
#include <vector>

#include "common/exit_code.hpp"

// 全仓唯一的提权入口：拼接命令行、请求 UAC、统一提示语与失败退出码。
// 此前 main.cpp 与 7 个命令各自复制了一份等价实现，语义已经分叉。
class ElevationGate {
public:
    // 处理「需要提权且尚未提权」的路径：
    //   成功启动提权进程 → Ok（调用方应结束当前进程）
    //   失败            → PermissionDenied（对应退出码 3）
    [[nodiscard]] static ExitCode ensureElevated(const std::vector<std::wstring>& args);

    // 与旧实现逐字一致：参数含空格时加引号，用空格连接；args[0] 即命令名
    [[nodiscard]] static std::wstring buildCommandLine(const std::vector<std::wstring>& args);
};

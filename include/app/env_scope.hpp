#pragma once

#include <string>
#include <vector>

#include "platform/registry.hpp"

// 解析命令行里的 --user / --sys，并从参数列表中剔除这两个开关。
// 作用范围：会写 PATH 的命令（search / use / env / remove / download）。
// 同时出现时以后出现的为准（与既有 remove 的行为一致）。
struct EnvScope {
    EnvTarget target = EnvTarget::Auto;
    bool userRequested = false;       // 命令行显式带 --user
    std::vector<std::wstring> args;   // 已剔除 --user / --sys 的参数

    static EnvScope parse(const std::vector<std::wstring>& raw);
};

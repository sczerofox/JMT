#pragma once
#include <string>
#include <vector>
#include "app/app_context.hpp"
#include "common/exit_code.hpp"

class CommandBase {
public:
    virtual ~CommandBase() = default;

    // 命令名：与 CommandRegistry 的注册名一致
    [[nodiscard]] virtual std::wstring name() const = 0;

    // 是否需要管理员权限。调用方（main / REPL）据此统一走 ElevationGate，
    // 命令自身不再拼提权命令行。
    [[nodiscard]] virtual bool requiresElevation() const { return false; }

    // 是否支持 --user（仅改当前用户的环境变量）。支持时，命令行带 --user
    // 可以免提权执行（见 ElevationGate::ensure）。
    [[nodiscard]] virtual bool allowsUserScope() const { return false; }

    virtual ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) = 0;
    [[nodiscard]] virtual std::wstring getHelp() const = 0;
};

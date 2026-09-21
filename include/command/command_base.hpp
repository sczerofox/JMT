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

    // 提权之前的只读校验：参数合法性、目标是否存在等。
    // 默认什么都不做；返回非 Ok 时调用方直接结束（不会再弹 UAC）。
    virtual ExitCode preflight([[maybe_unused]] const std::vector<std::wstring>& args,
                               [[maybe_unused]] AppContext& ctx) {
        return ExitCode::Ok;
    }

    virtual ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) = 0;
    [[nodiscard]] virtual std::wstring getHelp() const = 0;
};

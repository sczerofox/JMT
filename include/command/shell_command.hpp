#pragma once
#include "command/command_base.hpp"

class ShellCommand : public CommandBase {
public:
    [[nodiscard]] std::wstring name() const override { return L"shell"; }
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"打开已配置 JMT 环境的新终端窗口"; }
};

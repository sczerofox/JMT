#pragma once
#include "command/command_base.hpp"

class ListCommand : public CommandBase {
public:
    [[nodiscard]] std::wstring name() const override { return L"list"; }
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"列出所有已安装的 Java 版本"; }
};

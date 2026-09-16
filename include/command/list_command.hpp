#pragma once
#include "command/command_base.hpp"

class ListCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"列出所有已安装的 Java 版本"; }
};
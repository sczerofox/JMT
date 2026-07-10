#pragma once
#include "../core/command_base.hpp"

class ShellCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"shell - Open new CMD with JMT interactive"; }
};
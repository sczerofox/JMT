#pragma once
#include "command/command_base.hpp"

class UseCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"切换到指定版本，如 jmt use 17"; }
};
#pragma once
#include "../core/command_base.hpp"

class UseCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"use <version> - Switch global Java version"; }
};
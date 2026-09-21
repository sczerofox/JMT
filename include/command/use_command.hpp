#pragma once
#include "command/command_base.hpp"

class UseCommand : public CommandBase {
public:
    [[nodiscard]] std::wstring name() const override { return L"use"; }
    [[nodiscard]] bool requiresElevation() const override { return true; }
    ExitCode preflight(const std::vector<std::wstring>& args, AppContext& ctx) override;
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"切换到指定版本，如 jmt use 17"; }
};

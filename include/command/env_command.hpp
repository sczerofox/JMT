#pragma once
#include "command/command_base.hpp"

class EnvCommand : public CommandBase {
public:
    [[nodiscard]] std::wstring name() const override { return L"env"; }
    [[nodiscard]] bool requiresElevation() const override { return true; }
    [[nodiscard]] bool allowsUserScope() const override { return true; }
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"将 JMT 目录添加到系统 PATH"; }
};

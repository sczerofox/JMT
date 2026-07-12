#pragma once
#include "../core/command_base.hpp"

class EnvCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"将 JMT 目录添加到系统 PATH"; }
};
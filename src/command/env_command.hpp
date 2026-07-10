#pragma once
#include "../core/command_base.hpp"

class EnvCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"env - Add JMT directory to PATH"; }
};
#pragma once
#include "../core/command_base.hpp"

class VersionCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"version - Show JMT version"; }
};
#pragma once
#include "../core/command_base.hpp"

class ListCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"list - Show all installed Java versions"; }
};
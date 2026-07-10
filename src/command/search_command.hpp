#pragma once
#include "../core/command_base.hpp"

class SearchCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"search [--force] - Scan all valid JDK"; }
};
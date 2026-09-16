#pragma once
#include "command/command_base.hpp"

class SearchCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"扫描并自动设置 JDK（--force 强制重新扫描）"; }
};
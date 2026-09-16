#pragma once
#include "command/command_base.hpp"

class SearchCommand : public CommandBase {
public:
    [[nodiscard]] std::wstring name() const override { return L"search"; }
    [[nodiscard]] bool requiresElevation() const override { return true; }
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"扫描并自动设置 JDK（--force 强制重新扫描）"; }
};

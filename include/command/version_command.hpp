#pragma once
#include "command/command_base.hpp"

class VersionCommand : public CommandBase {
public:
    [[nodiscard]] std::wstring name() const override { return L"version"; }
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"显示 JMT 版本信息"; }
};

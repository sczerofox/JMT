#pragma once
#include "command/command_base.hpp"

class RemoveCommand : public CommandBase {
public:
    [[nodiscard]] std::wstring name() const override { return L"remove"; }
    [[nodiscard]] bool requiresElevation() const override { return true; }
    [[nodiscard]] bool allowsUserScope() const override { return true; }
    ExitCode preflight(const std::vector<std::wstring>& args, AppContext& ctx) override;
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"删除 JDK 或清理环境（env / all / temp / trash / <版本号>）"; }
};

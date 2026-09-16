#pragma once
#include "command/command_base.hpp"

class RemoveCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"删除 JDK 或清理环境（env / all / temp / trash / <版本号>）"; }
};
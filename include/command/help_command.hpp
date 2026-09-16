#pragma once
#include "command/command_base.hpp"
#include "command/command_registry.hpp"   // 添加完整头文件

class HelpCommand : public CommandBase {
public:
    explicit HelpCommand(const CommandRegistry& registry);
    [[nodiscard]] std::wstring name() const override { return L"help"; }
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"显示此帮助信息或命令详情"; }
private:
    const CommandRegistry& registry_;
};

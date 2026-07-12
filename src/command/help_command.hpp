#pragma once
#include "../core/command_base.hpp"
#include "../core/command_registry.hpp"   // 添加完整头文件

class HelpCommand : public CommandBase {
public:
    explicit HelpCommand(const CommandRegistry& registry);
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"显示此帮助信息或命令详情"; }
private:
    const CommandRegistry& registry_;
};
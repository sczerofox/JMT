#pragma once
#include "../core/command_base.hpp"

class RollbackCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override {
        return L"从回收站恢复已删除的 JDK（list 查看可恢复版本）";
    }
};
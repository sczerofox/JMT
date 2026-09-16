#pragma once
#include "command/command_base.hpp"

class RollbackCommand : public CommandBase {
public:
    [[nodiscard]] std::wstring name() const override { return L"rollback"; }
    // rollback list 只读；恢复在命令内部请求提权
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override {
        return L"从回收站恢复已删除的 JDK（list 查看可恢复版本）";
    }
};

#pragma once
#include "../core/command_base.hpp"

class RollbackCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override {
        return L"rollback <version> - Restore a deleted JDK version from trash";
    }
};
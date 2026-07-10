#pragma once
#include "../core/command_base.hpp"

class CleanTrashCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override {
        return L"clean-trash [--force] - Permanently delete all trashed JDKs";
    }
};
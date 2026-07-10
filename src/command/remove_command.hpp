#pragma once
#include "../core/command_base.hpp"

class RemoveCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"remove [--user|--sys] - Clear all JMT env"; }
};
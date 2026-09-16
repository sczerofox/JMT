#pragma once
#include <string>
#include <vector>
#include "command/jmt_context.hpp"

class CommandBase {
public:
    virtual ~CommandBase() = default;
    virtual int execute(const std::vector<std::wstring>& args, JmtContext& ctx) = 0;
    [[nodiscard]] virtual std::wstring getHelp() const = 0;
};
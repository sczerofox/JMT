#pragma once
#include "../core/command_base.hpp"

class DataCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override {
        return L"导出/导入 JDK 列表（output / input）";
    }
};

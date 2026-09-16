#pragma once
#include "command/command_base.hpp"

class DataCommand : public CommandBase {
public:
    [[nodiscard]] std::wstring name() const override { return L"data"; }
    // data output 只读；data input 在命令内部按子命令请求提权
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override {
        return L"导出/导入 JDK 列表（output / input）";
    }
};

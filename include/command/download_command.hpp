#pragma once
#include "command/command_base.hpp"

class DownloadCommand : public CommandBase {
public:
    [[nodiscard]] std::wstring name() const override { return L"download"; }
    [[nodiscard]] bool requiresElevation() const override { return true; }
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override {
        return L"下载并自动配置 JDK：能下 ZIP 就自动安装，只有安装包时提示手动安装";
    }
};

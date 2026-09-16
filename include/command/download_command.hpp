#pragma once
#include "command/command_base.hpp"

class DownloadCommand : public CommandBase {
public:
    [[nodiscard]] std::wstring name() const override { return L"download"; }
    [[nodiscard]] bool requiresElevation() const override { return true; }
    ExitCode execute(const std::vector<std::wstring>& args, AppContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"从镜像或官方源下载 JDK 并自动配置环境（exe 仅下载安装包）"; }
};

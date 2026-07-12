#pragma once
#include "../core/command_base.hpp"

class DownloadCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"从镜像或官方源下载 JDK 并自动配置环境（exe 仅下载安装包）"; }
};
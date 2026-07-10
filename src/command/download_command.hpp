#pragma once
#include "../core/command_base.hpp"

class DownloadCommand : public CommandBase {
public:
    int execute(const std::vector<std::wstring>& args, JmtContext& ctx) override;
    [[nodiscard]] std::wstring getHelp() const override { return L"download <version> - Download and install JDK"; }
};
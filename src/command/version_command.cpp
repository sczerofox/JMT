#include "command/version_command.hpp"
#include "console/color_print.hpp"

int VersionCommand::execute(const std::vector<std::wstring>& args, JmtContext& ctx) {
    PrintInfo(L"JMT v1.7 (build 2026.07.13)");
    return 0;
}
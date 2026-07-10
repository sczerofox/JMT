#include "version_command.hpp"
#include "../print/color_print.hpp"

int VersionCommand::execute(const std::vector<std::wstring>& args, JmtContext& ctx) {
    PrintInfo(L"JMT v1.6 (build 2026.07.6)");
    return 0;
}
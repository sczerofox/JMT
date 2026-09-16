#include "command/version_command.hpp"
#include "console/color_print.hpp"

ExitCode VersionCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    PrintInfo(L"JMT v1.7 (build 2026.07.13)");
    return ExitCode::Ok;
}
#include "command/version_command.hpp"
#include "platform/output.hpp"

ExitCode VersionCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    ctx.out->line(OutputLevel::Info, L"JMT v1.7 (build 2026.07.13)");
    return ExitCode::Ok;
}
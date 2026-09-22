#include "command/version_command.hpp"
#include "platform/output.hpp"
#include "common/version.hpp"
#include <string>

ExitCode VersionCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    // 前置空行由 ReplEngine 统一输出，这里不再自己加，避免出现两个空行
    ctx.out->line(OutputLevel::Info,
                  std::wstring(jmt::kAppName) + L" " + jmt::kVersion +
                          L" (build " + jmt::kBuildDate + L")");
    return ExitCode::Ok;
}

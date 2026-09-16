#include "command/env_command.hpp"
#include "jdk/jmt_path_service.hpp"
#include "platform/output.hpp"
#include "app/elevation_gate.hpp"
#include "app/env_scope.hpp"

ExitCode EnvCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    const EnvScope scope = EnvScope::parse(args);

    if (!ctx.jmtPath->registerJmtPath(ctx.paths.exeDir, scope.target)) {
        ctx.out->line(OutputLevel::Error, L"注册 PATH 失败，请以管理员身份运行，或改用 --user 只写当前用户的环境变量");
        return ExitCode::PermissionDenied;
    }
    const std::wstring scopeLabel = (scope.target == EnvTarget::UserOnly) ? L"（用户级）" : L"";
    ctx.out->line(OutputLevel::Success, L"JMT 目录已添加至 PATH: " + ctx.paths.exeDir + scopeLabel);
    ctx.out->line(OutputLevel::Warning, L"请重启终端使 PATH 生效");
    return ExitCode::Ok;
}

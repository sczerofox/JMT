#include "command/env_command.hpp"
#include "jdk/jmt_path_service.hpp"
#include "platform/output.hpp"
#include "app/elevation_gate.hpp"

ExitCode EnvCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    // 已提权，继续执行原有逻辑

    // 已提权，执行原有逻辑
    if (!ctx.jmtPath->registerJmtPath(ctx.paths.exeDir, EnvTarget::Auto)) {
        ctx.out->line(OutputLevel::Error, L"注册 PATH 失败，请以管理员身份运行或使用 --user");
        return ExitCode::PermissionDenied;
    }
    ctx.out->line(OutputLevel::Success, L"JMT 目录已添加至 PATH: " + ctx.paths.exeDir);
    ctx.out->line(OutputLevel::Warning, L"请重启终端使 PATH 生效");
    return ExitCode::Ok;
}
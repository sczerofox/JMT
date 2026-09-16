#include "command/env_command.hpp"
#include "jdk/jmt_path_service.hpp"
#include "console/color_print.hpp"
#include "app/elevation_gate.hpp"

ExitCode EnvCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    // 已提权，继续执行原有逻辑

    // 已提权，执行原有逻辑
    if (!JmtPathService::registerJmtPath(ctx.paths.exeDir, EnvTarget::Auto)) {
        PrintError(L"注册 PATH 失败，请以管理员身份运行或使用 --user");
        return ExitCode::PermissionDenied;
    }
    PrintSuccess(L"JMT 目录已添加至 PATH: " + ctx.paths.exeDir);
    PrintWarning(L"请重启终端使 PATH 生效");
    return ExitCode::Ok;
}
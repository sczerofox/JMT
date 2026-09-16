#include "command/use_command.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "jdk/java_env_service.hpp"
#include "platform/output.hpp"
#include "app/elevation_gate.hpp"
#include "app/env_scope.hpp"

ExitCode UseCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    const EnvScope scope = EnvScope::parse(args);

    if (scope.args.size() < 2) {
        ctx.out->line(OutputLevel::Error, L"缺少版本号，用法: use <version> [--user|--sys]");
        return ExitCode::BadArgs;
    }
    const std::wstring& ver = scope.args[1];

    auto jdks = ctx.scan->scanJdks(false, true);
    bool found = false;
    std::wstring targetPath;
    for (const auto& [v, p] : jdks) {
        if (v == ver) {
            found = true;
            targetPath = p;
            break;
        }
    }
    if (!found) {
        ctx.out->line(OutputLevel::Error, L"未找到版本 " + ver);
        return ExitCode::NotFound;
    }

    if (!ctx.env->setCurrentJdk(targetPath, scope.target)) {
        ctx.out->line(OutputLevel::Error, L"切换失败，请确保有管理员权限");
        return ExitCode::PermissionDenied;
    }

    ctx.out->line(OutputLevel::Success, L"已切换 PATH 至版本 " + ver);
    ctx.out->line(OutputLevel::Warning, L"请重启终端使环境变量生效！");
    return ExitCode::Ok;
}

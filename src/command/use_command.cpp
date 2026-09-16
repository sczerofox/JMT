#include "command/use_command.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "jdk/java_env_service.hpp"
#include "console/color_print.hpp"
#include "app/elevation_gate.hpp"

ExitCode UseCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {

    if (args.size() < 2) {
        PrintError(L"缺少版本号，用法: use <version>");
        return ExitCode::BadArgs;
    }
    const std::wstring& ver = args[1];

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
        PrintError(L"未找到版本 " + ver);
        return ExitCode::NotFound;
    }

    if (!ctx.env->setCurrentJdk(targetPath, EnvTarget::Auto)) {
        PrintError(L"切换失败，请确保有管理员权限");
        return ExitCode::PermissionDenied;
    }

    PrintSuccess(L"已切换 PATH 至版本 " + ver);
    PrintWarning(L"请重启终端使环境变量生效！");
    return ExitCode::Ok;
}
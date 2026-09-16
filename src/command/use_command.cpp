#include "command/use_command.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "jdk/java_env_service.hpp"
#include "console/color_print.hpp"
#include "app/elevation_gate.hpp"

int UseCommand::execute(const std::vector<std::wstring>& args, JmtContext& ctx) {
    if (!ctx.isElevated) {
        return toInt(ElevationGate::ensureElevated(args));
    }

    if (args.size() < 2) {
        PrintError(L"缺少版本号，用法: use <version>");
        return 1;
    }
    const std::wstring& ver = args[1];

    auto jdks = JdkScanService::scanJdks(false, ctx.paths.cacheFile, true);
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
        return 2;
    }

    if (!JavaEnvService::setCurrentJdk(targetPath, EnvTarget::Auto)) {
        PrintError(L"切换失败，请确保有管理员权限");
        return 3;
    }

    PrintSuccess(L"已切换 PATH 至版本 " + ver);
    PrintWarning(L"请重启终端使环境变量生效！");
    return 0;
}
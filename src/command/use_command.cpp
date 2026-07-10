#include "use_command.hpp"
#include "../service/jdk_scan_service.hpp"
#include "../service/java_env_service.hpp"
#include "../print/color_print.hpp"
#include "../infrastructure/elevation_helper.hpp"

int UseCommand::execute(const std::vector<std::wstring>& args, JmtContext& ctx) {
    if (!ctx.isElevated) {
        std::wstring cmdLine;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) cmdLine += L' ';
            if (args[i].find(L' ') != std::wstring::npos)
                cmdLine += L'"' + args[i] + L'"';
            else
                cmdLine += args[i];
        }
        PrintInfo(L"需要管理员权限，正在请求提权...");
        if (ElevationHelper::RelaunchElevated(cmdLine)) {
            return 0;
        } else {
            PrintError(L"提权失败，请手动以管理员身份运行");
            return 3;
        }
    }

    if (args.size() < 2) {
        PrintError(L"缺少版本号，用法: use <version>");
        return 1;
    }
    const std::wstring& ver = args[1];

    auto jdks = JdkScanService::scanJdks(false, ctx.cacheFilePath, true);
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
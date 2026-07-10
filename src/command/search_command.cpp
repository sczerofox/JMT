#include "search_command.hpp"
#include "../service/jdk_scan_service.hpp"
#include "../service/java_env_service.hpp"
#include "../print/color_print.hpp"
#include "../utils/string_helper.hpp"
#include "../infrastructure/elevation_helper.hpp"

int SearchCommand::execute(const std::vector<std::wstring>& args, JmtContext& ctx) {
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

    bool force = false;
    if (args.size() > 1 && StringHelper::equalsIgnoreCase(args[1], L"--force"))
        force = true;

    auto jdks = JdkScanService::scanJdks(force, ctx.cacheFilePath);
    if (jdks.empty()) {
        PrintWarning(L"未找到任何合法 JDK");
        return 2;
    }

    // 选择最大版本
    auto maxIt = std::max_element(jdks.begin(), jdks.end(),
                                  [](const auto& a, const auto& b) {
                                      return std::stoi(a.first) < std::stoi(b.first);
                                  });
    std::wstring maxVer = maxIt->first;
    std::wstring maxPath = maxIt->second;

    // 切换 PATH 到最大版本
    if (!JavaEnvService::setCurrentJdk(maxPath, EnvTarget::Auto)) {
        PrintError(L"设置当前 JDK 到 PATH 失败，请检查权限");
        return 3;
    }

    PrintSuccess(L"扫描完成，共 " + std::to_wstring(jdks.size()) + L" 个版本，当前生效: " + maxVer);
    PrintWarning(L"请重启终端使环境变量生效！");
    return 0;
}
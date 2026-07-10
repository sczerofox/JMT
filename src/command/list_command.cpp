#include "list_command.hpp"
#include "../service/jdk_scan_service.hpp"
#include "../service/java_env_service.hpp"
#include "../print/color_print.hpp"

int ListCommand::execute(const std::vector<std::wstring>& args, JmtContext& ctx) {
    auto jdks = JdkScanService::scanJdks(false, ctx.cacheFilePath, true);
    if (jdks.empty()) {
        PrintWarning(L"未找到任何 JDK，请运行 'search' 强制扫描");
        return 2;
    }

    std::wstring currentVer = JavaEnvService::getCurrentVersion();
    PrintInfo(L"已安装的 Java 版本：");
    for (const auto& [ver, path] : jdks) {
        if (ver == currentVer) {
            PrintSuccess(L"  Java " + ver + L" (当前生效)  -> " + path);
        } else {
            PrintInfo(L"  Java " + ver + L"  -> " + path);
        }
    }
    PrintInfo(L"版本总数：" + std::to_wstring(jdks.size()));
    if (!currentVer.empty()) {
        PrintSuccess(L"当前生效版本: " + currentVer);
    } else {
        PrintWarning(L"当前没有 JMT 管理的 JDK 版本");
    }
    return 0;
}
#include "command/list_command.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "jdk/java_env_service.hpp"
#include "platform/output.hpp"

ExitCode ListCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    auto jdks = ctx.scan->scanJdks(false, true);
    if (jdks.empty()) {
        ctx.out->line(OutputLevel::Warning, L"未找到任何 JDK，请运行 'search' 强制扫描");
        return ExitCode::NotFound;
    }

    std::wstring currentVer = ctx.env->getCurrentVersion();
    ctx.out->line(OutputLevel::Info, L"已安装的 Java 版本：");
    for (const auto& [ver, path] : jdks) {
        if (ver == currentVer) {
            ctx.out->line(OutputLevel::Success, L"  Java " + ver + L" (当前生效)  -> " + path);
        } else {
            ctx.out->line(OutputLevel::Info, L"  Java " + ver + L"  -> " + path);
        }
    }
    ctx.out->line(OutputLevel::Info, L"版本总数：" + std::to_wstring(jdks.size()));
    if (!currentVer.empty()) {
        ctx.out->line(OutputLevel::Success, L"当前生效版本: " + currentVer);
    } else {
        ctx.out->line(OutputLevel::Warning, L"当前没有 JMT 管理的 JDK 版本");
    }
    return ExitCode::Ok;
}
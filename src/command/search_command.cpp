#include "command/search_command.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "jdk/java_env_service.hpp"
#include "platform/output.hpp"
#include "common/string_helper.hpp"
#include "app/elevation_gate.hpp"
#include "app/env_scope.hpp"

ExitCode SearchCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    const EnvScope scope = EnvScope::parse(args);

    bool force = false;
    if (scope.args.size() > 1 && StringHelper::equalsIgnoreCase(scope.args[1], L"--force"))
        force = true;

    auto jdks = ctx.scan->scanJdks(force);
    if (jdks.empty()) {
        ctx.out->line(OutputLevel::Warning, L"未找到任何合法 JDK");
        return ExitCode::NotFound;
    }

    // 检查 PATH 中是否已有 JDK 版本
    std::wstring currentVer = ctx.env->getCurrentVersion();
    if (!currentVer.empty()) {
        ctx.out->line(OutputLevel::Success, L"扫描完成，共 " + std::to_wstring(jdks.size()) + L" 个版本，当前生效: " + currentVer);
        ctx.out->line(OutputLevel::Warning, L"如需切换版本，请使用 'jmt use <版本号>'");
        return ExitCode::Ok;
    }

    // PATH 中没有 JDK，选择最大版本
    auto maxIt = std::max_element(jdks.begin(), jdks.end(),
                                  [](const auto& a, const auto& b) {
                                      return std::stoi(a.first) < std::stoi(b.first);
                                  });
    std::wstring maxVer = maxIt->first;
    std::wstring maxPath = maxIt->second;

    // 切换 PATH 到最大版本
    if (!ctx.env->setCurrentJdk(maxPath, scope.target)) {
        ctx.out->line(OutputLevel::Error, L"设置当前 JDK 到 PATH 失败，请检查权限");
        return ExitCode::PermissionDenied;
    }

    ctx.out->line(OutputLevel::Success, L"扫描完成，共 " + std::to_wstring(jdks.size()) + L" 个版本，已自动设置: " + maxVer);
    ctx.out->line(OutputLevel::Warning, L"请重启终端使环境变量生效！");
    return ExitCode::Ok;
}

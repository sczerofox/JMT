#include "command/use_command.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "jdk/java_env_service.hpp"
#include "platform/output.hpp"
#include "app/elevation_gate.hpp"
#include "app/env_scope.hpp"
#include "jdk/version_match.hpp"

ExitCode UseCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    const EnvScope scope = EnvScope::parse(args);

    // --exact：只接受完整版本（例如 17.0.2），不做主版本/前缀匹配
    bool exactOnly = false;
    std::vector<std::wstring> positional;
    for (const auto& arg : scope.args) {
        if (arg == L"--exact") {
            exactOnly = true;
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() < 2) {
        ctx.out->line(OutputLevel::Error, L"缺少版本号，用法: use <version> [--exact] [--user|--sys]");
        return ExitCode::BadArgs;
    }
    const std::wstring& query = positional[1];

    auto jdks = ctx.scan->scanJdks(false, true);
    const VersionMatch match = resolveVersion(jdks, query, exactOnly);
    if (!match.found) {
        ctx.out->line(OutputLevel::Error, L"未找到版本 " + query +
                      (exactOnly ? L"（--exact 只接受完整版本）" : L"，可用 'jmt list' 查看已安装版本"));
        return ExitCode::NotFound;
    }

    if (match.ambiguous) {
        ctx.out->line(OutputLevel::Info, L"版本 " + query + L" 命中多个已安装版本，已选择最高的 " + match.version);
        for (const auto& [version, path] : match.candidates) {
            ctx.out->line(OutputLevel::Debug, L"  候选: " + version + L" -> " + path);
        }
    }

    if (!ctx.env->setCurrentJdk(match.path, scope.target)) {
        ctx.out->line(OutputLevel::Error, L"切换失败，请确保有管理员权限");
        return ExitCode::PermissionDenied;
    }

    const std::wstring matched = (match.version == query) ? L"" : L"（由 " + query + L" 匹配）";
    ctx.out->line(OutputLevel::Success, L"已切换 PATH 至版本 " + match.version + matched);
    ctx.out->line(OutputLevel::Info, L"路径: " + match.path);
    ctx.out->line(OutputLevel::Warning, L"请重启终端使环境变量生效！");
    return ExitCode::Ok;
}

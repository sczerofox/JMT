#include "app/elevation_gate.hpp"

#include "app/app_context.hpp"
#include "platform/elevator.hpp"
// 过渡期：提示语沿用既有 Print* 转发（skeleton/8 改为注入 IOutput）

std::wstring ElevationGate::buildCommandLine(const std::vector<std::wstring>& args) {
    std::wstring commandLine;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) commandLine += L' ';
        if (args[i].find(L' ') != std::wstring::npos) {
            commandLine += L'"' + args[i] + L'"';
        } else {
            commandLine += args[i];
        }
    }
    return commandLine;
}

ElevationDecision ElevationGate::ensure(bool requiresElevation,
                                        const std::vector<std::wstring>& args,
                                        AppContext& ctx) {
    if (!requiresElevation || ctx.isElevated) {
        return ElevationDecision{true, ExitCode::Ok};
    }
    return requestElevation(args, ctx);
}

ElevationDecision ElevationGate::requestElevation(const std::vector<std::wstring>& args,
                                                  AppContext& ctx) {
    ctx.out->line(OutputLevel::Info, L"需要管理员权限，正在请求提权...");
    if (ctx.elevator != nullptr && ctx.elevator->relaunchElevated(buildCommandLine(args))) {
        return ElevationDecision{false, ExitCode::Ok};
    }
    ctx.out->line(OutputLevel::Error, L"提权失败，请手动以管理员身份运行");
    return ElevationDecision{false, ExitCode::PermissionDenied};
}

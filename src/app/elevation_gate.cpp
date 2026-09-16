#include "app/elevation_gate.hpp"

#include "platform/elevator.hpp"
// 过渡期：提示语沿用既有 Print* 转发（skeleton/8 改为注入 IOutput）
#include "console/color_print.hpp"

namespace {

WinElevator& defaultElevator() {
    static WinElevator elevator;
    return elevator;
}

}  // namespace

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

ExitCode ElevationGate::ensureElevated(const std::vector<std::wstring>& args) {
    PrintInfo(L"需要管理员权限，正在请求提权...");
    if (defaultElevator().relaunchElevated(buildCommandLine(args))) {
        return ExitCode::Ok;
    }
    PrintError(L"提权失败，请手动以管理员身份运行");
    return ExitCode::PermissionDenied;
}

#include "env_command.hpp"
#include "../service/jmt_path_service.hpp"
#include "../print/color_print.hpp"
#include "../infrastructure/elevation_helper.hpp"

int EnvCommand::execute(const std::vector<std::wstring>& args, JmtContext& ctx) {
    if (!ctx.isElevated) {
        // 构建参数列表（不含命令本身，因为 RelaunchElevated 会自动添加 exe 路径）
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
            return 0;   // 父进程退出，子进程已启动
        } else {
            PrintError(L"提权失败，请手动以管理员身份运行");
            return 3;
        }
    }
    // 已提权，继续执行原有逻辑

    // 已提权，执行原有逻辑
    if (!JmtPathService::registerJmtPath(ctx.exeDirectory, EnvTarget::Auto)) {
        PrintError(L"注册 PATH 失败，请以管理员身份运行或使用 --user");
        return 3;
    }
    PrintSuccess(L"JMT 目录已添加至 PATH: " + ctx.exeDirectory);
    PrintWarning(L"请重启终端使 PATH 生效");
    return 0;
}
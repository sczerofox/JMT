#include "core/command_registry.hpp"
#include "core/jmt_context.hpp"
#include "command/search_command.hpp"
#include "command/list_command.hpp"
#include "command/use_command.hpp"
#include "command/env_command.hpp"
#include "command/remove_command.hpp"
#include "command/download_command.hpp"
#include "command/version_command.hpp"
#include "command/shell_command.hpp"
#include "command/help_command.hpp"
#include "command/rollback_command.hpp"
#include "command/clean_trash_command.hpp"
#include "repl/repl_engine.hpp"
#include "print/color_print.hpp"
#include "utils/utils.hpp"
#include "infrastructure/elevation_helper.hpp"
#include "service/jdk_download_service.hpp"
#include <vector>
#include <memory>

int wmain(int argc, wchar_t* argv[]) {
    // 初始化控制台
    InitConsole();

    // ---------- 初始化资源映射 ----------
    JdkDownloadService::reloadMappings();

    // 构建上下文
    JmtContext ctx;
    ctx.exeDirectory = GetExeDirectory();
    ctx.cacheFilePath = JoinPath(ctx.exeDirectory, L".jmt_cache");
    ctx.isInteractive = (argc == 1);
    ctx.isElevated = ElevationHelper::IsElevated();

    // 构建命令注册表
    CommandRegistry registry;
    registry.registerCommand(L"search", std::make_unique<SearchCommand>());
    registry.registerCommand(L"list", std::make_unique<ListCommand>());
    registry.registerCommand(L"use", std::make_unique<UseCommand>());
    registry.registerCommand(L"env", std::make_unique<EnvCommand>());
    registry.registerCommand(L"remove", std::make_unique<RemoveCommand>());
    registry.registerCommand(L"download", std::make_unique<DownloadCommand>());
    registry.registerCommand(L"version", std::make_unique<VersionCommand>());
    registry.registerCommand(L"shell", std::make_unique<ShellCommand>());
    registry.registerCommand(L"help", std::make_unique<HelpCommand>(registry));
    registry.registerCommand(L"rollback", std::make_unique<RollbackCommand>());
    registry.registerCommand(L"clean-trash", std::make_unique<CleanTrashCommand>());

    // 解析命令
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

    // 如果交互模式，直接进入 REPL
    if (ctx.isInteractive) {
        ReplEngine engine(registry, ctx);
        engine.run();
        return 0;
    }

    // 单次命令模式
    if (args.empty()) {
        // 无参数但 argc>1？实际上不可能，但以防万一
        PrintError(L"无命令");
        return 1;
    }

    std::wstring cmd = args[0];
    // 检查是否需要提权
    bool needsAdmin = (cmd == L"use" || cmd == L"env" || cmd == L"remove" || cmd == L"search" || cmd == L"download");
    // 但 search 需要写注册表初始化变量，也需要提权
    if (needsAdmin && !ctx.isElevated) {
        // 但若命令指定了 --user，则不需提权（仅操作 HKCU）
        bool hasUserFlag = false;
        for (const auto& a : args) {
            if (a == L"--user") { hasUserFlag = true; break; }
        }
        if (cmd == L"remove" && hasUserFlag) {
            // 可以不提权
        } else if (cmd == L"search" && hasUserFlag) {
            // search 没有 --user 参数，所以不提
        } else {
            // 需要提权
            PrintInfo(L"此操作需要管理员权限，正在请求...");
            // 构建完整命令行（包括原参数）
            std::wstring cmdLine;
            for (int i = 1; i < argc; ++i) {
                if (i > 1) cmdLine += L' ';
                // 如果参数包含空格，加引号
                std::wstring arg = argv[i];
                if (arg.find(L' ') != std::wstring::npos)
                    cmdLine += L'"' + arg + L'"';
                else
                    cmdLine += arg;
            }
            if (ElevationHelper::RelaunchElevated(cmdLine)) {
                return 0; // 父进程退出
            } else {
                PrintError(L"提权失败，请手动以管理员身份运行");
                return 3;
            }
        }
    }

    // 执行命令
    auto* command = registry.findCommand(cmd);
    if (!command) {
        PrintError(L"未知命令: " + cmd);
        return 1;
    }

    try {
        int exitCode = command->execute(args, ctx);
        return exitCode;
    } catch (const std::exception& e) {
        PrintError(L"执行异常: " + std::wstring(e.what(), e.what() + strlen(e.what())));
        return 1;
    }
}
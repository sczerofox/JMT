#include "console/repl_engine.hpp"
#include "console/repl_utils.hpp"
#include "console/color_print.hpp"
#include "app/elevation_gate.hpp"
#include "system/utils.hpp"
#include <windows.h>
#include <iostream>
#include <csignal>

static volatile sig_atomic_t g_interrupted = 0;
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT) {
        g_interrupted = 1;
        return TRUE;
    }
    return FALSE;
}

ReplEngine::ReplEngine(CommandRegistry& registry, AppContext& ctx)
        : registry_(registry), ctx_(ctx) {}

void ReplEngine::run() {
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    printBanner();
    while (true) {
        g_interrupted = 0;
        std::wcout << L"jmt> " << std::flush;
        std::wstring line;
        std::getline(std::wcin, line);
        if (g_interrupted) {
            PrintInfo(L"^C");
            continue;
        }
        if (line.empty()) continue;
        auto tokens = ReplUtils::splitCommandLine(line);
        if (tokens.empty()) continue;
        const std::wstring& cmd = tokens[0];
        if (cmd == L"exit" || cmd == L"quit")
            break;
        auto* command = registry_.findCommand(cmd);
        if (!command) {
            PrintError(L"未知命令，输入 'help' 查看帮助");
            continue;
        }

        // 提权：与单次命令模式共用同一套元数据驱动逻辑
        const ElevationDecision decision =
                ElevationGate::ensure(command->requiresElevation(), tokens, ctx_);
        if (!decision.proceed) {
            if (decision.code != ExitCode::Ok) {
                PrintDebug(L"命令返回码: " + std::to_wstring(toInt(decision.code)));
            }
            continue;
        }

        try {
            const ExitCode code = command->execute(tokens, ctx_);
            if (code != ExitCode::Ok) {
                PrintDebug(L"命令返回码: " + std::to_wstring(toInt(code)));
            }
        } catch (const std::exception& e) {
            PrintError(L"执行异常: " + ToWideString(e.what()));
        }
    }
    PrintInfo(L"再见！");
}

void ReplEngine::printBanner() {
    PrintInfo(L"=====================================");
    PrintInfo(L"Java Manager Tool v1.7");
    PrintInfo(L"=====================================");
    PrintInfo(L"Usage:");
    PrintInfo(L"");
    PrintInfo(L"JDK Management:");
    PrintInfo(L"  search [--force]    Scan and auto-setup JDK");
    PrintInfo(L"  list                List installed Java versions");
    PrintInfo(L"  use <version>       Switch to a specific JDK version");
    PrintInfo(L"  download <version>  Download JDK from mirror/official and auto-setup (exe: download only)");
    PrintInfo(L"  remove              Remove JDK or clean env (env/all/temp/trash/<version>)");
    PrintInfo(L"  rollback <version>  Restore a deleted JDK from trash");
    PrintInfo(L"");
    PrintInfo(L"Environment:");
    PrintInfo(L"  env                 Register JMT directory in PATH");
    PrintInfo(L"  shell               Open new CMD with JMT environment");
    PrintInfo(L"");
    PrintInfo(L"Data:");
    PrintInfo(L"  data (output/input) Export JDK list or import and install JDKs");
    PrintInfo(L"    output            Export JDK list to .data\\ver_out.txt");
    PrintInfo(L"    input             Import and install JDKs from .data\\ver_out.txt");
    PrintInfo(L"");
    PrintInfo(L"Other:");
    PrintInfo(L"  version             Show JMT version");
    PrintInfo(L"  help [command]      Show help");
    PrintInfo(L"  exit                Exit interactive mode");
}

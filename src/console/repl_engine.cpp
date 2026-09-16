#include "console/repl_engine.hpp"
#include "console/repl_utils.hpp"
#include "platform/output.hpp"
#include "app/elevation_gate.hpp"
#include "common/cancel_token.hpp"
#include "system/utils.hpp"
#include <windows.h>
#include <iostream>
#include <csignal>

static volatile sig_atomic_t g_interrupted = 0;
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT) {
        g_interrupted = 1;
        globalCancelState().cancel();   // 正在下载/扫描时中断
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
            ctx_.out->line(OutputLevel::Info, L"^C");
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
            ctx_.out->line(OutputLevel::Error, L"未知命令，输入 'help' 查看帮助");
            continue;
        }

        // 提权前先做只读校验（与单次命令模式一致）
        if (const ExitCode preflight = command->preflight(tokens, ctx_); preflight != ExitCode::Ok) {
            ctx_.out->line(OutputLevel::Debug, L"命令返回码: " + std::to_wstring(toInt(preflight)));
            continue;
        }

        // 提权：与单次命令模式共用同一套元数据驱动逻辑
        const ElevationDecision decision =
                ElevationGate::ensure(command->requiresElevation(), command->allowsUserScope(), tokens, ctx_);
        if (!decision.proceed) {
            if (decision.code != ExitCode::Ok) {
                ctx_.out->line(OutputLevel::Debug, L"命令返回码: " + std::to_wstring(toInt(decision.code)));
            }
            continue;
        }

        try {
            const ExitCode code = command->execute(tokens, ctx_);
            if (code != ExitCode::Ok) {
                ctx_.out->line(OutputLevel::Debug, L"命令返回码: " + std::to_wstring(toInt(code)));
            }
        } catch (const std::exception& e) {
            ctx_.out->line(OutputLevel::Error, L"执行异常: " + ToWideString(e.what()));
        }
    }
    ctx_.out->line(OutputLevel::Info, L"再见！");
}

void ReplEngine::printBanner() {
    ctx_.out->line(OutputLevel::Info, L"=====================================");
    ctx_.out->line(OutputLevel::Info, L"Java Manager Tool v1.7");
    ctx_.out->line(OutputLevel::Info, L"=====================================");
    ctx_.out->line(OutputLevel::Info, L"Usage:");
    ctx_.out->line(OutputLevel::Info, L"");
    ctx_.out->line(OutputLevel::Info, L"JDK Management:");
    ctx_.out->line(OutputLevel::Info, L"  search              Scan and auto-setup JDK (--force/--user/--sys)");
    ctx_.out->line(OutputLevel::Info, L"  list                List installed Java versions");
    ctx_.out->line(OutputLevel::Info, L"  use <version>       Switch to a specific JDK version (--exact/--user/--sys)");
    ctx_.out->line(OutputLevel::Info, L"  download <version>  Download JDK and auto-setup (--mirror/exe/java/--user/--sys)");
    ctx_.out->line(OutputLevel::Info, L"  remove              Remove JDK or clean env (env/all/temp/trash/<version>)");
    ctx_.out->line(OutputLevel::Info, L"  rollback            Restore a deleted JDK from trash (list/<version>)");
    ctx_.out->line(OutputLevel::Info, L"");
    ctx_.out->line(OutputLevel::Info, L"Environment:");
    ctx_.out->line(OutputLevel::Info, L"  env                 Register JMT directory in PATH (--user/--sys)");
    ctx_.out->line(OutputLevel::Info, L"  shell               Open new CMD with JMT environment");
    ctx_.out->line(OutputLevel::Info, L"");
    ctx_.out->line(OutputLevel::Info, L"Data:");
    ctx_.out->line(OutputLevel::Info, L"  data (output/input) Export JDK list or import and install JDKs");
    ctx_.out->line(OutputLevel::Info, L"    output            Export JDK list to .data\\ver_out.txt");
    ctx_.out->line(OutputLevel::Info, L"    input             Import and install JDKs from .data\\ver_out.txt");
    ctx_.out->line(OutputLevel::Info, L"");
    ctx_.out->line(OutputLevel::Info, L"Other:");
    ctx_.out->line(OutputLevel::Info, L"  version             Show JMT version");
    ctx_.out->line(OutputLevel::Info, L"  help [command]      Show help");
    ctx_.out->line(OutputLevel::Info, L"  exit                Exit interactive mode");
}

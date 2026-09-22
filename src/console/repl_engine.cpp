#include "console/repl_engine.hpp"
#include "console/repl_utils.hpp"
#include "platform/output.hpp"
#include "app/elevation_gate.hpp"
#include "common/cancel_token.hpp"
#include "common/version.hpp"
#include "system/utils.hpp"
#include <windows.h>
#include <iostream>
#include <csignal>
#include <string>

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
        // 提示符前先换行：避免与上一条命令的输出（尤其是原地刷新的进度行）粘在同一行
        ctx_.out->clearProgress();
        std::wcout << L"\n" << L"jmt> " << std::flush;
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

        // 真正要执行命令了，先空一行：让输出与上面那行 "jmt> xxx" 分开，便于阅读
        ctx_.out->blank();

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
                ElevationGate::ensure(command->requiresElevation(), tokens, ctx_);
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
    // 版本行直接用应用名 + 版本号 + 构建日期；不再画上下两条分隔线，保持界面简洁
    ctx_.out->line(OutputLevel::Info,
                   std::wstring(jmt::kAppName) + L"  " + jmt::kVersion +
                           L"  ( build  " + jmt::kBuildDate + L" )");
    ctx_.out->blank();
    ctx_.out->line(OutputLevel::Info, L"Usage:");
    ctx_.out->line(OutputLevel::Info, L"");
    ctx_.out->line(OutputLevel::Info, L"JDK Management:");
    ctx_.out->line(OutputLevel::Info, L"  search              Scan and auto-setup JDK (--force)");
    ctx_.out->line(OutputLevel::Info, L"  list                List installed Java versions");
    ctx_.out->line(OutputLevel::Info, L"  use <version>       Switch to a specific JDK version (--exact)");
    ctx_.out->line(OutputLevel::Info, L"  download <version>  Download JDK and auto-setup (exe)");
    ctx_.out->line(OutputLevel::Info, L"  remove              Remove JDK or clean env (env/all/temp/trash/<version>)");
    ctx_.out->line(OutputLevel::Info, L"  rollback            Restore a deleted JDK from trash (list/<version>)");
    ctx_.out->line(OutputLevel::Info, L"");
    ctx_.out->line(OutputLevel::Info, L"Environment / Other:");
    ctx_.out->line(OutputLevel::Info, L"  env                 Register JMT directory in PATH");
    ctx_.out->line(OutputLevel::Info, L"  version             Show JMT version");
    ctx_.out->line(OutputLevel::Info, L"  help [command]      Show help");
    ctx_.out->line(OutputLevel::Info, L"  exit                Exit interactive mode");
}

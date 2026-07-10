#include "repl_engine.hpp"
#include "repl_utils.hpp"
#include "../print/color_print.hpp"
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

ReplEngine::ReplEngine(CommandRegistry& registry, JmtContext& ctx)
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
        try {
            int code = command->execute(tokens, ctx_);
            if (code != 0) {
                PrintDebug(L"命令返回码: " + std::to_wstring(code));
            }
        } catch (const std::exception& e) {
            PrintError(L"执行异常: " + std::wstring(e.what(), e.what() + strlen(e.what())));
        }
    }
    PrintInfo(L"再见！");
}

void ReplEngine::printBanner() {
    PrintInfo(L"=====================================");
    PrintInfo(L"Java Manager Tool v1.5");
    PrintInfo(L"=====================================");
    PrintInfo(L"Usage:");
    PrintInfo(L"  search [--force]    Scan all valid JDK");
    PrintInfo(L"  list                Show all installed Java");
    PrintInfo(L"  use <num>           Switch global Java version");
    PrintInfo(L"  env                 Add JMT dir to PATH");
    PrintInfo(L"  remove              Clear all JMT env config");
    PrintInfo(L"  download <num>      Download & install JDK");
    PrintInfo(L"  version             Show JMT version");
    PrintInfo(L"  shell               Open new CMD with JMT");
    PrintInfo(L"  help [cmd]          Show help");
    PrintInfo(L"  rollback <ver>      Restore a deleted JDK from trash");
    PrintInfo(L"  clean-trash         Permanently delete all trashed JDKs");
    PrintInfo(L"  exit                Exit interactive mode");
    PrintInfo(L"");
}

bool ReplEngine::handleCtrlC() {
    return false;
}

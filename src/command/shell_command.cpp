#include "command/shell_command.hpp"
#include "system/utils.hpp"
#include "console/color_print.hpp"
#include <windows.h>
#include <shellapi.h>

ExitCode ShellCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    // 获取 jmt.exe 的完整路径
    const std::wstring& exePath = ctx.paths.exePath;

    // 构建 cmd 命令行：/k 表示执行后保持窗口打开，并运行 jmt.exe（无参进入交互模式）
    std::wstring cmdLine = L"/k \"" + exePath + L"\"";

    // 使用 ShellExecuteW 以 open 方式启动 cmd.exe，创建新窗口
    HINSTANCE hInst = ShellExecuteW(
            nullptr,                 // 父窗口句柄
            L"open",                 // 操作动词
            L"cmd.exe",              // 要运行的程序
            cmdLine.c_str(),         // 命令行参数
            nullptr,                 // 工作目录（默认）
            SW_SHOWNORMAL            // 窗口显示方式
    );

    // ShellExecuteW 返回值大于 32 表示成功
    if ((INT_PTR)hInst <= 32) {
        DWORD err = GetLastError();
        PrintError(L"无法启动新终端，错误码: " + std::to_wstring(err));
        return ExitCode::BadArgs;
    }

    // ----- 新增：向当前控制台输入缓冲区发送 "exit\r\n" 命令 -----
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    if (hInput != INVALID_HANDLE_VALUE && hInput != nullptr) {
        // 构造按键事件序列：e x i t 回车
        const wchar_t* cmd = L"exit";
        std::vector<INPUT_RECORD> records;
        for (int j = 0; j < 4; ++j) {
            INPUT_RECORD irDown = {};
            irDown.EventType = KEY_EVENT;
            irDown.Event.KeyEvent.bKeyDown = TRUE;
            irDown.Event.KeyEvent.wRepeatCount = 1;
            irDown.Event.KeyEvent.wVirtualKeyCode = 0;
            irDown.Event.KeyEvent.wVirtualScanCode = 0;
            irDown.Event.KeyEvent.uChar.UnicodeChar = cmd[j];
            irDown.Event.KeyEvent.dwControlKeyState = 0;
            records.push_back(irDown);

            INPUT_RECORD irUp = irDown;
            irUp.Event.KeyEvent.bKeyDown = FALSE;
            records.push_back(irUp);
        }
        // 回车键（按下 + 释放）
        INPUT_RECORD irEnterDown = {};
        irEnterDown.EventType = KEY_EVENT;
        irEnterDown.Event.KeyEvent.bKeyDown = TRUE;
        irEnterDown.Event.KeyEvent.wRepeatCount = 1;
        irEnterDown.Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        irEnterDown.Event.KeyEvent.wVirtualScanCode = 0;
        irEnterDown.Event.KeyEvent.uChar.UnicodeChar = L'\r';
        irEnterDown.Event.KeyEvent.dwControlKeyState = 0;
        records.push_back(irEnterDown);

        INPUT_RECORD irEnterUp = irEnterDown;
        irEnterUp.Event.KeyEvent.bKeyDown = FALSE;
        records.push_back(irEnterUp);

        DWORD written = 0;
        WriteConsoleInputW(hInput, records.data(), static_cast<DWORD>(records.size()), &written);
    }

    PrintSuccess(L"已打开新终端，旧窗口即将执行 exit 命令关闭");
    return ExitCode::Ok;
}

#include "console/color_print.hpp"
#include <windows.h>
#include <iostream>

static HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
static bool useVT = false;

void InitConsole() {
    SetConsoleOutputCP(CP_UTF8);
    DWORD mode = 0;
    if (GetConsoleMode(hConsole, &mode)) {
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hConsole, mode);
        useVT = true;
    }
}

static void PrintColored(const std::wstring& msg, WORD attributes) {
    if (useVT) {
        std::wstring prefix;
        if (attributes == FOREGROUND_GREEN) {
            prefix = L"\033[32m";
        } else if (attributes == FOREGROUND_RED) {
            prefix = L"\033[31m";
        } else if (attributes == (FOREGROUND_RED | FOREGROUND_GREEN)) {
            prefix = L"\033[33m";  // 黄色
        } else {
            prefix = L"\033[37m";  // 白色（默认）
        }
        DWORD written;
        WriteConsoleW(hConsole, (prefix + msg + L"\033[0m\n").c_str(),
                      (DWORD)(prefix.size() + msg.size() + 5), &written, nullptr);
    } else {
        SetConsoleTextAttribute(hConsole, attributes);
        DWORD written;
        WriteConsoleW(hConsole, (msg + L"\n").c_str(), (DWORD)(msg.size() + 1), &written, nullptr);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
}

void PrintSuccess(const std::wstring& msg) {
    PrintColored(L"[SUCCESS] " + msg, FOREGROUND_GREEN);
}

void PrintError(const std::wstring& msg) {
    PrintColored(L"[ERROR] " + msg, FOREGROUND_RED);
}

void PrintInfo(const std::wstring& msg) {
    PrintColored(L"[INFO] " + msg, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void PrintWarning(const std::wstring& msg) {
    // 黄色 = 红色+绿色
    PrintColored(L"[WARN] " + msg, FOREGROUND_RED | FOREGROUND_GREEN);
}

void PrintDebug([[maybe_unused]] const std::wstring& msg) {
#ifdef _DEBUG
    PrintColored(L"[DEBUG] " + msg, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#endif
}

// src/print/color_print.cpp
// 在文件末尾添加
void PrintProgress(const std::wstring& msg) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(h, &info);
    COORD pos = {0, info.dwCursorPosition.Y};
    DWORD written;
    FillConsoleOutputCharacterW(h, L' ', info.dwSize.X, pos, &written);
    SetConsoleCursorPosition(h, pos);
    WriteConsoleW(h, msg.c_str(), (DWORD)msg.size(), &written, nullptr);
}
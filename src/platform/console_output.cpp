#include "platform/output.hpp"

#include <windows.h>

namespace {

// 与旧 color_print.cpp 中的常量保持一致
constexpr int kDefaultAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

HANDLE consoleHandle() {
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

struct TagAndAttributes {
    const wchar_t* tag;
    int attributes;
};

TagAndAttributes describe(OutputLevel level) {
    switch (level) {
        case OutputLevel::Success: return {L"[SUCCESS] ", FOREGROUND_GREEN};
        case OutputLevel::Error:   return {L"[ERROR] ", FOREGROUND_RED};
        case OutputLevel::Warning: return {L"[WARN] ", FOREGROUND_RED | FOREGROUND_GREEN};
        case OutputLevel::Debug:   return {L"[DEBUG] ", kDefaultAttributes};
        case OutputLevel::Info:
        default:                   return {L"[INFO] ", kDefaultAttributes};
    }
}

}  // namespace

void ConsoleOutput::init() {
    SetConsoleOutputCP(CP_UTF8);
    DWORD mode = 0;
    if (GetConsoleMode(consoleHandle(), &mode)) {
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(consoleHandle(), mode);
        useVT_ = true;
    }
}

void ConsoleOutput::writeLine(const wchar_t* tag, const std::wstring& text, int attributes) {
    const std::wstring body = std::wstring(tag) + text;
    HANDLE handle = consoleHandle();
    DWORD written = 0;

    if (useVT_) {
        std::wstring ansiPrefix;
        if (attributes == FOREGROUND_GREEN) {
            ansiPrefix = L"\033[32m";
        } else if (attributes == FOREGROUND_RED) {
            ansiPrefix = L"\033[31m";
        } else if (attributes == (FOREGROUND_RED | FOREGROUND_GREEN)) {
            ansiPrefix = L"\033[33m";
        } else {
            ansiPrefix = L"\033[37m";
        }
        // 与旧实现逐字节一致：颜色前缀 + 正文 + 复位 + 换行
        const std::wstring output = ansiPrefix + body + L"\033[0m\n";
        WriteConsoleW(handle, output.c_str(), static_cast<DWORD>(output.size()), &written, nullptr);
    } else {
        SetConsoleTextAttribute(handle, static_cast<WORD>(attributes));
        const std::wstring output = body + L"\n";
        WriteConsoleW(handle, output.c_str(), static_cast<DWORD>(output.size()), &written, nullptr);
        SetConsoleTextAttribute(handle, kDefaultAttributes);
    }
}

void ConsoleOutput::line(OutputLevel level, const std::wstring& text) {
#ifndef _DEBUG
    if (level == OutputLevel::Debug) return;   // 旧 PrintDebug 只在 Debug 构建输出
#endif

    const TagAndAttributes described = describe(level);
    writeLine(described.tag, text, described.attributes);
}

void ConsoleOutput::progress(const std::wstring& text) {
    HANDLE handle = consoleHandle();
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(handle, &info);
    const COORD position = {0, info.dwCursorPosition.Y};
    DWORD written = 0;
    FillConsoleOutputCharacterW(handle, L' ', info.dwSize.X, position, &written);
    SetConsoleCursorPosition(handle, position);
    WriteConsoleW(handle, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
}

void ConsoleOutput::clearProgress() {
    HANDLE handle = consoleHandle();
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(handle, &info);
    const COORD position = {0, info.dwCursorPosition.Y};
    DWORD written = 0;
    FillConsoleOutputCharacterW(handle, L' ', info.dwSize.X, position, &written);
    SetConsoleCursorPosition(handle, position);
}

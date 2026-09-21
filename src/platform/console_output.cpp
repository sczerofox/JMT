#include "platform/output.hpp"

#include <windows.h>

namespace {

// 与旧 color_print.cpp 中的常量保持一致
constexpr int kDefaultAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

HANDLE consoleHandle() {
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

// 句柄是否指向真实控制台（管道/文件重定向时为 false）
bool isConsoleHandle(HANDLE handle) {
    DWORD mode = 0;
    return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) != 0;
}

// 非控制台（重定向到文件/管道）时按 UTF-8 输出，便于脚本与测试读取
void writeUtf8(HANDLE handle, const std::wstring& text) {
    if (text.empty()) return;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return;
    std::string buffer(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        buffer.data(), bytes, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &written, nullptr);
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

    if (!isConsoleHandle(handle)) {
        writeUtf8(handle, body + L"\r\n");
        return;
    }

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
            const std::wstring output = ansiPrefix + body + L"\033[0m\r\n";
            WriteConsoleW(handle, output.c_str(), static_cast<DWORD>(output.size()), &written, nullptr);
        } else {
            SetConsoleTextAttribute(handle, static_cast<WORD>(attributes));
            const std::wstring output = body + L"\r\n";
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
    if (!isConsoleHandle(handle)) return;   // 重定向时进度行无意义

    // 拿不到缓冲区信息时不做光标操作，直接写一行，保证进度显示永远不会成为故障点
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(handle, &info)) {
        DWORD written = 0;
        WriteConsoleW(handle, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
        return;
    }
    const SHORT width = info.dwSize.X > 0 ? info.dwSize.X : 120;
    const COORD position = {0, info.dwCursorPosition.Y};
    DWORD written = 0;
    FillConsoleOutputCharacterW(handle, L' ', static_cast<DWORD>(width), position, &written);
    SetConsoleCursorPosition(handle, position);
    WriteConsoleW(handle, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
}

void ConsoleOutput::clearProgress() {
    HANDLE handle = consoleHandle();
    if (!isConsoleHandle(handle)) return;
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(handle, &info)) return;
    const SHORT width = info.dwSize.X > 0 ? info.dwSize.X : 120;
    const COORD position = {0, info.dwCursorPosition.Y};
    DWORD written = 0;
    FillConsoleOutputCharacterW(handle, L' ', static_cast<DWORD>(width), position, &written);
    SetConsoleCursorPosition(handle, position);
}

bool ConsoleOutput::supportsProgress() const {
    // stdout 被重定向（文件/管道）时无法原地刷新，此时调用方应改用普通行
    return progressEnabled_ && isConsoleHandle(GetStdHandle(STD_OUTPUT_HANDLE));
}

void ConsoleOutput::blank() {
    HANDLE handle = consoleHandle();
    if (!isConsoleHandle(handle)) {
        writeUtf8(handle, L"\r\n");
        return;
    }
    DWORD written = 0;
    const wchar_t* newline = L"\r\n";
    WriteConsoleW(handle, newline, 2, &written, nullptr);
}

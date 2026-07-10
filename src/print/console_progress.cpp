#include "console_progress.hpp"
#include <windows.h>

void ConsoleProgress::ShowScanning(const std::wstring& drive, int found) {
    wchar_t buffer[256];
    swprintf_s(buffer, L"Scanning %s ... found %d items", drive.c_str(), found);
    DWORD written;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(h, &info);
    // 清空当前行
    COORD pos = {0, info.dwCursorPosition.Y};
    FillConsoleOutputCharacterW(h, L' ', info.dwSize.X, pos, &written);
    SetConsoleCursorPosition(h, pos);
    WriteConsoleW(h, buffer, (DWORD)wcslen(buffer), &written, nullptr);
}

void ConsoleProgress::ShowDownloadProgress(int percent, const std::wstring& extra) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(h, &info);
    COORD pos = {0, info.dwCursorPosition.Y};
    DWORD written;
    FillConsoleOutputCharacterW(h, L' ', info.dwSize.X, pos, &written);
    SetConsoleCursorPosition(h, pos);
    wchar_t buffer[256];
    if (percent >= 0) {
        if (extra.empty())
            swprintf_s(buffer, L"下载进度: %d%%", percent);
        else
            swprintf_s(buffer, L"下载进度: %d%% (%s)", percent, extra.c_str());
    } else {
        swprintf_s(buffer, L"下载进度: %s", extra.c_str());
    }
    WriteConsoleW(h, buffer, (DWORD)wcslen(buffer), &written, nullptr);
}

void ConsoleProgress::ClearLine() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(h, &info);
    COORD pos = {0, info.dwCursorPosition.Y};
    DWORD written;
    FillConsoleOutputCharacterW(h, L' ', info.dwSize.X, pos, &written);
    SetConsoleCursorPosition(h, pos);
}
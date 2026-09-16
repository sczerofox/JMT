#include "console/console_progress.hpp"
#include <windows.h>

void ConsoleProgress::ClearLine() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(h, &info);
    COORD pos = {0, info.dwCursorPosition.Y};
    DWORD written;
    FillConsoleOutputCharacterW(h, L' ', info.dwSize.X, pos, &written);
    SetConsoleCursorPosition(h, pos);
}
#include "elevation_helper.hpp"
#include <windows.h>
#include <shellapi.h>

bool ElevationHelper::IsElevated() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;
    TOKEN_ELEVATION elevation;
    DWORD size = sizeof(TOKEN_ELEVATION);
    bool elevated = false;
    if (GetTokenInformation(hToken, TokenElevation, &elevation, size, &size)) {
        elevated = elevation.TokenIsElevated;
    }
    CloseHandle(hToken);
    return elevated;
}

bool ElevationHelper::RelaunchElevated(const std::wstring& commandLine) {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
        return false;

    // 使用 /c 执行命令，完成后自动关闭窗口（但仍保留 pause 等待按键）
    std::wstring cmdLine = L"/c \"" + std::wstring(exePath) + L" " + commandLine + L" & pause\"";
    HINSTANCE hInst = ShellExecuteW(nullptr, L"runas", L"cmd.exe", cmdLine.c_str(), nullptr, SW_SHOWNORMAL);
    return (INT_PTR)hInst > 32;
}

bool ElevationHelper::RelaunchElevatedAndWait(const std::wstring& commandLine, int& exitCode) {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
        return false;

    SHELLEXECUTEINFOW sei = { sizeof(SHELLEXECUTEINFOW) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = commandLine.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei))
        return false;
    if (sei.hProcess == nullptr)
        return false;

    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD dwExit = 0;
    GetExitCodeProcess(sei.hProcess, &dwExit);
    exitCode = static_cast<int>(dwExit);
    CloseHandle(sei.hProcess);
    return true;
}
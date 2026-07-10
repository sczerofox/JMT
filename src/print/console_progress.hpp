#pragma once
#include <string>

// 在控制台显示扫描进度（动态刷新）
class ConsoleProgress {
public:
    static void ShowScanning(const std::wstring& drive, int found);
    static void ShowDownloadProgress(int percent, const std::wstring& extra = L"");
    static void ClearLine();
};
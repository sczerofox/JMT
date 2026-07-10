#pragma once
#include <string>

class ElevationHelper {
public:
    static bool IsElevated();
    static bool RelaunchElevated(const std::wstring& commandLine);
    // 新增：以管理员权限启动并等待完成，返回退出码
    static bool RelaunchElevatedAndWait(const std::wstring& commandLine, int& exitCode);
};
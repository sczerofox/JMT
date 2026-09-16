#pragma once

#include <string>

// 提权端口：命令层不再直接调用 ShellExecuteW/runas。
// 注意：本头文件刻意不包含 windows.h。
class IElevator {
public:
    virtual ~IElevator() = default;

    virtual bool isElevated() = 0;
    virtual bool relaunchElevated(const std::wstring& commandLine) = 0;
};

// Win32 实现：当前转发到 ElevationHelper 的既有实现（skeleton/8 回收为唯一实现）
class WinElevator : public IElevator {
public:
    bool isElevated() override;
    bool relaunchElevated(const std::wstring& commandLine) override;
};

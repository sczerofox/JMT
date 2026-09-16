#pragma once
#include <string>

// 设置控制台输出代码页为 UTF-8，并启用虚拟终端（若支持）
void InitConsole();

// 彩色输出函数（内部使用 WriteConsoleW）
void PrintSuccess(const std::wstring& msg);
void PrintError(const std::wstring& msg);
void PrintInfo(const std::wstring& msg);
void PrintWarning(const std::wstring& msg);
// 调试输出（仅在 Debug 构建下生效）
void PrintDebug([[maybe_unused]] const std::wstring& msg);
// 新增：同一行输出进度（覆盖前一行）
void PrintProgress(const std::wstring& msg);
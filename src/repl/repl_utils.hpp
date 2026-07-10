#pragma once
#include <string>
#include <vector>

class ReplUtils {
public:
    // 解析命令行字符串为参数列表（支持引号）
    static std::vector<std::wstring> splitCommandLine(const std::wstring& line);
};
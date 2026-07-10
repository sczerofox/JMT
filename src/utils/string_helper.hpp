#pragma once
#include <string>
#include <vector>

class StringHelper {
public:
    // 拆分字符串（按分隔符），保留空项可选
    static std::vector<std::wstring> split(const std::wstring& str, wchar_t delim, bool keepEmpty = false);
    // 去除首尾空格
    [[maybe_unused]] static std::wstring trim(const std::wstring& str);
    // 忽略大小写比较
    static bool equalsIgnoreCase(const std::wstring& a, const std::wstring& b);
    // 将命令行字符串解析为参数列表（类似 CommandLineToArgvW）
    [[maybe_unused]] static std::vector<std::wstring> parseCommandLine(const std::wstring& cmdLine);
};
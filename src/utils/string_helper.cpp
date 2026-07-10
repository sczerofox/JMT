#include "string_helper.hpp"
#include <algorithm>
#include <cctype>

std::vector<std::wstring> StringHelper::split(const std::wstring& str, wchar_t delim, bool keepEmpty) {
    std::vector<std::wstring> result;
    size_t start = 0;
    size_t pos = str.find(delim);
    while (pos != std::wstring::npos) {
        if (keepEmpty || pos > start)
            result.push_back(str.substr(start, pos - start));
        start = pos + 1;
        pos = str.find(delim, start);
    }
    if (keepEmpty || start < str.size())
        result.push_back(str.substr(start));
    return result;
}

[[maybe_unused]] std::wstring StringHelper::trim(const std::wstring& str) {
    size_t start = str.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    size_t end = str.find_last_not_of(L" \t\r\n");
    return str.substr(start, end - start + 1);
}

bool StringHelper::equalsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(),
                      [](wchar_t c1, wchar_t c2) { return towlower(c1) == towlower(c2); });
}

// 简化版命令行解析（不处理引号嵌套，仅支持基本引号）
[[maybe_unused]] std::vector<std::wstring> StringHelper::parseCommandLine(const std::wstring& cmdLine) {
    std::vector<std::wstring> args;
    std::wstring current;
    bool inQuotes = false;
    for (wchar_t ch : cmdLine) {
        if (ch == L'"') {
            inQuotes = !inQuotes;
        } else if (ch == L' ' && !inQuotes) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty())
        args.push_back(current);
    return args;
}
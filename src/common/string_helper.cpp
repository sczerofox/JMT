#include "common/string_helper.hpp"
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

bool StringHelper::equalsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(),
                      [](wchar_t c1, wchar_t c2) { return towlower(c1) == towlower(c2); });
}
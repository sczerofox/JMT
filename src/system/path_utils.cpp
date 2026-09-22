#include "system/path_utils.hpp"
#include <algorithm>

std::wstring PathUtils::normalize(const std::wstring& path) {
    std::wstring res = path;
    // 去除末尾反斜杠
    while (!res.empty() && (res.back() == L'\\' || res.back() == L'/'))
        res.pop_back();
    // 转为小写
    std::transform(res.begin(), res.end(), res.begin(), ::towlower);
    return res;
}

bool PathUtils::arePathsEqual(const std::wstring& a, const std::wstring& b) {
    return normalize(a) == normalize(b);
}

std::vector<std::wstring> PathUtils::splitPath(const std::wstring& path) {
    std::vector<std::wstring> result;
    if (path.empty()) return result;
    size_t start = 0;
    size_t pos = path.find(L';');
    while (pos != std::wstring::npos) {
        if (pos > start)
            result.push_back(path.substr(start, pos - start));
        start = pos + 1;
        pos = path.find(L';', start);
    }
    if (start < path.size())
        result.push_back(path.substr(start));
    return result;
}

std::vector<std::wstring> PathUtils::removeEntries(const std::vector<std::wstring>& entries,
                                                   const std::wstring& toRemove) {
    std::vector<std::wstring> result;
    for (const auto& e : entries) {
        if (!arePathsEqual(e, toRemove))
            result.push_back(e);
    }
    return result;
}

std::vector<std::wstring> PathUtils::addUniqueEntry(const std::vector<std::wstring>& entries,
                                                    const std::wstring& newEntry) {
    for (const auto& e : entries) {
        if (arePathsEqual(e, newEntry))
            return entries; // 已存在，直接返回原列表
    }
    auto result = entries;
    result.push_back(newEntry);
    return result;
}

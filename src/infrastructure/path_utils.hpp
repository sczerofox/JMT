#pragma once
#include <string>
#include <vector>

class PathUtils {
public:
    // 标准化：去除末尾反斜杠，转为小写（用于比较）
    static std::wstring normalize(const std::wstring& path);
    // 比较两条路径是否相等（忽略大小写，忽略末尾斜杠）
    static bool arePathsEqual(const std::wstring& a, const std::wstring& b);
    // 拆分 PATH 字符串（按分号），保留原始项（包括 %VAR%）
    static std::vector<std::wstring> splitPath(const std::wstring& path);
    // 从条目列表中移除所有匹配项（标准化比较）
    static std::vector<std::wstring> removeEntries(const std::vector<std::wstring>& entries,
                                                   const std::wstring& toRemove);
    // 添加条目，若已存在（标准化比较）则不添加
    static std::vector<std::wstring> addUniqueEntry(const std::vector<std::wstring>& entries,
                                                    const std::wstring& newEntry);
};
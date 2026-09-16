#pragma once

#include <string>
#include <utility>
#include <vector>

// 已安装 JDK 的最小表示：版本（原样字符串）+ 安装路径
using VersionCandidate = std::pair<std::wstring, std::wstring>;

struct VersionMatch {
    bool found = false;
    bool ambiguous = false;                     // 查询命中多条，已按最高版本选定
    std::wstring version;                       // 选中的版本（原样）
    std::wstring path;
    std::vector<VersionCandidate> candidates;   // 全部命中项，按版本从高到低
};

// 按版本从高到低排序
std::vector<VersionCandidate> sortByVersionDesc(const std::vector<VersionCandidate>& installed);

// 在已安装列表里解析用户输入的版本：
//   1) 完整版本精确匹配（保持列表顺序）；
//   2) 别名/前缀匹配（17 / 17.0 / 8 / 8u202 / 1.8），命中多条时取最高版本并标记 ambiguous；
//   3) exactOnly 为 true 时只做第 1 步。
VersionMatch resolveVersion(const std::vector<VersionCandidate>& installed,
                            const std::wstring& query,
                            bool exactOnly = false);

// 列表中版本最高的一项（空列表返回空串）
std::wstring maxVersion(const std::vector<VersionCandidate>& installed);

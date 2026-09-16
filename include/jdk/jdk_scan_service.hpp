#pragma once
#include <string>
#include <vector>
#include <utility>

class JdkScanService {
public:
    // 扫描所有合法 JDK，若 force 为 false 则优先使用缓存（并自愈）  添加静默扫描功能
    static std::vector<std::pair<std::wstring, std::wstring>> scanJdks(
            bool force,
            const std::wstring& cachePath,
            bool silent = false
    );
    // 校验单个 JDK 路径是否合法
    static bool isValidJdk(const std::wstring& path);
    // 提取版本号
    static std::wstring extractVersion(const std::wstring& path);
    // 写入缓存
    static void writeCache(const std::vector<std::pair<std::wstring, std::wstring>>& jdks,
                           const std::wstring& cachePath);
};
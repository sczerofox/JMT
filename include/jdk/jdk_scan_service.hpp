#pragma once

#include <string>
#include <utility>
#include <vector>

#include "app/app_paths.hpp"
#include "platform/output.hpp"
#include "jdk/version_match.hpp"

class JdkScanService {
public:
    JdkScanService(const AppPaths& paths, IOutput& out) : paths_(paths), out_(out) {}

    // 扫描所有合法 JDK，若 force 为 false 则优先使用缓存（并自愈）
    std::vector<std::pair<std::wstring, std::wstring>> scanJdks(bool force, bool silent = false);

    // 写入缓存
    void writeCache(const std::vector<std::pair<std::wstring, std::wstring>>& jdks);

    // 纯函数：只依赖文件系统，不依赖上下文，测试可直接调用
    static bool isValidJdk(const std::wstring& path);
    static std::wstring extractVersion(const std::wstring& path);

    // 缓存格式（带 schema 标记；旧格式会被判为无效并要求重扫）
    static std::wstring serializeCache(const std::vector<VersionCandidate>& jdks);
    static bool parseCache(const std::wstring& content, std::vector<VersionCandidate>& out);

private:
    const AppPaths& paths_;
    IOutput& out_;
};

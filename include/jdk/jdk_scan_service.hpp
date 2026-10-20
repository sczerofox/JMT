#pragma once

#include <string>
#include <utility>
#include <vector>

#include "app/app_paths.hpp"
#include "platform/output.hpp"
#include "jdk/version_match.hpp"

// 一次目录检查的完整结论：用于扫描、缓存自愈和诊断输出。
// reason 只在 valid == false 时有意义（中文，可直接打印）。
struct JdkInfo {
    bool valid = false;
    std::wstring version;       // release 里的 JAVA_VERSION（原样，如 17.0.9 / 1.8.0_202）
    std::wstring implementor;   // release 里的 IMPLEMENTOR（可能为空，JDK 8 早期版本就没有）
    std::wstring reason;        // 不是合法 JDK 的原因
};

class JdkScanService {
public:
    JdkScanService(const AppPaths& paths, IOutput& out) : paths_(paths), out_(out) {}

    // 扫描所有合法 JDK，若 force 为 false 则优先使用缓存（并自愈）
    std::vector<std::pair<std::wstring, std::wstring>> scanJdks(bool force, bool silent = false);

    // 写入缓存
    void writeCache(const std::vector<std::pair<std::wstring, std::wstring>>& jdks);

    // 纯函数：只依赖文件系统，不依赖上下文，测试可直接调用。
    // inspect() 是唯一的判定入口：release + 运行时 + 编译工具 + JDK 专属镜像，
    // 只看 java.exe/javac.exe/lib 会把 IDE 自带的 JBR、残缺安装当成 JDK。
    static JdkInfo inspect(const std::wstring& path);
    static bool isValidJdk(const std::wstring& path);
    static std::wstring extractVersion(const std::wstring& path);

    // 缓存格式（带 schema 标记；旧格式会被判为无效并要求重扫）
    static std::wstring serializeCache(const std::vector<VersionCandidate>& jdks);
    static bool parseCache(const std::wstring& content, std::vector<VersionCandidate>& out);

private:
    const AppPaths& paths_;
    IOutput& out_;
};

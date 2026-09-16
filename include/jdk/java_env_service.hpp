#pragma once

#include <functional>
#include <string>
#include <vector>

#include "platform/output.hpp"
#include "platform/registry.hpp"

class JavaEnvService {
public:
    // 由「JDK 目录」解析真实版本（默认读该目录的 release 文件）；
    // 注入假实现即可在单测里验证「当前版本」判定。
    using VersionResolver = std::function<std::wstring(const std::wstring& jdkPath)>;

    JavaEnvService(IRegistry& registry, IOutput& out, VersionResolver resolver = nullptr)
            : registry_(registry), out_(out), resolver_(std::move(resolver)) {}

    // 设置当前使用的 JDK：删除所有已有 JDK bin 路径，添加新路径
    bool setCurrentJdk(const std::wstring& jdkPath, EnvTarget target = EnvTarget::Auto);
    // 清除当前 JDK：删除所有 JDK bin 路径，恢复 Oracle javapath
    bool clearCurrentJdk(EnvTarget target = EnvTarget::Auto);
    // 获取当前 JDK 版本号（从 PATH 中解析）
    std::wstring getCurrentVersion();
    // 移除 Oracle javapath（如果存在）
    void removeOracleJavaPath(EnvTarget target = EnvTarget::Auto);
    // 恢复 Oracle javapath（如果目录存在）
    void restoreOracleJavaPath(EnvTarget target = EnvTarget::Auto);

private:
    IRegistry& registry_;
    IOutput& out_;
    VersionResolver resolver_;
};

#pragma once

#include <string>
#include <vector>

#include "platform/output.hpp"
#include "platform/registry.hpp"

class JavaEnvService {
public:
    JavaEnvService(IRegistry& registry, IOutput& out) : registry_(registry), out_(out) {}

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
};

#pragma once
#include <string>
#include <vector>
#include "system/registry_operator.hpp"

class JavaEnvService {
public:
    // 设置当前使用的 JDK：删除所有已有 JDK bin 路径，添加新路径
    static bool setCurrentJdk(const std::wstring& jdkPath, EnvTarget target = EnvTarget::Auto);
    // 清除当前 JDK：删除所有 JDK bin 路径，恢复 Oracle javapath
    static bool clearCurrentJdk(EnvTarget target = EnvTarget::Auto);
    // 获取当前 JDK 版本号（从 PATH 中解析）
    static std::wstring getCurrentVersion();
    // 移除 Oracle javapath（如果存在）
    static void removeOracleJavaPath(EnvTarget target = EnvTarget::Auto);
    // 恢复 Oracle javapath（如果目录存在）
    static void restoreOracleJavaPath(EnvTarget target = EnvTarget::Auto);
};
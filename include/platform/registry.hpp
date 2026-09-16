#pragma once

#include <string>
#include <vector>

// 环境变量写入目标（原定义在 system/registry_operator.hpp，迁移到端口层）
enum class EnvTarget {
    Auto,       // 先系统，失败则用户
    SystemOnly,
    UserOnly
};

// 注册表环境变量端口：服务层只依赖本接口，测试可注入内存实现。
// 注意：本头文件刻意不包含 windows.h。
class IRegistry {
public:
    virtual ~IRegistry() = default;

    virtual std::wstring readEnv(const std::wstring& name, EnvTarget target = EnvTarget::Auto) = 0;
    virtual bool writeEnv(const std::wstring& name, const std::wstring& value,
                          EnvTarget target = EnvTarget::Auto) = 0;
    virtual bool deleteEnv(const std::wstring& name, EnvTarget target = EnvTarget::Auto) = 0;
    virtual std::vector<std::wstring> listEnvNames(EnvTarget target = EnvTarget::Auto) = 0;

    virtual std::wstring readPath(EnvTarget target) = 0;
    virtual bool writePath(const std::wstring& path, EnvTarget target) = 0;
};

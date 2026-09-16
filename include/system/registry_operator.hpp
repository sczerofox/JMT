#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include "platform/registry.hpp"

class RegistryOperator : public IRegistry {
public:
    // ---------- IRegistry：实例接口，供服务层注入使用 ----------
    std::wstring readEnv(const std::wstring& name, EnvTarget target = EnvTarget::Auto) override;
    bool writeEnv(const std::wstring& name, const std::wstring& value,
                  EnvTarget target = EnvTarget::Auto) override;
    bool deleteEnv(const std::wstring& name, EnvTarget target = EnvTarget::Auto) override;
    std::vector<std::wstring> listEnvNames(EnvTarget target = EnvTarget::Auto) override;
    std::wstring readPath(EnvTarget target) override;
    bool writePath(const std::wstring& path, EnvTarget target) override;

    // ---------- 既有静态 API（过渡期保留，skeleton/8 删除） ----------
    // 写入环境变量（键值对）
    static bool writeEnvString(const std::wstring& key, const std::wstring& value, EnvTarget target = EnvTarget::Auto);
    // 读取环境变量（返回空串表示不存在）
    static std::wstring readEnvString(const std::wstring& key, EnvTarget target = EnvTarget::Auto);
    // 删除环境变量
    static bool deleteEnvString(const std::wstring& key, EnvTarget target = EnvTarget::Auto);
    // 获取系统/用户 PATH 字符串（原始格式）
    static std::wstring getPath(EnvTarget target);
    // 设置 PATH
    static bool setPath(const std::wstring& path, EnvTarget target);
    static std::vector<std::wstring> enumerateEnvValueNames(EnvTarget target);

private:
    static HKEY getRootKey(EnvTarget target);
    static bool openEnvKey(HKEY& hKey, HKEY root, bool writeAccess);
    static bool writeStringValue(HKEY hKey, const std::wstring& valueName, const std::wstring& data);
    static bool readStringValue(HKEY hKey, const std::wstring& valueName, std::wstring& out);
    static bool deleteValue(HKEY hKey, const std::wstring& valueName);
};

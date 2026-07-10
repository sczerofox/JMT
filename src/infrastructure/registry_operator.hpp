#pragma once
#include <string>
#include <vector>
#include <windows.h>

enum class EnvTarget {
    Auto,       // 先系统，失败则用户
    SystemOnly,
    UserOnly
};

class RegistryOperator {
public:
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
    // 在现有类中添加以下静态方法
    static bool writeMultiString(const std::wstring& key, const std::vector<std::wstring>& values, EnvTarget target = EnvTarget::Auto);
    static std::vector<std::wstring> readMultiString(const std::wstring& key, EnvTarget target = EnvTarget::Auto);

private:
    static HKEY getRootKey(EnvTarget target);
    static bool openEnvKey(HKEY& hKey, HKEY root, bool writeAccess);
    static bool writeStringValue(HKEY hKey, const std::wstring& valueName, const std::wstring& data);
    static bool readStringValue(HKEY hKey, const std::wstring& valueName, std::wstring& out);
    static bool deleteValue(HKEY hKey, const std::wstring& valueName);
};
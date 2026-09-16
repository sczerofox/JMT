#pragma once

#include <string>
#include <vector>
#include <windows.h>

#include "platform/registry.hpp"

// IRegistry 的 Win32 适配器：读写 HKLM/HKCU 的
// SYSTEM\CurrentControlSet\Control\Session Manager\Environment，修改后广播 WM_SETTINGCHANGE。
class WinRegistry : public IRegistry {
public:
    std::wstring readEnv(const std::wstring& name, EnvTarget target = EnvTarget::Auto) override;
    bool writeEnv(const std::wstring& name, const std::wstring& value,
                  EnvTarget target = EnvTarget::Auto) override;
    bool deleteEnv(const std::wstring& name, EnvTarget target = EnvTarget::Auto) override;
    std::vector<std::wstring> listEnvNames(EnvTarget target = EnvTarget::Auto) override;
    std::wstring readPath(EnvTarget target) override;
    bool writePath(const std::wstring& path, EnvTarget target) override;

private:
    // 以下是各接口的实现细节，均按 target 决定尝试的系统/用户分支
    static bool writeEnvString(const std::wstring& key, const std::wstring& value, EnvTarget target);
    static std::wstring readEnvString(const std::wstring& key, EnvTarget target);
    static bool deleteEnvString(const std::wstring& key, EnvTarget target);
    static std::wstring getPath(EnvTarget target);
    static bool setPath(const std::wstring& path, EnvTarget target);
    static std::vector<std::wstring> enumerateEnvValueNames(EnvTarget target);

    static HKEY getRootKey(EnvTarget target);
    // 目标对应的环境变量子键：系统 = HKLM\SYSTEM\...\Session Manager\Environment，
    // 用户 = HKCU\Environment（注意 HKCU 下没有 SYSTEM\... 这条路径）
    static const wchar_t* envKeyPath(EnvTarget target);
    // Auto → {系统, 用户}；其余 → 单一目标
    static std::vector<EnvTarget> targetsFor(EnvTarget target);
    static bool openEnvKey(HKEY& hKey, EnvTarget target, bool writeAccess);
    static bool writeStringValue(HKEY hKey, const std::wstring& valueName, const std::wstring& data);
    static bool readStringValue(HKEY hKey, const std::wstring& valueName, std::wstring& out);
    static bool deleteValue(HKEY hKey, const std::wstring& valueName);
};

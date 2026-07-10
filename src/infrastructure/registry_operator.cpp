#include "registry_operator.hpp"
#include "../print/color_print.hpp" // for debug
#include <vector>
#include <thread>    // 用于 Sleep 或 std::this_thread::sleep_for，这里使用 Sleep

// ----- 新增：广播环境变更，带重试机制 -----
static void BroadcastEnvironmentChange() {
    DWORD_PTR result = 0;

    // 第一次尝试：超时 10 秒（原为 5 秒）
    LRESULT ret = SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            (LPARAM)L"Environment",
            SMTO_ABORTIFHUNG,
            10000,           // 10 秒
            &result
    );

    if (ret == 0) {
        // 若第一次失败（可能因某个窗口挂起超时），稍等后重试
        PrintDebug(L"环境变更广播第一次尝试失败，等待 500ms 后重试...");
        Sleep(500);

        ret = SendMessageTimeoutW(
                HWND_BROADCAST,
                WM_SETTINGCHANGE,
                0,
                (LPARAM)L"Environment",
                SMTO_ABORTIFHUNG,
                5000,          // 第二次 5 秒
                &result
        );

        if (ret == 0) {
            // 两次均失败，记录但不影响功能（注册表已经写入成功）
            PrintDebug(L"环境变更广播两次尝试均失败，可能部分窗口未响应");
        } else {
            PrintDebug(L"环境变更广播重试成功");
        }
    } else {
        PrintDebug(L"环境变更广播成功");
    }
}
// ----- 新增结束 -----

HKEY RegistryOperator::getRootKey(EnvTarget target) {
    switch (target) {
        case EnvTarget::SystemOnly: return HKEY_LOCAL_MACHINE;
        case EnvTarget::UserOnly:   return HKEY_CURRENT_USER;
        default: return HKEY_LOCAL_MACHINE; // 先尝试系统
    }
}

bool RegistryOperator::openEnvKey(HKEY& hKey, HKEY root, bool writeAccess) {
    REGSAM access = writeAccess ? KEY_READ | KEY_WRITE : KEY_READ;
    LONG ret = RegOpenKeyExW(root, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, access, &hKey);
    return (ret == ERROR_SUCCESS);
}

bool RegistryOperator::writeStringValue(HKEY hKey, const std::wstring& valueName, const std::wstring& data) {
    LONG ret = RegSetValueExW(hKey, valueName.c_str(), 0, REG_EXPAND_SZ,
                              (const BYTE*)data.c_str(), (DWORD)((data.size() + 1) * sizeof(wchar_t)));
    return (ret == ERROR_SUCCESS);
}

bool RegistryOperator::readStringValue(HKEY hKey, const std::wstring& valueName, std::wstring& out) {
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS)
        return false;
    if (type != REG_EXPAND_SZ && type != REG_SZ)
        return false;
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
    if (RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type, (BYTE*)buffer.data(), &size) != ERROR_SUCCESS)
        return false;
    out = buffer.data();
    return true;
}

bool RegistryOperator::deleteValue(HKEY hKey, const std::wstring& valueName) {
    return (RegDeleteValueW(hKey, valueName.c_str()) == ERROR_SUCCESS);
}

bool RegistryOperator::writeEnvString(const std::wstring& key, const std::wstring& value, EnvTarget target) {
    HKEY hKey;
    bool success = false;
    EnvTarget attempts[2] = { EnvTarget::SystemOnly, EnvTarget::UserOnly };
    int count = (target == EnvTarget::Auto) ? 2 : 1;
    int start = (target == EnvTarget::Auto) ? 0 : ((target == EnvTarget::SystemOnly) ? 0 : 1);
    for (int i = 0; i < count; ++i) {
        EnvTarget t = attempts[start + i];
        HKEY root = getRootKey(t);
        if (openEnvKey(hKey, root, true)) {
            if (writeStringValue(hKey, key, value)) {
                success = true;
                RegCloseKey(hKey);
                break;
            }
            RegCloseKey(hKey);
        }
        // 若失败且为Auto，继续尝试下一个
    }
    if (success) {
        // 替换原广播为增强版
        BroadcastEnvironmentChange();
    }
    return success;
}

std::wstring RegistryOperator::readEnvString(const std::wstring& key, EnvTarget target) {
    HKEY hKey;
    std::wstring out;
    EnvTarget attempts[2] = { EnvTarget::SystemOnly, EnvTarget::UserOnly };
    int count = (target == EnvTarget::Auto) ? 2 : 1;
    int start = (target == EnvTarget::Auto) ? 0 : ((target == EnvTarget::SystemOnly) ? 0 : 1);
    for (int i = 0; i < count; ++i) {
        EnvTarget t = attempts[start + i];
        HKEY root = getRootKey(t);
        if (openEnvKey(hKey, root, false)) {
            if (readStringValue(hKey, key, out)) {
                RegCloseKey(hKey);
                return out;
            }
            RegCloseKey(hKey);
        }
    }
    return L"";
}

bool RegistryOperator::deleteEnvString(const std::wstring& key, EnvTarget target) {
    HKEY hKey;
    bool success = false;
    EnvTarget attempts[2] = { EnvTarget::SystemOnly, EnvTarget::UserOnly };
    int count = (target == EnvTarget::Auto) ? 2 : 1;
    int start = (target == EnvTarget::Auto) ? 0 : ((target == EnvTarget::SystemOnly) ? 0 : 1);
    for (int i = 0; i < count; ++i) {
        EnvTarget t = attempts[start + i];
        HKEY root = getRootKey(t);
        if (openEnvKey(hKey, root, true)) {
            if (deleteValue(hKey, key)) {
                success = true;
                RegCloseKey(hKey);
                break;
            }
            RegCloseKey(hKey);
        }
    }
    if (success) {
        // 替换原广播为增强版
        BroadcastEnvironmentChange();
    }
    return success;
}

std::wstring RegistryOperator::getPath(EnvTarget target) {
    return readEnvString(L"PATH", target);
}

bool RegistryOperator::setPath(const std::wstring& path, EnvTarget target) {
    // setPath 内部调用 writeEnvString，writeEnvString 已包含广播，无需重复
    return writeEnvString(L"PATH", path, target);
}

std::vector<std::wstring> RegistryOperator::enumerateEnvValueNames(EnvTarget target) {
    std::vector<std::wstring> result;
    HKEY hKey;
    HKEY root = getRootKey(target);
    if (!openEnvKey(hKey, root, false)) {
        return result;
    }

    DWORD index = 0;
    wchar_t valueName[32768];
    DWORD nameSize = 32768;
    while (RegEnumValueW(hKey, index, valueName, &nameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        result.push_back(std::wstring(valueName, nameSize));
        nameSize = 32768;
        ++index;
    }
    RegCloseKey(hKey);
    return result;
}

bool RegistryOperator::writeMultiString(const std::wstring& key, const std::vector<std::wstring>& values, EnvTarget target) {
    std::vector<wchar_t> data;
    for (const auto& s : values) {
        data.insert(data.end(), s.begin(), s.end());
        data.push_back(L'\0');
    }
    data.push_back(L'\0');

    HKEY hKey;
    bool success = false;
    EnvTarget attempts[2] = { EnvTarget::SystemOnly, EnvTarget::UserOnly };
    int count = (target == EnvTarget::Auto) ? 2 : 1;
    int start = (target == EnvTarget::Auto) ? 0 : ((target == EnvTarget::SystemOnly) ? 0 : 1);
    for (int i = 0; i < count; ++i) {
        EnvTarget t = attempts[start + i];
        HKEY root = getRootKey(t);
        if (openEnvKey(hKey, root, true)) {
            LONG ret = RegSetValueExW(hKey, key.c_str(), 0, REG_MULTI_SZ,
                                      (const BYTE*)data.data(), (DWORD)(data.size() * sizeof(wchar_t)));
            RegCloseKey(hKey);
            if (ret == ERROR_SUCCESS) {
                success = true;
                break;
            }
        }
    }
    if (success) {
        BroadcastEnvironmentChange(); // 您的广播函数
    }
    return success;
}

std::vector<std::wstring> RegistryOperator::readMultiString(const std::wstring& key, EnvTarget target) {
    std::vector<std::wstring> result;
    HKEY hKey;
    EnvTarget attempts[2] = { EnvTarget::SystemOnly, EnvTarget::UserOnly };
    int count = (target == EnvTarget::Auto) ? 2 : 1;
    int start = (target == EnvTarget::Auto) ? 0 : ((target == EnvTarget::SystemOnly) ? 0 : 1);
    for (int i = 0; i < count; ++i) {
        EnvTarget t = attempts[start + i];
        HKEY root = getRootKey(t);
        if (openEnvKey(hKey, root, false)) {
            DWORD type = 0, size = 0;
            if (RegQueryValueExW(hKey, key.c_str(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_MULTI_SZ) {
                std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
                if (RegQueryValueExW(hKey, key.c_str(), nullptr, &type, (BYTE*)buffer.data(), &size) == ERROR_SUCCESS) {
                    const wchar_t* ptr = buffer.data();
                    while (*ptr) {
                        std::wstring s = ptr;
                        result.push_back(s);
                        ptr += s.size() + 1;
                    }
                }
            }
            RegCloseKey(hKey);
            if (!result.empty()) break;
        }
    }
    return result;
}
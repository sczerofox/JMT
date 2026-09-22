#include "platform/win_registry.hpp"

#include <vector>
#include <thread>    // 用于 Sleep 或 std::this_thread::sleep_for，这里使用 Sleep

namespace {

// 调试日志：走 Win32 调试通道（仅 Debug 构建输出），避免适配器依赖应用的输出端口
void debugLog([[maybe_unused]] const std::wstring& message) {
#ifdef _DEBUG
    OutputDebugStringW((L"[DEBUG] " + message + L"\n").c_str());
#endif
}

}  // namespace

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
        debugLog(L"环境变更广播第一次尝试失败，等待 500ms 后重试...");
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
            debugLog(L"环境变更广播两次尝试均失败，可能部分窗口未响应");
        } else {
            debugLog(L"环境变更广播重试成功");
        }
    } else {
        debugLog(L"环境变更广播成功");
    }
}
// ----- 新增结束 -----

HKEY WinRegistry::getRootKey(EnvTarget target) {
    switch (target) {
        case EnvTarget::SystemOnly: return HKEY_LOCAL_MACHINE;
        case EnvTarget::UserOnly:   return HKEY_CURRENT_USER;
        default: return HKEY_LOCAL_MACHINE; // 先尝试系统
    }
}

const wchar_t* WinRegistry::envKeyPath(EnvTarget target) {
    if (target == EnvTarget::UserOnly) {
        return L"Environment";
    }
    return L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
}

std::vector<EnvTarget> WinRegistry::targetsFor(EnvTarget target) {
    switch (target) {
        case EnvTarget::SystemOnly: return {EnvTarget::SystemOnly};
        case EnvTarget::UserOnly:   return {EnvTarget::UserOnly};
        default:                    return {EnvTarget::SystemOnly, EnvTarget::UserOnly};
    }
}

bool WinRegistry::openEnvKey(HKEY& hKey, EnvTarget target, bool writeAccess) {
    REGSAM access = writeAccess ? KEY_READ | KEY_WRITE : KEY_READ;
    HKEY root = getRootKey(target);
    const wchar_t* subKey = envKeyPath(target);
    if (RegOpenKeyExW(root, subKey, 0, access, &hKey) == ERROR_SUCCESS) {
        return true;
    }
    // 用户级环境变量键在极少数系统上可能不存在：写模式下尝试创建
    if (target == EnvTarget::UserOnly && writeAccess) {
        DWORD disposition = 0;
        return RegCreateKeyExW(root, subKey, 0, nullptr, 0, access, nullptr, &hKey, &disposition) == ERROR_SUCCESS;
    }
    return false;
}

bool WinRegistry::writeStringValue(HKEY hKey, const std::wstring& valueName, const std::wstring& data) {
    LONG ret = RegSetValueExW(hKey, valueName.c_str(), 0, REG_EXPAND_SZ,
                              (const BYTE*)data.c_str(), (DWORD)((data.size() + 1) * sizeof(wchar_t)));
    return (ret == ERROR_SUCCESS);
}

bool WinRegistry::readStringValue(HKEY hKey, const std::wstring& valueName, std::wstring& out) {
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

bool WinRegistry::deleteValue(HKEY hKey, const std::wstring& valueName) {
    return (RegDeleteValueW(hKey, valueName.c_str()) == ERROR_SUCCESS);
}

bool WinRegistry::writeEnvString(const std::wstring& key, const std::wstring& value, EnvTarget target) {
    bool success = false;
    for (EnvTarget t : targetsFor(target)) {
        HKEY hKey = nullptr;
        if (openEnvKey(hKey, t, true)) {
            if (writeStringValue(hKey, key, value)) {
                success = true;
                RegCloseKey(hKey);
                break;
            }
            RegCloseKey(hKey);
        }
        // 若失败且为 Auto，继续尝试下一个目标
    }
    if (success) {
        BroadcastEnvironmentChange();
    }
    return success;
}

std::wstring WinRegistry::readEnvString(const std::wstring& key, EnvTarget target) {
    std::wstring out;
    for (EnvTarget t : targetsFor(target)) {
        HKEY hKey = nullptr;
        if (openEnvKey(hKey, t, false)) {
            if (readStringValue(hKey, key, out)) {
                RegCloseKey(hKey);
                return out;
            }
            RegCloseKey(hKey);
        }
    }
    return L"";
}

bool WinRegistry::deleteEnvString(const std::wstring& key, EnvTarget target) {
    bool success = false;
    for (EnvTarget t : targetsFor(target)) {
        HKEY hKey = nullptr;
        if (openEnvKey(hKey, t, true)) {
            if (deleteValue(hKey, key)) {
                success = true;
                RegCloseKey(hKey);
                break;
            }
            RegCloseKey(hKey);
        }
    }
    if (success) {
        BroadcastEnvironmentChange();
    }
    return success;
}

std::wstring WinRegistry::getPath(EnvTarget target) {
    return readEnvString(L"PATH", target);
}

bool WinRegistry::setPath(const std::wstring& path, EnvTarget target) {
    // setPath 内部调用 writeEnvString，writeEnvString 已包含广播，无需重复
    return writeEnvString(L"PATH", path, target);
}

std::vector<std::wstring> WinRegistry::enumerateEnvValueNames(EnvTarget target) {
    std::vector<std::wstring> result;
    HKEY hKey = nullptr;
    if (!openEnvKey(hKey, target, false)) {
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


std::wstring WinRegistry::readEnv(const std::wstring& name, EnvTarget target) {
    return readEnvString(name, target);
}

bool WinRegistry::writeEnv(const std::wstring& name, const std::wstring& value, EnvTarget target) {
    return writeEnvString(name, value, target);
}

bool WinRegistry::deleteEnv(const std::wstring& name, EnvTarget target) {
    return deleteEnvString(name, target);
}

std::vector<std::wstring> WinRegistry::listEnvNames(EnvTarget target) {
    return enumerateEnvValueNames(target);
}

std::wstring WinRegistry::readPath(EnvTarget target) {
    return getPath(target);
}

bool WinRegistry::writePath(const std::wstring& path, EnvTarget target) {
    return setPath(path, target);
}

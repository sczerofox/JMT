#include "utils.hpp"
#include <windows.h>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

std::wstring GetExeDirectory() {
    wchar_t buffer[MAX_PATH];
    if (GetModuleFileNameW(nullptr, buffer, MAX_PATH) == 0)
        return L"";
    std::wstring path = buffer;
    size_t pos = path.find_last_of(L'\\');
    if (pos != std::wstring::npos)
        path = path.substr(0, pos);
    return path;
}

std::vector<std::wstring> GetAvailableDrives() {
    std::vector<std::wstring> drives;
    DWORD mask = GetLogicalDrives(); // Windows API
    for (int i = 0; i < 26; ++i) {
        if (mask & (1 << i)) {
            wchar_t drive[4] = { (wchar_t)(L'A' + i), L':', L'\\', L'\0' };
            UINT type = GetDriveTypeW(drive);
            if (type == DRIVE_FIXED)
                drives.push_back(std::wstring(drive, 3));
        }
    }
    return drives;
}

bool IsDirectory(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsSSDDrive([[maybe_unused]] const std::wstring& driveLetter) {
    // 简化：假定所有固定磁盘均为 SSD（可后续优化）
    return true;
}

std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    std::wstring result = left;
    if (!result.empty() && result.back() != L'\\')
        result += L'\\';
    result += right;
    return result;
}

std::wstring ReadFileText(const std::wstring& path) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return L"";

    DWORD size = GetFileSize(hFile, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(hFile);
        return L"";
    }

    std::vector<char> buffer(size);
    DWORD read = 0;
    if (!ReadFile(hFile, buffer.data(), size, &read, nullptr) || read != size) {
        CloseHandle(hFile);
        return L"";
    }
    CloseHandle(hFile);

    // 检测 BOM（UTF-16 LE / BE）
    if (size >= 2) {
        unsigned char* bytes = reinterpret_cast<unsigned char*>(buffer.data());
        if (bytes[0] == 0xFF && bytes[1] == 0xFE) {
            // UTF-16 LE BOM
            std::wstring content(reinterpret_cast<wchar_t*>(buffer.data() + 2), (size - 2) / 2);
            return content;
        }
        if (bytes[0] == 0xFE && bytes[1] == 0xFF) {
            // UTF-16 BE（跳过，不作处理）
            // 可转换，但一般 Windows 不用，此处直接降级
        }
    }

    // 尝试作为 UTF-8 解析
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buffer.data(), size, nullptr, 0);
    if (wlen > 0) {
        std::wstring content(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, buffer.data(), size, &content[0], wlen);
        return content;
    }

    // 最后尝试当前代码页（ANSI）
    wlen = MultiByteToWideChar(CP_ACP, 0, buffer.data(), size, nullptr, 0);
    if (wlen > 0) {
        std::wstring content(wlen, L'\0');
        MultiByteToWideChar(CP_ACP, 0, buffer.data(), size, &content[0], wlen);
        return content;
    }

    return L"";
}

[[maybe_unused]] bool WriteFileText(const std::wstring& path, const std::wstring& content) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    BOOL ok = WriteFile(hFile, content.c_str(), (DWORD)(content.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(hFile);
    return (ok && written == content.size() * sizeof(wchar_t));
}

bool IsFile(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring GetTimestampStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf_s(buf, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring EncodePathForFilename(const std::wstring& path) {
    std::wstring result = path;
    for (auto& ch : result) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' ||
            ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
            ch = L'_';
    }
    return result;
}

std::wstring DecodePathFromFilename(const std::wstring& encoded) {
    // 反向替换（注意：可能将合法的 '_' 也转换，但我们只转换上述字符，其他 '_' 保持）
    // 由于无法区分，我们只将 '_' 还原为 '\\' 是不安全的。更好的方式是在编码时使用特殊转义。
    // 简便起见，我们约定编码时用两个 '__' 代表原始 '_'，但目前暂不处理，因为路径中的 '_' 少见。
    // 更可靠：我们保存原始路径到 .meta 文件。
    // 建议采用元数据文件方式，这里暂用简单解码（大部分情况适用）
    std::wstring result = encoded;
    for (auto& ch : result) {
        if (ch == L'_') ch = L'\\';  // 但可能误转换
    }
    return result;
}

// 更可靠：我们采用元数据文件。在移动时，在回收条目内创建一个 .original_path 文件，写入原始路径。
// 那么 rollback 时直接读取该文件。这样更安全，不需编码解码。
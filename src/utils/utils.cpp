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

    auto* bytes = reinterpret_cast<unsigned char*>(buffer.data());

    // 检测 BOM（UTF-16 LE / BE）
    if (size >= 2) {
        if (bytes[0] == 0xFF && bytes[1] == 0xFE) {
            // UTF-16 LE BOM
            return std::wstring(reinterpret_cast<wchar_t*>(buffer.data() + 2), (size - 2) / 2);
        }
        if (bytes[0] == 0xFE && bytes[1] == 0xFF) {
            // UTF-16 BE — 罕见，不作处理
        }
    }

    // 启发式检测 UTF-16 LE 无 BOM（偶数大小，且奇数字节大部分为 0x00）
    if (size >= 4 && (size % 2 == 0)) {
        int nullCount = 0;
        int checkCount = (std::min)(static_cast<DWORD>(size / 2), static_cast<DWORD>(64));
        for (int i = 0; i < checkCount; ++i) {
            if (bytes[i * 2 + 1] == 0x00) ++nullCount;
        }
        if (nullCount > checkCount * 3 / 4) {
            // 高度疑似 UTF-16 LE
            return std::wstring(reinterpret_cast<wchar_t*>(buffer.data()), size / 2);
        }
    }

    // 尝试作为 UTF-8 解析
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buffer.data(), size, nullptr, 0);
    if (wlen > 0) {
        std::wstring content(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, buffer.data(), size, &content[0], wlen);
        // 去掉 UTF-8 BOM 字符（U+FEFF）
        if (!content.empty() && content[0] == 0xFEFF) {
            content.erase(0, 1);
        }
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

    // 写入 UTF-8 BOM + UTF-8 编码的内容，便于跨工具查看
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    WriteFile(hFile, bom, sizeof(bom), &written, nullptr);

    int utf8Size = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), (int)content.size(), nullptr, 0, nullptr, nullptr);
    if (utf8Size > 0) {
        std::vector<char> utf8Buf(utf8Size);
        WideCharToMultiByte(CP_UTF8, 0, content.c_str(), (int)content.size(), utf8Buf.data(), utf8Size, nullptr, nullptr);
        WriteFile(hFile, utf8Buf.data(), utf8Size, &written, nullptr);
    }

    CloseHandle(hFile);
    return true;
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

// 已被 [[deprecated]] — 该转换是有损的（无法区分原始路径中的 _ 和编码后的 _）
// 代码库已改用 .original_path 元数据文件记录原始路径，不再依赖此函数
[[deprecated]] std::wstring DecodePathFromFilename(const std::wstring& encoded) {
    std::wstring result = encoded;
    for (auto& ch : result) {
        if (ch == L'_') ch = L'\\';
    }
    return result;
}

std::wstring ToWideString(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), &result[0], len);
    return result;
}
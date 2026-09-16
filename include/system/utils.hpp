#pragma once
#include <string>
#include <vector>

std::wstring GetExeDirectory();
std::vector<std::wstring> GetAvailableDrives();  // 改名
bool IsDirectory(const std::wstring& path);
std::wstring JoinPath(const std::wstring& left, const std::wstring& right);
std::wstring ReadFileText(const std::wstring& path);
bool IsFile(const std::wstring& path);
bool WriteFileText(const std::wstring& path, const std::wstring& content);

// 将窄字符串（ANSI 编码）正确转换为宽字符串，避免逐字节扩展导致乱码
std::wstring ToWideString(const std::string& str);
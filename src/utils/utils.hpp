#pragma once
#include <string>
#include <vector>

std::wstring GetExeDirectory();
std::vector<std::wstring> GetAvailableDrives();  // 改名
bool IsDirectory(const std::wstring& path);
bool IsSSDDrive([[maybe_unused]] const std::wstring& driveLetter);
std::wstring JoinPath(const std::wstring& left, const std::wstring& right);
std::wstring ReadFileText(const std::wstring& path);
bool IsFile(const std::wstring& path);
// 获取当前时间戳字符串（格式：YYYYMMDD_HHMMSS）
std::wstring GetTimestampStr();
// 将路径中的非法字符（\ : / * ? " < > |）转换为 '_'
std::wstring EncodePathForFilename(const std::wstring& path);
// 将编码后的文件名还原为原始路径（反向替换，有损转换，不建议使用）
[[deprecated("Use .original_path metadata files instead of encoding/decoding paths")]]
std::wstring DecodePathFromFilename(const std::wstring& encoded);

[[maybe_unused]] bool WriteFileText(const std::wstring& path, const std::wstring& content);

// 将窄字符串（ANSI 编码）正确转换为宽字符串，避免逐字节扩展导致乱码
std::wstring ToWideString(const std::string& str);
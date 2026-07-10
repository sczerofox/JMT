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
// 将编码后的文件名还原为原始路径（反向替换）
std::wstring DecodePathFromFilename(const std::wstring& encoded);
// 解析回收站条目名称，返回 pair<版本号, 原始路径>，若解析失败返回空
std::pair<std::wstring, std::wstring> ParseTrashEntryName(const std::wstring& dirName);
// 获取回收站根目录
std::wstring GetTrashRoot(const std::wstring& exeDir);

[[maybe_unused]] bool WriteFileText(const std::wstring& path, const std::wstring& content);
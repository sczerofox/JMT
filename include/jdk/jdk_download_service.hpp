#pragma once
#include <string>
#include <vector>
#include <map>

class JdkDownloadService {
public:
    static std::wstring downloadAndInstall(const std::wstring& version,
                                           const std::wstring& installRoot = L"");
    static std::wstring downloadFromMirror(const std::wstring& version,
                                           const std::wstring& installRoot = L"");
    // 在 public 部分添加
    static std::wstring downloadFromOfficial(const std::wstring& version,
                                             const std::wstring& installRoot = L"");

    // 重新加载外部映射文件
    static void reloadMappings();

private:
    using UrlList = std::vector<std::wstring>;
    static std::map<std::wstring, UrlList> zipMap_;
    static std::map<std::wstring, UrlList> exeMap_;

    // 加载内置映射
    static void initBuiltinMappings();
    // 加载外部 .repo 文件
    static void loadExternalMappings();
    // 确保外部映射文件存在（若不存在则从内置生成）
    static void ensureExternalMappingFiles();

    // 查找某个版本的 zip URL（返回第一个可用的）
    static std::wstring findZipUrl(const std::wstring& version);
    // 查找某个版本的 exe URL
    static std::wstring findExeUrl(const std::wstring& version);
    // 判断 URL 是否为 demo 包
    static bool isDemoPackage(const std::wstring& url);
};
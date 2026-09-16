#pragma once

#include <map>
#include <string>
#include <vector>

#include "app/app_paths.hpp"
#include "network/host_throttle.hpp"
#include "platform/output.hpp"
#include "jdk/download_plan.hpp"

class JdkDownloadService {
public:
    JdkDownloadService(const AppPaths& paths, IOutput& out, HostThrottle& throttle)
            : paths_(paths), out_(out), throttle_(throttle) {}

    // 默认策略：镜像 ZIP → 镜像 EXE → 官方 ZIP；返回安装目录 / L"EXE_DOWNLOADED" / 空串
    std::wstring downloadAndInstall(const std::wstring& version, const std::wstring& installRoot = L"");
    // 仅镜像源：ZIP → EXE
    std::wstring downloadFromMirror(const std::wstring& version, const std::wstring& installRoot = L"");
    // 仅官方 Adoptium 源
    std::wstring downloadFromOfficial(const std::wstring& version, const std::wstring& installRoot = L"");
    // exe 参数：只下载 EXE 安装包到 .temp，不自动安装
    std::wstring downloadInstallerOnly(const std::wstring& version);

    // 重新加载内置 + 外部映射（启动时调用一次）
    void reloadMappings();

private:
    using UrlList = std::vector<std::wstring>;

    std::map<std::wstring, UrlList> zipMap_;
    std::map<std::wstring, UrlList> exeMap_;
    const AppPaths& paths_;
    IOutput& out_;
    HostThrottle& throttle_;   // 镜像友好：并发/间隔/预算/退避/429 拉黑

    // 目录与临时文件（此前散落在各处的 GetExeDirectory() + 字面量拼接）
    std::wstring tempDirectory();
    std::wstring repoDirectory();
    std::wstring tempDownloadPath(const std::wstring& ext, const std::wstring& url);

    // 下载与解压
    bool downloadFileWithCurl(const std::wstring& url, const std::wstring& destPath, int& outStatus);
    bool downloadFileWithMultiThread(const std::wstring& url, const std::wstring& destPath, int& outStatus);
    bool downloadFile(const std::wstring& url, const std::wstring& destPath, int& outStatus);
    bool extractZip(const std::wstring& zipPath, const std::wstring& destDir);
    bool isValidZipFile(const std::wstring& path);
    bool fixNestedJdkDirectory(const std::wstring& targetDir);

    // 映射管理
    void initBuiltinMappings();
    void loadExternalMappings();
    void ensureExternalMappingFiles();
    const UrlList& zipUrlsFor(const std::wstring& version);
    const UrlList& exeUrlsFor(const std::wstring& version);

    // 计划执行：按 DownloadPlan 逐步尝试，成功即返回
    std::wstring executePlan(const DownloadPlan& plan,
                             const std::wstring& version,
                             const std::wstring& installRoot);
    bool tryZipSource(const std::wstring& url, const std::wstring& version, const std::wstring& targetDir);
    std::wstring tryExeSource(const std::wstring& url, const std::wstring& version, const std::wstring& targetDir);
    bool tryOfficialZip(const std::wstring& version, const std::wstring& targetDir);
    // 完整版本请求时校验安装结果与请求是否一致（不一致则删除并放弃该源）
    bool versionSatisfied(const std::wstring& targetDir, const std::wstring& requested);

    // 官方源（Adoptium API，手动解析 307 重定向）
    bool officialDownloadInfo(const std::wstring& version,
                              std::wstring& outFinalUrl,
                              std::wstring& outFileName);
};

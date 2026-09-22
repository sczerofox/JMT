#pragma once

#include <string>
#include <vector>

#include "app/app_paths.hpp"
#include "network/host_throttle.hpp"
#include "platform/output.hpp"
#include "jdk/download_sources.hpp"

class JdkDownloadService {
public:
    JdkDownloadService(const AppPaths& paths, IOutput& out, HostThrottle& throttle)
            : paths_(paths), out_(out), throttle_(throttle) {}

    // 下载并安装 JDK。
    //   有 ZIP：下载 → 校验 → 解压 → 自动安装，返回安装目录
    //   只有安装包（EXE/MSI）：下载到 .temp，返回 L"EXE_DOWNLOADED" 由命令层提示手动安装
    //   全部失败：返回空串
    // 交互式终端里会先列出可用源让用户选择（回车取第 1 个），非交互环境按顺序自动尝试。
    std::wstring downloadAndInstall(const std::wstring& version);

private:
    // 探测结论
    enum class ProbeResult { Empty, Html, Zip, Installer, TooSmall };

    const AppPaths& paths_;
    IOutput& out_;
    HostThrottle& throttle_;   // 镜像友好：并发/间隔/预算/退避/429 拉黑

    std::wstring tempDirectory();
    std::wstring tempDownloadPath(const std::wstring& ext, const std::wstring& url);

    // 轻量探测：Range 取文件头 4 字节，用 Content-Type + 魔数判定真假
    // （镜像站的「浏览器校验页」常见形态是 HTTP 200 + text/html，只看状态码会误判）
    ProbeResult probeUrl(const std::wstring& url, bool wantZip);
    int64_t lastProbeContentLength() const { return lastProbeLength_; }

    // 下载（curl 为主，失败降级多线程）
    bool downloadFileWithCurl(const std::wstring& url, const std::wstring& destPath,
                              int& outStatus, int64_t& outBytes, int& outSpeedBps);
    bool downloadFileWithMultiThread(const std::wstring& url, const std::wstring& destPath,
                                     int& outStatus, int64_t& outBytes, int& outSpeedBps);
    bool downloadFile(const std::wstring& url, const std::wstring& destPath,
                      int& outStatus, int64_t& outBytes, int& outSpeedBps);

    // 解压与校验
    bool extractZip(const std::wstring& zipPath, const std::wstring& destDir);
    bool isValidZipFile(const std::wstring& path);
    bool isValidInstallerFile(const std::wstring& path);
    bool fixNestedJdkDirectory(const std::wstring& targetDir);

    // Adoptium metadata：拿该主版本最新的 jdk 产物文件名与官方直链。
    // 文件名是权威的（含真实补丁号），各 Adoptium 镜像目录与之一致，可直接复用。
    bool officialDownloadInfo(const std::wstring& version,
                              std::wstring& outFileName,
                              std::wstring& outDirectUrl);

    // 单个候选：探测 → 下载 → （ZIP）解压安装
    bool tryZipStep(const DownloadStep& step, const std::wstring& version,
                    const std::wstring& targetDir);
    // 单个安装包候选：探测 → 下载到 .temp
    bool tryManualStep(const DownloadStep& step, const std::wstring& version,
                       std::wstring& outDownloadedPath);

    // 列出可用源并让用户选（返回 1 起的编号；回车取 1；非交互环境返回 0 = 按顺序全部尝试）
    int chooseSource(const DownloadPlan& plan, const std::wstring& version);
    // 完整版本请求时校验安装结果与请求是否一致（不一致则删除并放弃该源）
    bool versionSatisfied(const std::wstring& targetDir, const std::wstring& requested);

    int64_t lastProbeLength_ = 0;
};

#pragma once

#include <string>
#include <vector>

// 下载策略（对应命令行开关）
enum class DownloadMode {
    Default,       // 镜像 ZIP → 镜像 EXE → 官方 ZIP
    MirrorOnly,    // 只走镜像：ZIP → EXE
    OfficialOnly   // 只走官方 Adoptium
};

// 单个尝试步骤
enum class DownloadStepKind { MirrorZip, MirrorExe, OfficialZip };

struct DownloadStep {
    DownloadStepKind kind;
    std::wstring url;   // OfficialZip 为空：运行时通过 Adoptium API 解析
};

// 下载计划：把「用哪些源、按什么顺序、跳过什么」从执行逻辑中拆出来，
// 使源选择可以脱离网络单独测试。
struct DownloadPlan {
    std::vector<DownloadStep> steps;             // 按尝试顺序
    std::vector<std::wstring> skippedDemoUrls;   // 因 demo 包被跳过的 URL（用于提示）
    bool installerOnly = false;                  // exe 参数：只下载 EXE 安装包，不自动安装

    // zipUrls/exeUrls：该版本可用的源列表（内置在前、外部追加）
    static DownloadPlan build(DownloadMode mode,
                              bool installerOnly,
                              const std::vector<std::wstring>& zipUrls,
                              const std::vector<std::wstring>& exeUrls);
};

// URL 是否为 demo 包（仅含示例代码，不能作为 JDK 使用）
bool isDemoUrl(const std::wstring& url);

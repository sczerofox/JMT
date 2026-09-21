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

// 源的友好名称：优先按主机名映射（华为云 / 南京大学 / 清华 TUNA / …），
// 未知主机退化为主机名本身；官方源与本地文件另有专门名称。
std::wstring sourceDisplayName(const std::wstring& url);

// 官方源（Adoptium API，运行时解析真实下载地址）的展示名
inline const wchar_t* kOfficialSourceName = L"Adoptium 官方源";

// 源分组键：同一主机算同一个源（这样「华为云镜像」的多条链接会归到一条里）；
// 官方源（url 为空）单独一组。
std::wstring sourceKey(const std::wstring& url);

// 按请求的版本过滤下载源：
//   主版本请求（17）→ 返回全部候选；
//   完整版本请求（17.0.2 / 8u202）→ 只保留 URL 里包含该版本串的源（可能为空）。
std::vector<std::wstring> filterUrlsForVersion(const std::vector<std::wstring>& urls,
                                               const std::wstring& requested);

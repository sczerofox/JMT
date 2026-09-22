#pragma once

#include <string>
#include <vector>

// 下载源清单与候选链接构建。
//
// 全流程：
//   1. 向 Adoptium 的 metadata API 问「该主版本最新的 jdk 产物文件名」（同时给出官方直链）
//   2. 用该文件名按各源的布局拼候选链接（布局因源而异，见 SourceKind）
//   3. 按主机归并成「源」列表展示给用户选择（非交互环境按顺序自动尝试）
//   4. 逐个候选探测（Range 取 4 字节验魔数；镜像站的校验页是 HTTP 200 + HTML，只看状态码会误判）
//   5. ZIP 通过 → 下载 → 解压自动安装；只有安装包（EXE/MSI）→ 下载后提示手动安装
//
// 实测覆盖（2026-09）：
//   南大 / 清华       Adoptium 全量镜像：8、11、16+（各版本仅最新补丁）
//   华为云 GA         按大版本直连：12~26（初始 GA 包）
//   华为云旧库 EXE    仅安装程序，覆盖 6、7、8、9、10（8 同时有 ZIP）
//   官方              GitHub Releases，最后兜底

// Adoptium 不发布的老版本只能走安装包
inline constexpr const wchar_t* kOfficialSourceName = L"Adoptium 官方源";
inline constexpr const wchar_t* kHuaweiLegacyName = L"华为云镜像（Oracle 旧版 EXE）";

// 源类型：决定链接怎么拼
enum class SourceKind {
    Adoptium,        // <base>/<主版本>/jdk/x64/windows/<官方文件名>
    HuaweiGa,       // <base>/<主版本>/openjdk-<主版本>_windows-x64_bin.zip
    HuaweiLegacyExe // 旧库安装程序（链接固定在表里）
};

struct MirrorSource {
    const wchar_t* name;
    const wchar_t* baseUrl;   // Adoptium 型为站点 adoptium 根；GA 型为 openjdk 根；EXE 型不用
    SourceKind kind;
};

const std::vector<MirrorSource>& mirrorSources();

// 各源覆盖面（分开判断：范围不同，不能共用一个布尔量）
bool adoptiumHasZip(const std::wstring& majorVersion);   // 8 / 11 / 16+
bool huaweiGaHasZip(const std::wstring& majorVersion);   // 12 ~ 26

// 该版本是否有可下载的压缩包（可自动安装）
bool hasZipCandidate(const std::wstring& majorVersion);
// 该版本是否有可下载的安装包（需手动安装）：6 / 7 / 8 / 9 / 10
bool hasManualInstaller(const std::wstring& majorVersion);
// 华为云旧库安装程序直链（表里没有则返回空）
std::wstring huaweiLegacyExeUrl(const std::wstring& majorVersion);

// 从 Adoptium 产物文件名取主版本：OpenJDK21U-..._21.0.12.1_1.zip → 21
std::wstring majorVersionFromFileName(const std::wstring& fileName);

// 制品类型
enum class DownloadStepKind {
    Zip,   // 下载后自动解压安装
    Exe    // 下载后提示手动安装
};

struct DownloadStep {
    DownloadStepKind kind;
    std::wstring sourceName;   // 所属源的展示名
    std::wstring url;
};

// 一个「源」：同一主机下的全部候选链接（用户选择与展示的单位）
struct DownloadSource {
    std::wstring name;
    bool isOfficial = false;          // 官方直链（展示时标注速度慢）
    std::vector<DownloadStep> steps;  // 该源的候选，按尝试顺序
};

struct DownloadPlan {
    std::vector<DownloadSource> sources;   // 按优先级排列
    // ZIP 候选总数（用于「候选链接共 N 条」）
    size_t zipStepCount = 0;
    bool hasZip() const { return zipStepCount > 0; }
};

// 构建候选源列表。fileName/directUrl 来自 Adoptium metadata，可能为空（此时只有按大版本直连的源）。
bool buildDownloadPlan(const std::wstring& majorVersion,
                       const std::wstring& fileName,
                       const std::wstring& directUrl,
                       DownloadPlan& outPlan);

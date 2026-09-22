#include "jdk/download_sources.hpp"

#include "network/host_throttle.hpp"
#include "common/java_version.hpp"

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace {

// 源优先级（数组顺序即尝试顺序）。
// 速度实测三轮均值：清华 4.44 / 南大 4.16 / 华为云 4.04 MB/s——几乎持平，
// 因此按「覆盖面 + 版本新鲜度 + 稳定性」排序，而不是按速度：
//   Adoptium 镜像（南大、清华）给的是最新补丁，放最前
//   华为云 GA 只有初始 GA 包，作为 Adoptium 未覆盖版本（12~15）的补充
//   旧库 EXE 只在没有 ZIP 时用（手动安装）
const std::vector<MirrorSource> kSources = {
        {L"南京大学镜像", L"https://mirrors.nju.edu.cn/adoptium/", SourceKind::Adoptium},
        {L"清华 TUNA 镜像", L"https://mirrors.tuna.tsinghua.edu.cn/Adoptium/", SourceKind::Adoptium},
        {L"华为云镜像", L"https://mirrors.huaweicloud.com/openjdk/", SourceKind::HuaweiGa},
        {kHuaweiLegacyName, L"", SourceKind::HuaweiLegacyExe},
};

// 华为云旧库（repo.huaweicloud.com/java/jdk/）的安装程序：这一批在 Adoptium 里都没有，
// 旧库也没有目录索引，链接只能固定写。2026-09 实测全部 200 且魔数为 MZ。
struct LegacyExe {
    const wchar_t* major;
    const wchar_t* url;
};
const LegacyExe kLegacyExes[] = {
        {L"6", L"https://repo.huaweicloud.com/java/jdk/6u45-b06/jdk-6u45-windows-x64.exe"},
        {L"7", L"https://repo.huaweicloud.com/java/jdk/7u80-b15/jdk-7u80-windows-x64.exe"},
        {L"8", L"https://repo.huaweicloud.com/java/jdk/8u202-b08/jdk-8u202-windows-x64.exe"},
        {L"9", L"https://repo.huaweicloud.com/java/jdk/9.0.1+11/jdk-9.0.1_windows-x64_bin.exe"},
        {L"10", L"https://repo.huaweicloud.com/java/jdk/10.0.2+13/jdk-10.0.2-windows-x64_bin.exe"},
};

bool endsWithIgnoreCase(const std::wstring& text, const wchar_t* suffix) {
    const size_t suffixLen = wcslen(suffix);
    if (text.size() < suffixLen) return false;
    const std::wstring tail = text.substr(text.size() - suffixLen);
    return std::equal(tail.begin(), tail.end(), suffix,
                      [](wchar_t a, wchar_t b) { return ::towlower(a) == ::towlower(b); });
}

int majorNumberOf(const std::wstring& version) {
    const JavaVersion parsed = JavaVersion::parse(version);
    return parsed.valid() ? parsed.feature : -1;
}

}  // namespace

// Adoptium / Temurin 发布过的主版本：8、11、16 及以上
bool adoptiumHasZip(const std::wstring& majorVersion) {
    const int major = majorNumberOf(majorVersion);
    if (major < 0) return false;
    return (major == 8) || (major == 11) || (major >= 16);
}

// 华为云 openjdk/{major}/ 实测覆盖 12~26（8/9/10/11 为 404）
bool huaweiGaHasZip(const std::wstring& majorVersion) {
    const int major = majorNumberOf(majorVersion);
    if (major < 0) return false;
    return major >= 12 && major <= 26;
}

const std::vector<MirrorSource>& mirrorSources() {
    return kSources;
}

std::wstring majorVersionFromFileName(const std::wstring& fileName) {
    // OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip → 21
    const size_t start = fileName.find(L"OpenJDK");
    if (start == std::wstring::npos) return L"";
    size_t pos = start + 7;   // 跳过 "OpenJDK"
    std::wstring digits;
    while (pos < fileName.size() && ::iswdigit(fileName[pos])) {
        digits.push_back(fileName[pos]);
        ++pos;
    }
    if (digits.empty()) return L"";
    if (pos >= fileName.size() || fileName[pos] != L'U') {
        return L"";   // 不是 OpenJDK<主版本>U- 命名，不硬猜
    }
    return digits;
}

std::wstring huaweiLegacyExeUrl(const std::wstring& majorVersion) {
    for (const auto& entry : kLegacyExes) {
        if (majorVersion == entry.major) {
            return entry.url;
        }
    }
    return L"";
}

bool hasZipCandidate(const std::wstring& majorVersion) {
    return adoptiumHasZip(majorVersion) || huaweiGaHasZip(majorVersion);
}

bool hasManualInstaller(const std::wstring& majorVersion) {
    return !huaweiLegacyExeUrl(majorVersion).empty();
}

bool buildDownloadPlan(const std::wstring& majorVersion,
                       const std::wstring& fileName,
                       const std::wstring& directUrl,
                       DownloadPlan& outPlan) {
    outPlan = DownloadPlan{};
    if (majorVersion.empty()) return false;

    const std::wstring major = majorVersion;
    const bool adoptiumOk = adoptiumHasZip(major);
    const bool gaOk = huaweiGaHasZip(major);
    const bool legacyOk = hasManualInstaller(major);
    if (!adoptiumOk && !gaOk && !legacyOk) {
        return false;   // 没有任何可用来源
    }

    // Adoptium 型源只能直接用 metadata 给的文件名；GA 型按大版本自己拼
    const std::wstring gaName = L"openjdk-" + major + L"_windows-x64_bin.zip";

    // 按主机归并：同一主机（同一镜像站）的多个链接算同一个源
    std::vector<DownloadSource> ordered;
    std::vector<std::wstring> hostOf;   // 与 ordered 一一对应的主机键

    const auto appendStep = [&](const wchar_t* name, const std::wstring& url,
                                DownloadStepKind kind, const std::wstring& hostKey) {
        for (size_t i = 0; i < ordered.size(); ++i) {
            if (hostOf[i] == hostKey) {
                ordered[i].steps.push_back(DownloadStep{kind, name, url});
                return;
            }
        }
        DownloadSource source;
        source.name = name;
        source.steps.push_back(DownloadStep{kind, name, url});
        ordered.push_back(source);
        hostOf.push_back(hostKey);
    };

    for (const auto& source : kSources) {
        switch (source.kind) {
            case SourceKind::Adoptium:
                if (adoptiumOk && !fileName.empty()) {
                    const std::wstring url = std::wstring(source.baseUrl) + major +
                                             L"/jdk/x64/windows/" + fileName;
                    appendStep(source.name, url, DownloadStepKind::Zip,
                               HostThrottle::hostOf(url));
                }
                break;
            case SourceKind::HuaweiGa:
                if (gaOk) {
                    const std::wstring url = std::wstring(source.baseUrl) + major + L"/" + gaName;
                    appendStep(source.name, url, DownloadStepKind::Zip,
                               HostThrottle::hostOf(url));
                }
                break;
            case SourceKind::HuaweiLegacyExe: {
                const std::wstring exeUrl = huaweiLegacyExeUrl(major);
                if (!exeUrl.empty()) {
                    appendStep(source.name, exeUrl, DownloadStepKind::Exe, L"legacy-exe");
                }
                break;
            }
        }
    }

    // 官方直链：作为最后一个源（选它时速度慢，但版本最新）
    if (!directUrl.empty()) {
        DownloadSource official;
        official.name = kOfficialSourceName;
        official.isOfficial = true;
        official.steps.push_back(DownloadStep{
                endsWithIgnoreCase(directUrl, L".msi") ? DownloadStepKind::Exe : DownloadStepKind::Zip,
                kOfficialSourceName, directUrl});
        ordered.push_back(official);
    }

    for (const auto& source : ordered) {
        for (const auto& step : source.steps) {
            if (step.kind == DownloadStepKind::Zip) {
                ++outPlan.zipStepCount;
            }
        }
    }
    outPlan.sources = std::move(ordered);
    return !outPlan.sources.empty();
}

#include "jdk/download_plan.hpp"

#include "common/java_version.hpp"
#include "network/host_throttle.hpp"

#include <algorithm>
#include <cctype>

bool isDemoUrl(const std::wstring& url) {
    std::wstring lowerUrl = url;
    std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(::towlower(ch)); });
    return lowerUrl.find(L"-demos") != std::wstring::npos;
}

std::vector<std::wstring> filterUrlsForVersion(const std::vector<std::wstring>& urls,
                                               const std::wstring& requested) {
    if (!JavaVersion::isFullVersionQuery(requested)) {
        return urls;   // 只给了主版本：全部候选按顺序尝试
    }
    std::vector<std::wstring> matched;
    for (const auto& url : urls) {
        if (url.find(requested) != std::wstring::npos) {
            matched.push_back(url);
        }
    }
    return matched;
}

std::wstring sourceDisplayName(const std::wstring& url) {
    if (url.empty()) {
        return kOfficialSourceName;
    }

    std::wstring lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(::towlower(ch)); });

    struct HostName {
        const wchar_t* host;
        const wchar_t* name;
    };
    static const HostName kKnownHosts[] = {
            {L"mirrors.huaweicloud.com", L"华为云镜像"},
            {L"repo.huaweicloud.com", L"华为云镜像（Oracle 旧版）"},
            {L"mirrors.nju.edu.cn", L"南京大学镜像"},
            {L"mirrors.tuna.tsinghua.edu.cn", L"清华 TUNA 镜像"},
            {L"mirrors.ustc.edu.cn", L"中科大镜像"},
            {L"mirrors.aliyun.com", L"阿里云镜像"},
            {L"mirrors.bfsu.edu.cn", L"北外镜像"},
            {L"api.adoptium.net", L"Adoptium 官方源"},
            {L"github.com", L"GitHub Releases"},
            {L"objects.githubusercontent.com", L"GitHub Releases"},
    };
    for (const auto& entry : kKnownHosts) {
        if (lower.find(entry.host) != std::wstring::npos) {
            return entry.name;
        }
    }

    if (lower.rfind(L"file://", 0) == 0) {
        return L"本地文件";
    }
    const std::wstring host = HostThrottle::hostOf(url);
    return host.empty() ? L"未知来源" : host;
}

std::wstring sourceKey(const std::wstring& url) {
    if (url.empty()) {
        return L"official";
    }
    return HostThrottle::hostOf(url);
}

DownloadPlan DownloadPlan::build(DownloadMode mode,
                                 bool installerOnly,
                                 const std::vector<std::wstring>& zipUrls,
                                 const std::vector<std::wstring>& exeUrls) {
    DownloadPlan plan;
    plan.installerOnly = installerOnly;

    const auto addZipSteps = [&plan, &zipUrls]() {
        for (const auto& url : zipUrls) {
            if (isDemoUrl(url)) {
                plan.skippedDemoUrls.push_back(url);   // demo 包直接跳过，不再下载
                continue;
            }
            plan.steps.push_back(DownloadStep{DownloadStepKind::MirrorZip, url});
        }
    };
    const auto addExeSteps = [&plan, &exeUrls]() {
        for (const auto& url : exeUrls) {
            plan.steps.push_back(DownloadStep{DownloadStepKind::MirrorExe, url});
        }
    };

    // exe 参数：只下载安装包（官方 API 只提供 ZIP，因此不参与该模式）
    if (installerOnly) {
        addExeSteps();
        return plan;
    }

    switch (mode) {
        case DownloadMode::OfficialOnly:
            plan.steps.push_back(DownloadStep{DownloadStepKind::OfficialZip, L""});
            break;
        case DownloadMode::MirrorOnly:
            addZipSteps();
            addExeSteps();
            break;
        case DownloadMode::Default:
        default:
            addZipSteps();
            addExeSteps();
            plan.steps.push_back(DownloadStep{DownloadStepKind::OfficialZip, L""});
            break;
    }
    return plan;
}

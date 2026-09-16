#include "jdk/download_plan.hpp"

#include "common/java_version.hpp"

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

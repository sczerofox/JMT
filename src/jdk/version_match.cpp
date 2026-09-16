#include "jdk/version_match.hpp"

#include "common/java_version.hpp"

#include <algorithm>

std::vector<VersionCandidate> sortByVersionDesc(const std::vector<VersionCandidate>& installed) {
    std::vector<VersionCandidate> sorted = installed;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const VersionCandidate& left, const VersionCandidate& right) {
                         return JavaVersion::parse(left.first).compare(JavaVersion::parse(right.first)) > 0;
                     });
    return sorted;
}

VersionMatch resolveVersion(const std::vector<VersionCandidate>& installed,
                            const std::wstring& query,
                            bool exactOnly) {
    VersionMatch result;
    if (query.empty()) return result;

    for (const auto& candidate : installed) {
        if (candidate.first == query) {
            result.found = true;
            result.version = candidate.first;
            result.path = candidate.second;
            result.candidates.push_back(candidate);
            return result;
        }
    }
    if (exactOnly) return result;

    for (const auto& candidate : installed) {
        const JavaVersion candidateVersion = JavaVersion::parse(candidate.first);
        if (candidateVersion.valid() && candidateVersion.matches(query)) {
            result.candidates.push_back(candidate);
        } else if (!candidateVersion.valid() && candidate.first.rfind(query, 0) == 0) {
            // 版本串解析不出来时退化为前缀比较（向后兼容）
            result.candidates.push_back(candidate);
        }
    }
    if (result.candidates.empty()) return result;

    result.candidates = sortByVersionDesc(result.candidates);
    result.found = true;
    result.version = result.candidates.front().first;
    result.path = result.candidates.front().second;
    result.ambiguous = result.candidates.size() > 1;
    return result;
}

std::wstring maxVersion(const std::vector<VersionCandidate>& installed) {
    if (installed.empty()) return L"";
    return sortByVersionDesc(installed).front().first;
}

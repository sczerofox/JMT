#include "common/java_version.hpp"

#include <algorithm>
#include <cwctype>
#include <regex>

namespace {

std::wstring trimmed(const std::wstring& input) {
    size_t begin = 0;
    while (begin < input.size() &&
           (input[begin] == L' ' || input[begin] == L'\t' || input[begin] == L'"' ||
            input[begin] == L'\r' || input[begin] == L'\n')) {
        ++begin;
    }
    size_t end = input.size();
    while (end > begin &&
           (input[end - 1] == L' ' || input[end - 1] == L'\t' || input[end - 1] == L'"' ||
            input[end - 1] == L'\r' || input[end - 1] == L'\n')) {
        --end;
    }
    return input.substr(begin, end - begin);
}

// 去掉 jdk- / jdk / openjdk- / openjdk 前缀（目录名与文件名里常见）
std::wstring stripJdkPrefix(const std::wstring& input) {
    std::wstring lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(::towlower(ch)); });
    const wchar_t* prefixes[] = {L"openjdk-", L"openjdk", L"jdk-", L"jdk"};
    for (const wchar_t* prefix : prefixes) {
        const std::wstring candidate(prefix);
        if (lower.compare(0, candidate.size(), candidate) == 0) {
            return input.substr(candidate.size());
        }
    }
    return input;
}

// 8u202 这类旧别名
bool parseUpdateAlias(const std::wstring& text, JavaVersion& version) {
    const std::wregex pattern(L"^(\\d+)[uU](\\d+)$");
    std::wsmatch match;
    if (!std::regex_match(text, match, pattern)) return false;
    version.feature = std::stoi(match[1].str());
    version.update = std::stoi(match[2].str());
    return true;
}

// 查询串指定了几段：1=只看主版本，2=加 interim，3=加 update，4=加 patch
int specifiedDepth(const std::wstring& query) {
    if (parseUpdateAlias(query, JavaVersion{})) return 2;
    int dots = 0;
    bool hasUnderscoreUpdate = false;
    for (wchar_t ch : query) {
        if (ch == L'.') ++dots;
        if (ch == L'_') hasUnderscoreUpdate = true;
    }
    if (dots <= 0) return 1;              // "17" / "8"
    if (hasUnderscoreUpdate) return 3;    // "1.8.0_202" → 主版本 + interim + update
    return dots + 1;                      // "17.0" → 2，"17.0.2" → 3
}

}  // namespace

JavaVersion JavaVersion::parse(const std::wstring& raw) {
    JavaVersion version;
    version.raw = raw;

    const std::wstring text = stripJdkPrefix(trimmed(raw));
    if (text.empty()) return JavaVersion{raw};
    if (parseUpdateAlias(text, version)) return version;

    static const std::wregex pattern(
            L"^(\\d+)(?:\\.(\\d+))?(?:\\.(\\d+))?(?:_(\\d+))?(?:\\+([0-9A-Za-z._-]+))?$");
    std::wsmatch match;
    if (!std::regex_match(text, match, pattern)) return JavaVersion{raw};

    const int first = std::stoi(match[1].str());
    const int second = match[2].matched ? std::stoi(match[2].str()) : 0;
    const int third = match[3].matched ? std::stoi(match[3].str()) : 0;
    const int underscore = match[4].matched ? std::stoi(match[4].str()) : 0;
    if (match[5].matched) version.build = match[5].str();

    if (first == 1) {
        // 1.8.0_202 → feature=8，interim/update 顺次映射
        version.feature = second;
        version.interim = third;
        version.update = underscore;
    } else {
        version.feature = first;
        version.interim = second;
        version.update = underscore != 0 ? underscore : third;
    }
    return version;
}

int JavaVersion::compare(const JavaVersion& other) const {
    if (feature != other.feature) return feature < other.feature ? -1 : 1;
    if (interim != other.interim) return interim < other.interim ? -1 : 1;
    if (update != other.update) return update < other.update ? -1 : 1;
    if (patch != other.patch) return patch < other.patch ? -1 : 1;
    return 0;
}

bool JavaVersion::isFullVersionQuery(const std::wstring& query) {
    const std::wstring text = stripJdkPrefix(trimmed(query));
    if (text.empty()) return false;
    if (parseUpdateAlias(text, JavaVersion{})) return true;   // 8u202
    return text.find(L'.') != std::wstring::npos;            // 17.0.2 / 1.8.0_202
}

bool JavaVersion::matches(const std::wstring& query) const {
    const std::wstring text = stripJdkPrefix(trimmed(query));
    if (text.empty() || !valid()) return false;
    if (text == raw) return true;                            // 精确匹配

    const JavaVersion queryVersion = JavaVersion::parse(text);
    if (!queryVersion.valid()) return false;
    if (queryVersion.feature != feature) return false;

    // 8u202 这类别名必须连 update 一起比，不能只比主版本
    if (parseUpdateAlias(text, JavaVersion{})) {
        return queryVersion.update == update;
    }

    const int depth = specifiedDepth(text);
    if (depth <= 1) return true;                             // "17" / "8"
    if (queryVersion.interim != interim) return false;
    if (depth <= 2) return true;                             // "17.0" / "1.8"
    if (depth == 3) {
        // "17.0.2"：update 明确指定；"1.8.0" 这类 update 为 0 的写法按未限定处理
        return queryVersion.update == 0 || queryVersion.update == update;
    }
    return queryVersion.update == update && queryVersion.patch == patch;
}

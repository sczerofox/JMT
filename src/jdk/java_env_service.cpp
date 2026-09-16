#include "jdk/java_env_service.hpp"
#include "common/java_version.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "system/path_utils.hpp"
#include "platform/output.hpp"
#include "system/utils.hpp"
#include <algorithm>
#include <regex>

// 辅助：获取 JDK bin 路径
static std::wstring GetJdkBinPath(const std::wstring& jdkPath) {
    return JoinPath(jdkPath, L"bin");
}

// 判断一个 PATH 条目是否为 JDK bin 路径（路径以 bin 结尾且包含 "jdk"）
static bool IsJdkBinPath(const std::wstring& pathEntry) {
    std::wstring lower = pathEntry;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    // 检查是否以 bin 结尾（忽略末尾反斜杠）
    std::wstring trimmed = lower;
    while (!trimmed.empty() && (trimmed.back() == L'\\' || trimmed.back() == L'/')) {
        trimmed.pop_back();
    }
    if (trimmed.size() < 3) return false;
    if (trimmed.substr(trimmed.size() - 3) != L"bin") return false;
    // 检查是否包含 "jdk"
    return trimmed.find(L"jdk") != std::wstring::npos;
}

bool JavaEnvService::setCurrentJdk(const std::wstring& jdkPath, EnvTarget target) {
    std::wstring binPath = GetJdkBinPath(jdkPath);

    // 1. 获取当前 PATH
    std::wstring path = registry_.readPath(target);
    auto entries = PathUtils::splitPath(path);

    // 2. 删除所有 JDK bin 路径（由 JMT 管理的）
    std::vector<std::wstring> newEntries;
    for (const auto& e : entries) {
        if (!IsJdkBinPath(e)) {
            newEntries.push_back(e);
        }
    }

    // 3. 移除 Oracle javapath（无论是否存在，确保其不干扰）
    removeOracleJavaPath(target); // 内部会修改 PATH，但我们重新读取 PATH 以确保一致
    path = registry_.readPath(target);
    entries = PathUtils::splitPath(path);
    // 再次过滤（防止上一步删除不彻底）
    newEntries.clear();
    for (const auto& e : entries) {
        if (!IsJdkBinPath(e)) {
            newEntries.push_back(e);
        }
    }

    // 4. 添加新路径（去重）
    newEntries = PathUtils::addUniqueEntry(newEntries, binPath);

    // 5. 组合新 PATH
    std::wstring newPath;
    for (size_t i = 0; i < newEntries.size(); ++i) {
        if (i > 0) newPath += L';';
        newPath += newEntries[i];
    }

    // 6. 写入 PATH
    if (!registry_.writePath(newPath, target))
        return false;

    return true;
}

bool JavaEnvService::clearCurrentJdk(EnvTarget target) {
    // 1. 获取当前 PATH
    std::wstring path = registry_.readPath(target);
    auto entries = PathUtils::splitPath(path);

    // 2. 删除所有 JDK bin 路径
    std::vector<std::wstring> newEntries;
    for (const auto& e : entries) {
        if (!IsJdkBinPath(e)) {
            newEntries.push_back(e);
        }
    }

    std::wstring newPath;
    for (size_t i = 0; i < newEntries.size(); ++i) {
        if (i > 0) newPath += L';';
        newPath += newEntries[i];
    }

    if (!registry_.writePath(newPath, target))
        return false;

    // 3. 恢复 Oracle javapath（如果目录存在）
    restoreOracleJavaPath(target);
    return true;
}

namespace {

// 去掉尾部 \bin 得到 JDK 安装目录
std::wstring parentOfBin(const std::wstring& binPath) {
    std::wstring dir = binPath;
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) {
        dir.pop_back();
    }
    const size_t pos = dir.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : dir.substr(0, pos);
}

// 回退方案：直接从路径文本里解析版本（release 文件缺失时使用）
std::wstring versionFromPathText(const std::wstring& text) {
    static const std::wregex pattern(L"(?:jdk|openjdk)[-_]?(\\d+(?:[uU]\\d+)?(?:\\.\\d+)*(?:_\\d+)?)");
    std::wsmatch match;
    if (std::regex_search(text, match, pattern)) {
        const JavaVersion version = JavaVersion::parse(match[1].str());
        if (version.valid()) {
            return version.raw;
        }
    }
    return L"";
}

}  // namespace

std::wstring JavaEnvService::getCurrentVersion() {
    // Windows 上用户 PATH 优先于系统 PATH 生效，因此先看用户级再回退系统级
    for (EnvTarget scope : {EnvTarget::UserOnly, EnvTarget::SystemOnly}) {
        for (const auto& entry : PathUtils::splitPath(registry_.readPath(scope))) {
            if (!IsJdkBinPath(entry)) continue;

            // 1) 读该 JDK 的 release：目录名往往只有主版本（jdk-17 实际是 17.0.9）
            const std::wstring jdkDir = parentOfBin(entry);
            if (!jdkDir.empty() && resolver_) {
                const std::wstring resolved = resolver_(jdkDir);
                if (!resolved.empty()) {
                    return resolved;
                }
            }

            // 2) 回退：从路径文本解析
            const std::wstring parsed = versionFromPathText(entry);
            if (!parsed.empty()) {
                return parsed;
            }
        }
    }
    return L"";
}

void JavaEnvService::removeOracleJavaPath(EnvTarget target) {
    std::wstring oraclePath = L"C:\\Program Files\\Common Files\\Oracle\\Java\\javapath";
    std::wstring path = registry_.readPath(target);
    auto entries = PathUtils::splitPath(path);
    auto newEntries = PathUtils::removeEntries(entries, oraclePath);
    // 也删除带引号的版本（如果有）
    std::wstring quoted = L"\"" + oraclePath + L"\"";
    newEntries = PathUtils::removeEntries(newEntries, quoted);
    std::wstring newPath;
    for (size_t i = 0; i < newEntries.size(); ++i) {
        if (i > 0) newPath += L';';
        newPath += newEntries[i];
    }
    registry_.writePath(newPath, target);
    out_.line(OutputLevel::Info, L"已从 PATH 中移除 Oracle javapath 条目");
}

void JavaEnvService::restoreOracleJavaPath(EnvTarget target) {
    std::wstring oraclePath = L"C:\\Program Files\\Common Files\\Oracle\\Java\\javapath";
    // 检查目录是否存在，若不存在则不添加
    if (!IsDirectory(oraclePath)) {
        return;
    }
    std::wstring path = registry_.readPath(target);
    auto entries = PathUtils::splitPath(path);
    // 如果已存在则不重复添加
    bool exists = false;
    for (const auto& e : entries) {
        if (PathUtils::arePathsEqual(e, oraclePath)) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        auto newEntries = PathUtils::addUniqueEntry(entries, oraclePath);
        std::wstring newPath;
        for (size_t i = 0; i < newEntries.size(); ++i) {
            if (i > 0) newPath += L';';
            newPath += newEntries[i];
        }
        registry_.writePath(newPath, target);
        out_.line(OutputLevel::Info, L"已添加 Oracle javapath 到 PATH");
    }
}

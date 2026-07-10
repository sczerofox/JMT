#include "java_env_service.hpp"
#include "../infrastructure/path_utils.hpp"
#include "../print/color_print.hpp"
#include "../utils/utils.hpp"
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
    std::wstring path = RegistryOperator::getPath(target);
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
    path = RegistryOperator::getPath(target);
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
    if (!RegistryOperator::setPath(newPath, target))
        return false;

    return true;
}

bool JavaEnvService::clearCurrentJdk(EnvTarget target) {
    // 1. 获取当前 PATH
    std::wstring path = RegistryOperator::getPath(target);
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

    if (!RegistryOperator::setPath(newPath, target))
        return false;

    // 3. 恢复 Oracle javapath（如果目录存在）
    restoreOracleJavaPath(target);
    return true;
}

std::wstring JavaEnvService::getCurrentVersion() {
    std::wstring path = RegistryOperator::getPath(EnvTarget::Auto);
    auto entries = PathUtils::splitPath(path);
    for (const auto& e : entries) {
        if (IsJdkBinPath(e)) {
            // 从路径中提取版本号，如 D:\Program Files\Java\jdk-17\bin -> 17
            std::wregex pattern(L"jdk[-_]?(\\d+)");
            std::wsmatch match;
            if (std::regex_search(e, match, pattern) && match.size() > 1) {
                return match[1].str();
            }
        }
    }
    return L"";
}

void JavaEnvService::removeOracleJavaPath(EnvTarget target) {
    std::wstring oraclePath = L"C:\\Program Files\\Common Files\\Oracle\\Java\\javapath";
    std::wstring path = RegistryOperator::getPath(target);
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
    RegistryOperator::setPath(newPath, target);
    PrintInfo(L"已从 PATH 中移除 Oracle javapath 条目");
}

void JavaEnvService::restoreOracleJavaPath(EnvTarget target) {
    std::wstring oraclePath = L"C:\\Program Files\\Common Files\\Oracle\\Java\\javapath";
    // 检查目录是否存在，若不存在则不添加
    if (!IsDirectory(oraclePath)) {
        return;
    }
    std::wstring path = RegistryOperator::getPath(target);
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
        RegistryOperator::setPath(newPath, target);
        PrintInfo(L"已添加 Oracle javapath 到 PATH");
    }
}
#include "jmt_path_service.hpp"
#include "../infrastructure/path_utils.hpp"
#include "../print/color_print.hpp"

bool JmtPathService::registerJmtPath(const std::wstring& exeDir, EnvTarget target) {
    std::wstring path = RegistryOperator::getPath(target);
    auto entries = PathUtils::splitPath(path);
    // 删除所有匹配 exeDir 的条目
    entries = PathUtils::removeEntries(entries, exeDir);
    // 添加当前目录
    entries = PathUtils::addUniqueEntry(entries, exeDir);
    std::wstring newPath;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) newPath += L';';
        newPath += entries[i];
    }
    bool ok = RegistryOperator::setPath(newPath, target);
    if (ok) PrintDebug(L"JMT path registered: " + exeDir);
    return ok;
}

bool JmtPathService::unregisterJmtPath(const std::wstring& exeDir, EnvTarget target) {
    std::wstring path = RegistryOperator::getPath(target);
    auto entries = PathUtils::splitPath(path);
    entries = PathUtils::removeEntries(entries, exeDir);
    std::wstring newPath;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) newPath += L';';
        newPath += entries[i];
    }
    bool ok = RegistryOperator::setPath(newPath, target);
    if (ok) PrintDebug(L"JMT path unregistered: " + exeDir);
    return ok;
}
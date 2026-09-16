#include "jdk/jmt_path_service.hpp"
#include "system/path_utils.hpp"
#include "platform/output.hpp"

bool JmtPathService::registerJmtPath(const std::wstring& exeDir, EnvTarget target) {
    std::wstring path = registry_.readPath(target);
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
    bool ok = registry_.writePath(newPath, target);
    if (ok) out_.line(OutputLevel::Debug, L"JMT path registered: " + exeDir);
    return ok;
}
#include "app/app_paths.hpp"

#include "system/utils.hpp"

AppPaths AppPaths::rootedAt(const std::wstring& root) {
    AppPaths paths;
    paths.exeDir = root;
    paths.exePath = JoinPath(root, L"jmt.exe");
    paths.cacheFile = JoinPath(root, L".jmt_cache");
    paths.tempDir = JoinPath(root, L".temp");
    paths.trashDir = JoinPath(root, L".trash");
    return paths;
}

AppPaths AppPaths::fromExecutable() {
    return rootedAt(GetExeDirectory());
}

#pragma once
#include <string>

struct JmtContext {
    std::wstring exeDirectory;
    std::wstring cacheFilePath;
    bool isInteractive;
    bool isElevated;
};
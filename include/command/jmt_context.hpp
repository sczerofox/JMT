#pragma once
#include <string>
#include "app/app_paths.hpp"

struct JmtContext {
    AppPaths paths;
    bool isInteractive;
    bool isElevated;
};

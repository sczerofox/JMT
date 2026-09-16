#pragma once

#include "app/app_paths.hpp"
#include "platform/elevator.hpp"
#include "platform/output.hpp"
#include "platform/registry.hpp"

class JdkScanService;
class JavaEnvService;
class JdkDownloadService;
class JmtPathService;

// 应用上下文：命令层可见的全部外部依赖（路径 + 三个端口 + 运行状态）。
// 生产环境由 main.cpp 装配；测试可自行装配 fake 实现（内存注册表、捕获输出等）。
struct AppContext {
    AppPaths paths;
    IOutput* out = nullptr;
    IRegistry* registry = nullptr;
    IElevator* elevator = nullptr;
    JdkScanService* scan = nullptr;
    JavaEnvService* env = nullptr;
    JdkDownloadService* download = nullptr;
    JmtPathService* jmtPath = nullptr;
    bool isInteractive = false;
    bool isElevated = false;
};

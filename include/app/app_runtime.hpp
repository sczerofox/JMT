#pragma once

#include "app/app_context.hpp"
#include "app/app_paths.hpp"
#include "jdk/java_env_service.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "jdk/jmt_path_service.hpp"
#include "platform/elevator.hpp"
#include "platform/output.hpp"
#include "system/registry_operator.hpp"

// 组合根：装配生产环境的 Win32 适配器与服务实例，并对外暴露 AppContext。
// 测试不必使用本类，可以直接构造 AppContext + 自己的 fake 端口/服务。
class AppRuntime {
public:
    AppRuntime(const AppPaths& paths, bool isInteractive);

    [[nodiscard]] AppContext& context() { return ctx_; }
    [[nodiscard]] IOutput& output() { return output_; }

private:
    ConsoleOutput output_;
    RegistryOperator registry_;
    WinElevator elevator_;
    AppContext ctx_;          // 先建上下文：服务会引用 ctx_.paths
    JdkScanService scan_;
    JavaEnvService env_;
    JmtPathService jmtPath_;
};

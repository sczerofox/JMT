#include "app/app_runtime.hpp"

AppRuntime::AppRuntime(const AppPaths& paths, bool isInteractive)
        : ctx_{},
          scan_(ctx_.paths, output_),
          env_(registry_, output_, [](const std::wstring& jdkPath) {
              return JdkScanService::extractVersion(jdkPath);
          }),
          download_(ctx_.paths, output_, throttle_),
          jmtPath_(registry_, output_) {
    output_.init();          // 等价于旧的 InitConsole()

    ctx_.paths = paths;
    ctx_.out = &output_;
    ctx_.registry = &registry_;
    ctx_.elevator = &elevator_;
    ctx_.scan = &scan_;
    ctx_.env = &env_;
    ctx_.download = &download_;
    ctx_.jmtPath = &jmtPath_;
    ctx_.isInteractive = isInteractive;
    ctx_.isElevated = elevator_.isElevated();
}

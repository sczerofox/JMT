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

    // 诊断开关：JMT_NO_PROGRESS=1 时不做原地刷新，改为普通行输出
    wchar_t noProgress[8] = {0};
    if (GetEnvironmentVariableW(L"JMT_NO_PROGRESS", noProgress, 8) > 0 && noProgress[0] != L'0') {
        output_.setProgressEnabled(false);
    }

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

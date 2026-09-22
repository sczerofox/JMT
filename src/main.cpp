#include "command/command_registry.hpp"
#include "app/app_context.hpp"
#include "command/search_command.hpp"
#include "command/list_command.hpp"
#include "command/use_command.hpp"
#include "command/env_command.hpp"
#include "command/remove_command.hpp"
#include "command/download_command.hpp"
#include "command/version_command.hpp"
#include "command/help_command.hpp"
#include "command/rollback_command.hpp"
#include "console/repl_engine.hpp"
#include "app/app_runtime.hpp"
#include "common/cancel_token.hpp"
#include <windows.h>
#include "app/elevation_gate.hpp"
#include "system/utils.hpp"
#include "jdk/jdk_download_service.hpp"
#include <vector>
#include <memory>

// 未处理的结构化异常（访问违规等）默认会让进程静默退出，这里统一打印诊断信息。
// 只用最底层的 WriteConsoleW/WriteFile，避免在异常路径里再触发复杂逻辑。
static LONG WINAPI JmtUnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
    const DWORD code = (info != nullptr && info->ExceptionRecord != nullptr)
                               ? info->ExceptionRecord->ExceptionCode
                               : 0;
    const void* address = (info != nullptr && info->ExceptionRecord != nullptr)
                                  ? info->ExceptionRecord->ExceptionAddress
                                  : nullptr;

    // 把故障地址换算成「模块 + 偏移」，这样只看输出就能定位到具体代码
    wchar_t location[160] = L"未知位置";
    HMODULE module = nullptr;
    if (address != nullptr &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(address), &module) &&
        module != nullptr) {
        wchar_t modulePath[MAX_PATH] = {0};
        GetModuleFileNameW(module, modulePath, MAX_PATH);
        const wchar_t* moduleName = wcsrchr(modulePath, L'\\');
        moduleName = moduleName != nullptr ? moduleName + 1 : modulePath;
        const unsigned long long offset =
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) -
                                                reinterpret_cast<uintptr_t>(module));
        swprintf_s(location, L"%s+0x%llX", moduleName, offset);
    }

    wchar_t message[512];
    swprintf_s(message,
               L"[ERROR] 程序异常终止（异常代码 0x%08X，位置 %s），请把上面的输出反馈给开发者\r\n",
               code, location);

    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written = 0;
    DWORD mode = 0;
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
        WriteConsoleW(handle, message, static_cast<DWORD>(wcslen(message)), &written, nullptr);
    } else if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        char utf8[512] = {0};
        const int size = WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
        if (size > 0) {
            WriteFile(handle, utf8, static_cast<DWORD>(size - 1), &written, nullptr);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// 单次命令模式下的 Ctrl+C：置位取消开关，让下载/扫描尽快退出
static BOOL WINAPI JmtCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT) {
        globalCancelState().cancel();
        return TRUE;
    }
    return FALSE;
}
int wmain(int argc, wchar_t* argv[]) {
    // 注册 Ctrl+C 处理与未处理异常诊断：下载/扫描时可以被打断，异常退出也能看到原因
    SetConsoleCtrlHandler(JmtCtrlHandler, TRUE);
    SetUnhandledExceptionFilter(JmtUnhandledExceptionFilter);

    // 组合根：装配 Win32 适配器与服务实例
    AppRuntime runtime(AppPaths::fromExecutable(), argc == 1);
    AppContext& ctx = runtime.context();

    // 构建命令注册表
    CommandRegistry registry;
    registry.registerCommand(L"search", std::make_unique<SearchCommand>());
    registry.registerCommand(L"list", std::make_unique<ListCommand>());
    registry.registerCommand(L"use", std::make_unique<UseCommand>());
    registry.registerCommand(L"env", std::make_unique<EnvCommand>());
    registry.registerCommand(L"remove", std::make_unique<RemoveCommand>());
    registry.registerCommand(L"download", std::make_unique<DownloadCommand>());
    registry.registerCommand(L"version", std::make_unique<VersionCommand>());
    registry.registerCommand(L"help", std::make_unique<HelpCommand>(registry));
    registry.registerCommand(L"rollback", std::make_unique<RollbackCommand>());

    // 解析命令
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

    // 如果交互模式，直接进入 REPL
    if (ctx.isInteractive) {
        ReplEngine engine(registry, ctx);
        engine.run();
        return toInt(ExitCode::Ok);
    }

    // 单次命令模式
    if (args.empty()) {
        // 无参数但 argc>1？实际上不可能，但以防万一
        ctx.out->line(OutputLevel::Error, L"无命令");
        return toInt(ExitCode::BadArgs);
    }

    const std::wstring& cmd = args[0];

    // 执行命令
    auto* command = registry.findCommand(cmd);
    if (!command) {
        ctx.out->line(OutputLevel::Error, L"未知命令: " + cmd);
        return toInt(ExitCode::BadArgs);
    }

    // 提权前先做只读校验：版本不存在之类的问题不必弹 UAC
    if (const ExitCode preflight = command->preflight(args, ctx); preflight != ExitCode::Ok) {
        return toInt(preflight);
    }

    // 提权：由命令元数据（CommandBase::requiresElevation）驱动，统一走 ElevationGate
    const ElevationDecision decision =
            ElevationGate::ensure(command->requiresElevation(), args, ctx);
    if (!decision.proceed) {
        return toInt(decision.code);
    }

    try {
        return toInt(command->execute(args, ctx));
    } catch (const std::exception& e) {
        ctx.out->line(OutputLevel::Error, L"执行异常: " + ToWideString(e.what()));
        return toInt(ExitCode::BadArgs);
    }
}

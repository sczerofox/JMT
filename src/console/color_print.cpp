#include "console/color_print.hpp"
#include "platform/output.hpp"

// 过渡期实现：把既有的 Print* 自由函数转发到默认控制台输出端口。
// 调用点迁移完成后（skeleton/8）本文件会被删除。
namespace {

ConsoleOutput& defaultOutput() {
    static ConsoleOutput output;
    return output;
}

}  // namespace

void InitConsole() {
    defaultOutput().init();
}

void PrintSuccess(const std::wstring& msg) { defaultOutput().line(OutputLevel::Success, msg); }

void PrintError(const std::wstring& msg) { defaultOutput().line(OutputLevel::Error, msg); }

void PrintInfo(const std::wstring& msg) { defaultOutput().line(OutputLevel::Info, msg); }

void PrintWarning(const std::wstring& msg) { defaultOutput().line(OutputLevel::Warning, msg); }

void PrintDebug([[maybe_unused]] const std::wstring& msg) {
    defaultOutput().line(OutputLevel::Debug, msg);
}

void PrintProgress(const std::wstring& msg) { defaultOutput().progress(msg); }

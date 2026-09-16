#pragma once

#include <string>

// 输出端口：命令层与服务层只依赖这个接口，控制台/文件/静默实现可互换。
// 注意：本头文件刻意不包含 windows.h，保持端口层与平台无关。
enum class OutputLevel { Info, Success, Warning, Error, Debug };

class IOutput {
public:
    virtual ~IOutput() = default;
    virtual void line(OutputLevel level, const std::wstring& text) = 0;
    virtual void progress(const std::wstring& text) = 0;
    virtual void clearProgress() = 0;
    // 是否支持「原地刷新」的进度行（真实控制台为 true；重定向/管道为 false）
    [[nodiscard]] virtual bool supportsProgress() const = 0;
};

// 控制台实现：ANSI 上色 + WriteConsoleW，行为与重构前的 color_print 完全一致。
// Debug 等级仅在 _DEBUG 构建下输出（沿用旧 PrintDebug 的编译期开关）。
class ConsoleOutput : public IOutput {
public:
    void init();   // 等价于旧的 InitConsole()

    void line(OutputLevel level, const std::wstring& text) override;
    void progress(const std::wstring& text) override;
    void clearProgress() override;
    [[nodiscard]] bool supportsProgress() const override;

private:
    void writeLine(const wchar_t* tag, const std::wstring& text, int attributes);

    void* handle_ = nullptr;   // HANDLE：避免在端口头文件引入 windows.h
    bool useVT_ = false;
};

// 静默实现：后台任务与测试使用
class NullOutput : public IOutput {
public:
    void line(OutputLevel, const std::wstring&) override {}
    void progress(const std::wstring&) override {}
    void clearProgress() override {}
    [[nodiscard]] bool supportsProgress() const override { return false; }
};

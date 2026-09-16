#pragma once

#include <string>
#include <utility>

#include "common/exit_code.hpp"

// 错误描述：退出码 + 可直接展示给用户的文案
struct Error {
    ExitCode code = ExitCode::IoOrNetwork;
    std::wstring message;
};

// 无返回值的操作结果（默认构造即成功）
class Status {
public:
    Status() = default;

    static Status ok() { return Status{}; }

    static Status fail(ExitCode code, std::wstring message) {
        Status status;
        status.ok_ = false;
        status.error_ = Error{code, std::move(message)};
        return status;
    }

    [[nodiscard]] bool isOk() const { return ok_; }
    [[nodiscard]] ExitCode code() const { return ok_ ? ExitCode::Ok : error_.code; }
    [[nodiscard]] const std::wstring& message() const { return error_.message; }

private:
    bool ok_ = true;
    Error error_;
};

// 带返回值的操作结果：失败时 value() 为 T 的默认值，调用方应先看 isOk()
template <class T>
class Result {
public:
    static Result ok(T value) {
        Result result;
        result.ok_ = true;
        result.value_ = std::move(value);
        return result;
    }

    static Result fail(ExitCode code, std::wstring message) {
        Result result;
        result.ok_ = false;
        result.error_ = Error{code, std::move(message)};
        return result;
    }

    [[nodiscard]] bool isOk() const { return ok_; }
    [[nodiscard]] ExitCode code() const { return ok_ ? ExitCode::Ok : error_.code; }
    [[nodiscard]] const std::wstring& message() const { return error_.message; }
    [[nodiscard]] const T& value() const { return value_; }
    [[nodiscard]] T& value() { return value_; }

private:
    bool ok_ = true;
    T value_{};
    Error error_;
};

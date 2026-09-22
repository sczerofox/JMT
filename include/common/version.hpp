#pragma once

// 应用名与版本号的单一来源。
// 此前 REPL banner（src/console/repl_engine.cpp）与 `jmt version`
//（src/command/version_command.cpp）各硬编码一份，日期已经分叉；这里统一维护。
namespace jmt {
    inline constexpr const wchar_t* kAppName = L"Java Manager Tool";
    inline constexpr const wchar_t* kVersion = L"v2.0";
    // 不带 v 前缀的纯版本号：用于 User-Agent（JMT/2.0）等需要拼接的场景
    inline constexpr const wchar_t* kVersionNumber = L"2.0";
    inline constexpr const wchar_t* kBuildDate = L"2026.09.23";
}

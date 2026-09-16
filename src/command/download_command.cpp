#include "command/download_command.hpp"
#include "jdk/jdk_download_service.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "jdk/java_env_service.hpp"
#include "system/path_utils.hpp"
#include <windows.h>
#include "platform/output.hpp"
#include "system/utils.hpp"
#include "app/elevation_gate.hpp"
#include "app/env_scope.hpp"
#include <regex>
#include <filesystem>
#include <conio.h>

namespace fs = std::filesystem;

// 辅助：将目录移动到回收站（与 remove 命令复用）
static bool MoveToTrash(const std::wstring& jdkPath, const std::wstring& version, const std::wstring& exeDir, std::wstring& outTrashPath) {
    if (!IsDirectory(jdkPath)) return false;

    std::wstring trashRoot = exeDir + L"\\.trash";
    CreateDirectoryW(trashRoot.c_str(), nullptr);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timeBuf[32];
    swprintf_s(timeBuf, L"%04d%02d%02d_%02d%02d%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::wstring trashDirName = L"jdk-" + version + L"_" + std::wstring(timeBuf);
    std::wstring trashPath = JoinPath(trashRoot, trashDirName);

    BOOL moved = MoveFileW(jdkPath.c_str(), trashPath.c_str());
    if (!moved) {
        try {
            fs::copy(jdkPath, trashPath, fs::copy_options::recursive);
            fs::remove_all(jdkPath);
        } catch (...) {
            return false;
        }
    }
    WriteFileText(JoinPath(trashPath, L".original_path"), jdkPath);
    outTrashPath = trashPath;
    return true;
}

ExitCode DownloadCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    const EnvScope scope = EnvScope::parse(args);
    // ----- 提权 -----

    // ----- 参数检查 -----
    if (scope.args.size() < 2) {
        ctx.out->line(OutputLevel::Error, L"请指定版本号，如 download 21");
        ctx.out->line(OutputLevel::Info, L"可选参数: --mirror  (使用内置镜像加速下载)");
        ctx.out->line(OutputLevel::Info, L"          exe     (强制下载 EXE 安装程序到 .temp，不自动安装)");
        ctx.out->line(OutputLevel::Info, L"          java    (使用官方源下载很慢，自动解压安装)");
        return ExitCode::BadArgs;
    }

    // 解析参数
    bool useMirror = false;
    bool forceOfficial = false;   // ← 新增标志
    bool installerOnly = false;   // exe 参数：只下载 EXE 安装包，不自动安装
    std::wstring versionArg;
    for (size_t i = 1; i < scope.args.size(); ++i) {
        if (scope.args[i] == L"--mirror") {
            useMirror = true;
        } else if (scope.args[i] == L"exe") {
            installerOnly = true;
        } else if (scope.args[i] == L"java") {
            forceOfficial = true;
        } else {
            if (versionArg.empty()) {
                versionArg = scope.args[i];
            }
        }
    }

    if (versionArg.empty()) {
        ctx.out->line(OutputLevel::Error, L"请指定版本号，如 download 21");
        return ExitCode::BadArgs;
    }

    // 提取主版本号
    std::wstring majorVersion;
    std::wregex pattern(L"^(\\d+)");
    std::wsmatch match;
    if (std::regex_search(versionArg, match, pattern) && match.size() > 1) {
        majorVersion = match[1].str();
        if (majorVersion != versionArg) {
            ctx.out->line(OutputLevel::Info, L"检测到具体版本号 " + versionArg + L"，将使用主要版本 " + majorVersion);
        }
    } else {
        ctx.out->line(OutputLevel::Error, L"无效的版本号格式，请输入数字，如 17");
        return ExitCode::BadArgs;
    }

    // ----- 检查是否已存在该版本 -----
    auto jdks = ctx.scan->scanJdks(false, true);
    bool exists = false;
    std::wstring existingPath;
    for (const auto& [ver, path] : jdks) {
        if (ver == majorVersion) {
            exists = true;
            existingPath = path;
            break;
        }
    }

    if (exists) {
        ctx.out->line(OutputLevel::Warning, L"JDK " + majorVersion + L" 已安装在: " + existingPath);
        ctx.out->line(OutputLevel::Info, L"是否删除旧版本并重新下载安装？(y/n)");
        int ch = _getwch();
        if (ch != L'y' && ch != L'Y') {
            ctx.out->line(OutputLevel::Info, L"操作已取消");
            return ExitCode::Ok;
        }
        ctx.out->line(OutputLevel::Info, L""); // 换行

        // 将旧目录移动到回收站
        std::wstring trashPath;
        if (!MoveToTrash(existingPath, majorVersion, ctx.paths.exeDir, trashPath)) {
            ctx.out->line(OutputLevel::Error, L"移动旧版本到回收站失败，请手动删除 " + existingPath);
            return ExitCode::IoOrNetwork;
        }
        ctx.out->line(OutputLevel::Info, L"旧版本已移至回收站: " + trashPath);

        // 从缓存中移除该版本
        auto newJdks = jdks;
        newJdks.erase(std::remove_if(newJdks.begin(), newJdks.end(),
                                     [&](const auto& p) { return p.first == majorVersion; }), newJdks.end());
        ctx.scan->writeCache(newJdks);

        // ---- 更新 PATH：如果当前版本被删除，则切换到最大版本 ----
        std::wstring currentVer = ctx.env->getCurrentVersion();
        if (currentVer == majorVersion) {
            ctx.out->line(OutputLevel::Info, L"当前 PATH 正使用该版本，正在切换到最大版本...");
            if (newJdks.empty()) {
                // 无其他版本，清除 PATH 中的 JDK 路径
                if (!ctx.env->clearCurrentJdk(scope.target)) {
                    ctx.out->line(OutputLevel::Error, L"清除当前 JDK PATH 失败");
                    return ExitCode::PermissionDenied;
                }
                ctx.out->line(OutputLevel::Info, L"已清除当前 JDK PATH（无其他版本）");
            } else {
                auto maxIt = std::max_element(newJdks.begin(), newJdks.end(),
                                              [](const auto& a, const auto& b) {
                                                  return std::stoi(a.first) < std::stoi(b.first);
                                              });
                if (!ctx.env->setCurrentJdk(maxIt->second, scope.target)) {
                    ctx.out->line(OutputLevel::Error, L"切换到最大版本失败");
                    return ExitCode::PermissionDenied;
                }
                ctx.out->line(OutputLevel::Info, L"已切换至最大版本: " + maxIt->first);
            }
        } else {
            // 当前 PATH 不是该版本，但为了安全，从 PATH 中删除该版本的 bin 路径（如果存在）
            std::wstring binPath = JoinPath(existingPath, L"bin");
            std::wstring path = ctx.registry->readPath(scope.target);
            auto entries = PathUtils::splitPath(path);
            entries = PathUtils::removeEntries(entries, binPath);
            std::wstring newPath;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) newPath += L';';
                newPath += entries[i];
            }
            ctx.registry->writePath(newPath, scope.target);
        }

        // 删除任何残留的 JAVA_HOME<version> 变量（向后兼容）
        ctx.registry->deleteEnv(L"JAVA_HOME" + majorVersion, scope.target);
        ctx.out->line(OutputLevel::Info, L"旧版本环境变量已清理");
    }

    // ----- 执行下载 -----
    std::wstring installPath;

    if (installerOnly) {
        // exe 参数优先：只下载 EXE 安装包到 .temp，不自动安装
        installPath = ctx.download->downloadInstallerOnly(majorVersion);
    } else if (forceOfficial) {
        // 强制从官方下载（绕过镜像和 EXE）
        installPath = ctx.download->downloadFromOfficial(majorVersion);
    } else if (useMirror) {
        installPath = ctx.download->downloadFromMirror(majorVersion);
    } else {
        // 默认：尝试镜像 ZIP，失败则回退官方 ZIP
        installPath = ctx.download->downloadAndInstall(majorVersion);
    }

    if (installPath == L"EXE_DOWNLOADED") {
        ctx.out->line(OutputLevel::Info, L"JDK 安装程序已下载到 .temp 目录，请手动完成安装");
        return ExitCode::Ok;
    }

    if (installPath.empty()) {
        ctx.out->line(OutputLevel::Error, L"下载或安装失败");
        return ExitCode::IoOrNetwork;
    }

    // ----- 安装成功，更新缓存并设置当前版本 -----
    // 强制刷新缓存，获取最新列表
    auto updatedJdks = ctx.scan->scanJdks(true, true);
    bool foundNew = false;
    for (const auto& [v, p] : updatedJdks) {
        if (p == installPath || v == majorVersion) { // 若路径匹配或版本匹配
            if (!ctx.env->setCurrentJdk(p, scope.target)) {
                ctx.out->line(OutputLevel::Error, L"设置当前 JDK 到 PATH 失败，请手动执行 'jmt use " + v + L"'");
                return ExitCode::PermissionDenied;
            }
            foundNew = true;
            ctx.out->line(OutputLevel::Success, L"JDK " + v + L" 已安装并设为当前版本");
            break;
        }
    }
    if (!foundNew && !updatedJdks.empty()) {
        // 若未找到新版本（可能名称不一致），则选择最大版本
        auto maxIt = std::max_element(updatedJdks.begin(), updatedJdks.end(),
                                      [](const auto& a, const auto& b) {
                                          return std::stoi(a.first) < std::stoi(b.first);
                                      });
        if (!ctx.env->setCurrentJdk(maxIt->second, scope.target)) {
            ctx.out->line(OutputLevel::Error, L"设置当前 JDK 到 PATH 失败，请手动执行 'jmt use " + maxIt->first + L"'");
            return ExitCode::PermissionDenied;
        }
        ctx.out->line(OutputLevel::Success, L"JDK 安装成功，已自动切换至最大版本 " + maxIt->first);
    } else {
        // 已设置成功
    }

    ctx.out->line(OutputLevel::Info, L"请重启终端使环境变量生效（或新开终端）");
    return ExitCode::Ok;
}

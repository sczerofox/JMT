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
#include "common/java_version.hpp"
#include "jdk/version_match.hpp"
#include <regex>
#include <filesystem>
#include <conio.h>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

// stdin 是否为真实控制台（重定向/管道时不应弹交互提示，避免脚本挂住）
static bool isInputInteractive() {
    DWORD mode = 0;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    return input != nullptr && input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &mode) != 0;
}

// 打印可用源并让用户选择；返回 1 起的源编号（0 = 按顺序自动尝试，-1 = 参数非法）
static int chooseSource(const std::vector<JdkDownloadService::SourceOption>& sources,
                        int explicitChoice, IOutput& out) {
    if (sources.empty()) {
        out.line(OutputLevel::Error, L"该版本没有可用的下载源");
        return -1;
    }

    if (explicitChoice > 0) {
        if (explicitChoice > static_cast<int>(sources.size())) {
            out.line(OutputLevel::Error, L"--source 指定的源不存在（可用范围 1~" +
                                         std::to_wstring(sources.size()) + L"）");
            return -1;
        }
        return explicitChoice;
    }

    size_t totalLinks = 0;
    for (const auto& source : sources) {
        totalLinks += static_cast<size_t>(source.candidateCount);
    }
    out.blank();
    out.line(OutputLevel::Info, L"查找到可用源：" + std::to_wstring(sources.size()) +
                                 L"（候选链接共 " + std::to_wstring(totalLinks) + L" 条）");
    out.blank();
    out.line(OutputLevel::Info, L"请选择要使用的源：");
    for (const auto& source : sources) {
        std::wstring line = L"             " + std::to_wstring(source.index) + L". " + source.name;
        if (source.official) {
            line += L" (速度慢)";   // 官方源需要先解析重定向，且通常比镜像慢
        }
        if (source.candidateCount > 1) {
            line += L"（" + std::to_wstring(source.candidateCount) + L" 条链接）";
        }
        out.line(OutputLevel::Info, line);
    }
    out.blank();

    if (!isInputInteractive()) {
        out.line(OutputLevel::Info, L"（非交互环境：按顺序自动尝试全部源）");
        return 0;
    }

    out.line(OutputLevel::Info, L"请输入您选择的源（回车默认选择: 1 ）：");
    std::wstring input;
    std::getline(std::wcin, input);
    out.blank();   // 用户输入后换行（无回显的终端也能保证下一行输出从行首开始）
    if (input.empty()) {
        return 1;
    }
    try {
        const int choice = std::stoi(input);
        if (choice >= 1 && choice <= static_cast<int>(sources.size())) {
            return choice;
        }
    } catch (const std::exception&) {
        // 落到下面的兜底
    }
    out.line(OutputLevel::Warning, L"输入无效，按顺序自动尝试全部源");
    return 0;
}

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
    int sourceChoice = 0;         // --source N：直接指定源，不弹交互
    std::wstring versionArg;
    for (size_t i = 1; i < scope.args.size(); ++i) {
        if (scope.args[i] == L"--mirror") {
            useMirror = true;
        } else if (scope.args[i] == L"exe") {
            installerOnly = true;
        } else if (scope.args[i] == L"java") {
            forceOfficial = true;
        } else if (scope.args[i] == L"--source" && i + 1 < scope.args.size()) {
            try {
                sourceChoice = std::stoi(scope.args[++i]);
            } catch (const std::exception&) {
                ctx.out->line(OutputLevel::Error, L"--source 需要一个数字，例如 --source 2");
                return ExitCode::BadArgs;
            }
        } else if (scope.args[i].rfind(L"--source=", 0) == 0) {
            try {
                sourceChoice = std::stoi(scope.args[i].substr(9));
            } catch (const std::exception&) {
                ctx.out->line(OutputLevel::Error, L"--source 需要一个数字，例如 --source=2");
                return ExitCode::BadArgs;
            }
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

    // 版本参数保留完整版本（17.0.2 / 8u202 都支持），只做合法性校验
    const JavaVersion requested = JavaVersion::parse(versionArg);
    if (!requested.valid()) {
        ctx.out->line(OutputLevel::Error, L"无效的版本号格式，请输入如 17 或 17.0.2");
        return ExitCode::BadArgs;
    }
    const std::wstring version = requested.raw;
    ctx.out->line(OutputLevel::Info, L"目标版本: " + version);

    // ----- 检查是否已安装 -----
    // 完整版本请求：精确到补丁版本；只给主版本时：该主版本下任意已安装版本
    auto jdks = ctx.scan->scanJdks(false, true);
    const VersionMatch existing = JavaVersion::isFullVersionQuery(version)
                                          ? resolveVersion(jdks, version, true)
                                          : resolveVersion(jdks, version);

    if (existing.found) {
        ctx.out->line(OutputLevel::Warning, L"JDK " + existing.version + L" 已安装在: " + existing.path);
        ctx.out->line(OutputLevel::Info, L"是否删除旧版本并重新下载安装？(y/n)");
        int ch = _getwch();
        if (ch != L'y' && ch != L'Y') {
            ctx.out->line(OutputLevel::Info, L"操作已取消");
            return ExitCode::Ok;
        }
        ctx.out->line(OutputLevel::Info, L""); // 换行

        // 将旧目录移动到回收站
        std::wstring trashPath;
        if (!MoveToTrash(existing.path, existing.version, ctx.paths.exeDir, trashPath)) {
            ctx.out->line(OutputLevel::Error, L"移动旧版本到回收站失败，请手动删除 " + existing.path);
            return ExitCode::IoOrNetwork;
        }
        ctx.out->line(OutputLevel::Info, L"旧版本已移至回收站: " + trashPath);

        // 从缓存中移除该版本
        auto newJdks = jdks;
        newJdks.erase(std::remove_if(newJdks.begin(), newJdks.end(),
                                     [&](const auto& p) { return p.second == existing.path; }), newJdks.end());
        ctx.scan->writeCache(newJdks);

        // ---- 更新 PATH：如果当前版本被删除，则切换到最大版本 ----
        std::wstring currentVer = ctx.env->getCurrentVersion();
        if (currentVer == existing.version) {
            ctx.out->line(OutputLevel::Info, L"当前 PATH 正使用该版本，正在切换到最大版本...");
            if (newJdks.empty()) {
                // 无其他版本，清除 PATH 中的 JDK 路径
                if (!ctx.env->clearCurrentJdk(scope.target)) {
                    ctx.out->line(OutputLevel::Error, L"清除当前 JDK PATH 失败");
                    return ExitCode::PermissionDenied;
                }
                ctx.out->line(OutputLevel::Info, L"已清除当前 JDK PATH（无其他版本）");
            } else {
                const std::wstring maxVer = maxVersion(newJdks);
                std::wstring maxPath;
                for (const auto& [v, p] : newJdks) {
                    if (v == maxVer) { maxPath = p; break; }
                }
                if (!ctx.env->setCurrentJdk(maxPath, scope.target)) {
                    ctx.out->line(OutputLevel::Error, L"切换到最大版本失败");
                    return ExitCode::PermissionDenied;
                }
                ctx.out->line(OutputLevel::Info, L"已切换至最大版本: " + maxVer);
            }
        } else {
            // 当前 PATH 不是该版本，但为了安全，从 PATH 中删除该版本的 bin 路径（如果存在）
            std::wstring binPath = JoinPath(existing.path, L"bin");
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
        ctx.registry->deleteEnv(L"JAVA_HOME" + existing.version, scope.target);
        ctx.out->line(OutputLevel::Info, L"旧版本环境变量已清理");
    }

    // ----- 执行下载 -----
    std::wstring installPath;

    // 列出可用源并（在交互式终端里）让用户选择；--source N 可直接指定
    const DownloadMode mode = installerOnly ? DownloadMode::Default
                                            : (forceOfficial ? DownloadMode::OfficialOnly
                                                             : (useMirror ? DownloadMode::MirrorOnly
                                                                          : DownloadMode::Default));
    const std::vector<JdkDownloadService::SourceOption> sources =
            ctx.download->listSources(version, mode, installerOnly);
    const int preferredSource = chooseSource(sources, sourceChoice, *ctx.out);
    if (preferredSource < 0) {
        return ExitCode::BadArgs;
    }

    if (installerOnly) {
        // exe 参数优先：只下载 EXE 安装包到 .temp，不自动安装
        installPath = ctx.download->downloadInstallerOnly(version, preferredSource);
    } else if (forceOfficial) {
        // 强制从官方下载（绕过镜像和 EXE）
        installPath = ctx.download->downloadFromOfficial(version, L"", preferredSource);
    } else if (useMirror) {
        installPath = ctx.download->downloadFromMirror(version, L"", preferredSource);
    } else {
        // 默认：尝试镜像 ZIP，失败则回退官方 ZIP
        installPath = ctx.download->downloadAndInstall(version, L"", preferredSource);
    }

    if (installPath == L"EXE_DOWNLOADED") {
        ctx.out->line(OutputLevel::Info,
                      L"JDK 安装程序已下载到 .temp 目录，请手动完成安装"
                      L"(注意：请定期清理.temp文件夹 防止文件占用磁盘空间 清理命令 remove temp)");
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
        if (p == installPath) { // 按安装路径匹配，避免把同主版本的其它补丁版本当成新装版本
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
        const std::wstring fallbackVersion = maxVersion(updatedJdks);
        std::wstring fallbackPath;
        for (const auto& [v, p] : updatedJdks) {
            if (v == fallbackVersion) { fallbackPath = p; break; }
        }
        if (!ctx.env->setCurrentJdk(fallbackPath, scope.target)) {
            ctx.out->line(OutputLevel::Error, L"设置当前 JDK 到 PATH 失败，请手动执行 'jmt use " + fallbackVersion + L"'");
            return ExitCode::PermissionDenied;
        }
        ctx.out->line(OutputLevel::Success, L"JDK 安装成功，已自动切换至最大版本 " + fallbackVersion);
    } else {
        // 已设置成功
    }

    ctx.out->line(OutputLevel::Info, L"请重启终端使环境变量生效（或新开终端）");
    return ExitCode::Ok;
}

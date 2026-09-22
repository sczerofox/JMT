#include "command/download_command.hpp"
#include "jdk/jdk_download_service.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "jdk/java_env_service.hpp"
#include "system/path_utils.hpp"
#include <windows.h>
#include "platform/output.hpp"
#include "system/utils.hpp"
#include "common/java_version.hpp"
#include "jdk/version_match.hpp"
#include <filesystem>
#include <conio.h>
#include <string>

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
    // ----- 参数检查 -----
    if (args.size() < 2) {
        ctx.out->line(OutputLevel::Error, L"请指定版本号，如 download 21");
        ctx.out->line(OutputLevel::Info, L"能下载到 ZIP 时自动解压安装；只有安装包时下载后提示手动安装");
        return ExitCode::BadArgs;
    }

    // 解析参数（现在没有开关：ZIP 优先、安装包兜底由下载服务自动决定）
    std::wstring versionArg;
    for (size_t i = 1; i < args.size(); ++i) {
        if (versionArg.empty()) {
            versionArg = args[i];
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
    // 目标版本的打印由下载服务统一输出（避免两处各打一遍）

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
                if (!ctx.env->clearCurrentJdk(EnvTarget::Auto)) {
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
                if (!ctx.env->setCurrentJdk(maxPath, EnvTarget::Auto)) {
                    ctx.out->line(OutputLevel::Error, L"切换到最大版本失败");
                    return ExitCode::PermissionDenied;
                }
                ctx.out->line(OutputLevel::Info, L"已切换至最大版本: " + maxVer);
            }
        } else {
            // 当前 PATH 不是该版本，但为了安全，从 PATH 中删除该版本的 bin 路径（如果存在）
            std::wstring binPath = JoinPath(existing.path, L"bin");
            std::wstring path = ctx.registry->readPath(EnvTarget::Auto);
            auto entries = PathUtils::splitPath(path);
            entries = PathUtils::removeEntries(entries, binPath);
            std::wstring newPath;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) newPath += L';';
                newPath += entries[i];
            }
            ctx.registry->writePath(newPath, EnvTarget::Auto);
        }

        // 删除任何残留的 JAVA_HOME<version> 变量（向后兼容）
        ctx.registry->deleteEnv(L"JAVA_HOME" + existing.version, EnvTarget::Auto);
        ctx.out->line(OutputLevel::Info, L"旧版本环境变量已清理");
    }

    // ----- 执行下载 -----
    // 流程由 JdkDownloadService 决定：能下 ZIP 就下 ZIP 并自动安装；
    // 只有安装包（MSI/EXE）时下载后提示手动安装；源按优先级自动回退。
    const std::wstring installPath = ctx.download->downloadAndInstall(version);

    if (installPath == L"EXE_DOWNLOADED") {
        // 详细的手动安装提示已由下载服务打印，这里只提醒清理 .temp
        ctx.out->line(OutputLevel::Info,
                      L"注意：请定期清理 .temp 文件夹，防止占用磁盘空间（清理命令：remove temp）");
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
            if (!ctx.env->setCurrentJdk(p, EnvTarget::Auto)) {
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
        if (!ctx.env->setCurrentJdk(fallbackPath, EnvTarget::Auto)) {
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

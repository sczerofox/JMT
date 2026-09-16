#include "command/remove_command.hpp"
#include "jdk/java_env_service.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "system/path_utils.hpp"
#include <windows.h>
#include "platform/output.hpp"
#include "system/utils.hpp"
#include "app/elevation_gate.hpp"
#include <algorithm>
#include <cctype>
#include <conio.h>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

ExitCode RemoveCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    // 解析 --user / --sys
    EnvTarget target = EnvTarget::Auto;
    std::vector<std::wstring> filteredArgs;
    for (const auto& arg : args) {
        if (arg == L"--user") { target = EnvTarget::UserOnly; }
        else if (arg == L"--sys") { target = EnvTarget::SystemOnly; }
        else {
            filteredArgs.push_back(arg);
        }
    }

    // 提权检查

    // 检查是否有子命令
    if (filteredArgs.size() < 2) {
        ctx.out->line(OutputLevel::Error, L"用法: remove <子命令> [--user|--sys]");
        ctx.out->line(OutputLevel::Info, L"  子命令: env      - 清理 JMT 管理的 JDK PATH 和 JMT 自身 PATH，恢复 Oracle javapath");
        ctx.out->line(OutputLevel::Info, L"           all     - 完全清理所有 JMT 相关 PATH 条目和缓存");
        ctx.out->line(OutputLevel::Info, L"           temp    - 删除 .temp 下载缓存目录");
        ctx.out->line(OutputLevel::Info, L"           trash   - 永久清空回收站 (.trash)");
        ctx.out->line(OutputLevel::Info, L"           <版本号> - 删除指定版本的 JDK，自动切换到最大版本（若为当前版本）");
        ctx.out->line(OutputLevel::Info, L"  示例: remove env, remove all, remove 17, remove --user env");
        return ExitCode::BadArgs;
    }

    const std::wstring& subCmd = filteredArgs[1];

    // ---------- remove env ----------
    if (subCmd == L"env") {
        ctx.out->line(OutputLevel::Info, L"正在从 PATH 中移除 JMT 自身目录...");

        std::wstring path = ctx.registry->readPath(target);
        auto entries = PathUtils::splitPath(path);
        entries = PathUtils::removeEntries(entries, ctx.paths.exeDir);
        std::wstring newPath;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) newPath += L';';
            newPath += entries[i];
        }
        ctx.registry->writePath(newPath, target);

        ctx.out->line(OutputLevel::Success, L"JMT 自身目录已从 PATH 中移除");
        return ExitCode::Ok;
    }

    // ---------- remove all ----------
    if (subCmd == L"all") {
        ctx.out->line(OutputLevel::Warning, L"此操作将清除所有 JMT 配置，包括：");
        ctx.out->line(OutputLevel::Info, L"  - 从 PATH 中移除 JMT 自身目录和当前 JDK 路径");
        ctx.out->line(OutputLevel::Info, L"  - 恢复 Oracle javapath 环境变量");
        ctx.out->line(OutputLevel::Info, L"  - 删除 .trash 回收站");
        ctx.out->line(OutputLevel::Info, L"  - 删除 .temp 下载缓存");
        ctx.out->line(OutputLevel::Info, L"请输入 'y' 确认，其他任意键取消：");
        int ch = _getwch();
        if (ch != L'y' && ch != L'Y') {
            ctx.out->line(OutputLevel::Info, L"操作已取消");
            return ExitCode::Ok;
        }
        ctx.out->line(OutputLevel::Info, L""); // 换行

        ctx.out->line(OutputLevel::Info, L"正在完全清理所有 JMT 配置...");

        // 1. 清除当前 JDK（删除 PATH 条目，恢复 Oracle javapath）
        if (!ctx.env->clearCurrentJdk(target)) {
            ctx.out->line(OutputLevel::Error, L"清除当前 JDK PATH 失败");
        } else {
            ctx.out->line(OutputLevel::Info, L"已清除当前 JDK PATH，已恢复 Oracle javapath");
        }

        // 2. 删除 JMT 自身 PATH 条目
        {
            std::wstring path = ctx.registry->readPath(target);
            auto entries = PathUtils::splitPath(path);
            entries = PathUtils::removeEntries(entries, ctx.paths.exeDir);
            std::wstring newPath;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) newPath += L';';
                newPath += entries[i];
            }
            ctx.registry->writePath(newPath, target);
            ctx.out->line(OutputLevel::Info, L"已从 PATH 中移除 JMT 自身目录");
        }

        // 3. 删除 .trash 回收站
        {
            std::wstring trashRoot = ctx.paths.trashDir;
            if (IsDirectory(trashRoot)) {
                try {
                    fs::remove_all(trashRoot);
                    ctx.out->line(OutputLevel::Info, L"已删除 .trash 回收站");
                } catch (const std::exception& e) {
                    ctx.out->line(OutputLevel::Warning, L"删除 .trash 失败: " + ToWideString(e.what()));
                }
            } else {
                ctx.out->line(OutputLevel::Info, L".trash 不存在，跳过");
            }
        }

        // 4. 删除 .temp 下载缓存
        {
            std::wstring tempDir = ctx.paths.tempDir;
            if (IsDirectory(tempDir)) {
                try {
                    fs::remove_all(tempDir);
                    ctx.out->line(OutputLevel::Info, L"已删除 .temp 下载缓存");
                } catch (const std::exception& e) {
                    ctx.out->line(OutputLevel::Warning, L"删除 .temp 失败: " + ToWideString(e.what()));
                }
            } else {
                ctx.out->line(OutputLevel::Info, L".temp 不存在，跳过");
            }
        }

        // 5. 删除缓存文件
        if (IsFile(ctx.paths.cacheFile)) {
            DeleteFileW(ctx.paths.cacheFile.c_str());
            ctx.out->line(OutputLevel::Info, L"已删除缓存文件");
        }

        // 6. 清理所有 JAVA_HOME* 变量
        auto varNames = ctx.registry->listEnvNames(target);
        bool hasJavaHome = false;
        for (const auto& name : varNames) {
            if (name.find(L"JAVA_HOME") == 0) {
                ctx.registry->deleteEnv(name, target);
                hasJavaHome = true;
            }
        }
        if (hasJavaHome) {
            ctx.out->line(OutputLevel::Info, L"已清理 JAVA_HOME 环境变量");
        }

        ctx.out->line(OutputLevel::Success, L"完全清理完成");
        return ExitCode::Ok;
    }

    // ---------- remove temp ----------
    if (subCmd == L"temp") {
        std::wstring tempDir = ctx.paths.tempDir;
        if (!IsDirectory(tempDir)) {
            ctx.out->line(OutputLevel::Info, L".temp 目录不存在，无需清理");
            return ExitCode::Ok;
        }
        try {
            fs::remove_all(tempDir);
            ctx.out->line(OutputLevel::Success, L"已删除 .temp 目录");
        } catch (const std::exception& e) {
            ctx.out->line(OutputLevel::Error, L"删除 .temp 目录失败: " + ToWideString(e.what()));
            return ExitCode::PermissionDenied;
        }
        return ExitCode::Ok;
    }

    // ---------- remove trash ----------
    if (subCmd == L"trash") {
        std::wstring trashRoot = ctx.paths.trashDir;
        if (!IsDirectory(trashRoot)) {
            ctx.out->line(OutputLevel::Info, L"回收站不存在，无需清理");
            return ExitCode::Ok;
        }

        ctx.out->line(OutputLevel::Warning, L"此操作将永久删除回收站中的所有 JDK 备份，不可恢复！");
        ctx.out->line(OutputLevel::Info, L"请输入 'y' 确认，其他任意键取消：");
        int ch = _getwch();
        if (ch != L'y' && ch != L'Y') {
            ctx.out->line(OutputLevel::Info, L"操作已取消");
            return ExitCode::Ok;
        }
        ctx.out->line(OutputLevel::Info, L""); // 换行

        try {
            fs::remove_all(trashRoot);
            ctx.out->line(OutputLevel::Success, L"回收站已清空");
        } catch (const std::exception& e) {
            std::wstring errMsg = ToWideString(e.what());
            ctx.out->line(OutputLevel::Error, L"清空失败: " + errMsg);
            return ExitCode::PermissionDenied;
        }
        return ExitCode::Ok;
    }

    // ---------- remove <version> ----------
    if (std::regex_match(subCmd, std::wregex(L"^\\d+$"))) {
        std::wstring version = subCmd;
        ctx.out->line(OutputLevel::Info, L"正在删除 JDK 版本 " + version + L" ...");

        // 获取当前 JDK 列表（使用缓存，若缓存无效则强制扫描）
        auto jdks = ctx.scan->scanJdks(false, true);
        if (jdks.empty()) {
            ctx.out->line(OutputLevel::Info, L"缓存为空，强制扫描...");
            jdks = ctx.scan->scanJdks(true, true);
            if (jdks.empty()) {
                ctx.out->line(OutputLevel::Warning, L"未找到任何 JDK");
                return ExitCode::NotFound;
            }
        }

        // 查找要删除的版本
        auto it = std::find_if(jdks.begin(), jdks.end(),
                               [&](const auto& p) { return p.first == version; });
        if (it == jdks.end()) {
            ctx.out->line(OutputLevel::Error, L"未找到版本 " + version + L" 的 JDK");
            return ExitCode::NotFound;
        }

        std::wstring jdkPath = it->second;

        // ---- 处理 PATH 切换（如果删除的是当前版本） ----
        std::wstring currentVer = ctx.env->getCurrentVersion();
        if (currentVer == version) {
            // 需要切换到其他版本（选最大）
            std::vector<std::pair<std::wstring, std::wstring>> otherJdks;
            for (const auto& [v, p] : jdks) {
                if (v != version) otherJdks.push_back({v, p});
            }
            if (!otherJdks.empty()) {
                auto maxIt = std::max_element(otherJdks.begin(), otherJdks.end(),
                                              [](const auto& a, const auto& b) {
                                                  return std::stoi(a.first) < std::stoi(b.first);
                                              });
                if (!ctx.env->setCurrentJdk(maxIt->second, target)) {
                    ctx.out->line(OutputLevel::Error, L"切换到最大版本失败");
                    return ExitCode::PermissionDenied;
                }
                ctx.out->line(OutputLevel::Info, L"已自动切换到版本 " + maxIt->first);
            } else {
                // 无其他版本，清除当前 JDK
                if (!ctx.env->clearCurrentJdk(target)) {
                    ctx.out->line(OutputLevel::Error, L"清除当前 JDK PATH 失败");
                    return ExitCode::PermissionDenied;
                }
                ctx.out->line(OutputLevel::Info, L"已清除当前 JDK PATH（无其他版本）");
            }
        } else {
            // 删除的不是当前版本，但为了防止 PATH 中残留该版本的 bin 路径，尝试移除
            std::wstring binPath = JoinPath(jdkPath, L"bin");
            std::wstring path = ctx.registry->readPath(target);
            auto entries = PathUtils::splitPath(path);
            entries = PathUtils::removeEntries(entries, binPath);
            std::wstring newPath;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) newPath += L';';
                newPath += entries[i];
            }
            ctx.registry->writePath(newPath, target);
        }

        // ---- 将 JDK 目录移动到回收站 ----
        if (IsDirectory(jdkPath)) {
            std::wstring trashRoot = ctx.paths.trashDir;
            CreateDirectoryW(trashRoot.c_str(), nullptr);

            // 生成唯一目录名：jdk-<版本>_<时间戳>
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t timeBuf[32];
            swprintf_s(timeBuf, L"%04d%02d%02d_%02d%02d%02d",
                       st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            std::wstring trashDirName = L"jdk-" + version + L"_" + std::wstring(timeBuf);
            std::wstring trashPath = JoinPath(trashRoot, trashDirName);

            BOOL moved = MoveFileW(jdkPath.c_str(), trashPath.c_str());
            if (!moved) {
                // 如果跨卷，MoveFileW 会失败，改用复制+删除
                ctx.out->line(OutputLevel::Info, L"移动失败（可能跨卷），正在复制并删除原目录...");
                try {
                    fs::copy(jdkPath, trashPath, fs::copy_options::recursive);
                    fs::remove_all(jdkPath);
                    ctx.out->line(OutputLevel::Info, L"复制完成，原目录已删除");
                } catch (const std::exception& e) {
                    ctx.out->line(OutputLevel::Error, L"复制或删除失败: " + ToWideString(e.what()));
                    return ExitCode::IoOrNetwork;
                }
            } else {
                ctx.out->line(OutputLevel::Info, L"已移动到回收站: " + trashPath);
            }

            // 写入元数据文件（记录原始路径）
            std::wstring metaPath = JoinPath(trashPath, L".original_path");
            WriteFileText(metaPath, jdkPath);
        } else {
            ctx.out->line(OutputLevel::Warning, L"目录不存在: " + jdkPath);
        }

        // ---- 更新缓存（移除已删除版本） ----
        auto newJdks = jdks;
        newJdks.erase(std::remove_if(newJdks.begin(), newJdks.end(),
                                     [&](const auto& p) { return p.first == version; }), newJdks.end());
        ctx.scan->writeCache(newJdks);

        // ---- 删除任何残留的 JAVA_HOME<version> 变量（向后兼容） ----
        ctx.registry->deleteEnv(L"JAVA_HOME" + version, target);

        ctx.out->line(OutputLevel::Success, L"JDK " + version + L" 已移至回收站并清理相关配置");
        ctx.out->line(OutputLevel::Info, L"如需还原，请使用 'jmt rollback " + version + L"'");
        return ExitCode::Ok;
    }

    // 未知子命令
    ctx.out->line(OutputLevel::Error, L"未知子命令: " + subCmd);
    ctx.out->line(OutputLevel::Info, L"可用子命令: env, all, temp, trash, <版本号>");
    return ExitCode::BadArgs;
}
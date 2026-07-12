#include "remove_command.hpp"
#include "../service/java_env_service.hpp"
#include "../service/jdk_scan_service.hpp"
#include "../infrastructure/path_utils.hpp"
#include "../print/color_print.hpp"
#include "../utils/utils.hpp"
#include "../infrastructure/elevation_helper.hpp"
#include <algorithm>
#include <cctype>
#include <conio.h>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

int RemoveCommand::execute(const std::vector<std::wstring>& args, JmtContext& ctx) {
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
    if (!ctx.isElevated) {
        std::wstring cmdLine;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) cmdLine += L' ';
            if (args[i].find(L' ') != std::wstring::npos)
                cmdLine += L'"' + args[i] + L'"';
            else
                cmdLine += args[i];
        }
        PrintInfo(L"需要管理员权限，正在请求提权...");
        if (ElevationHelper::RelaunchElevated(cmdLine)) {
            return 0;
        } else {
            PrintError(L"提权失败，请手动以管理员身份运行");
            return 3;
        }
    }

    // 检查是否有子命令
    if (filteredArgs.size() < 2) {
        PrintError(L"用法: remove <子命令> [--user|--sys]");
        PrintInfo(L"  子命令: env      - 清理 JMT 管理的 JDK PATH 和 JMT 自身 PATH，恢复 Oracle javapath");
        PrintInfo(L"           all     - 完全清理所有 JMT 相关 PATH 条目和缓存");
        PrintInfo(L"           temp    - 删除 .temp 下载缓存目录");
        PrintInfo(L"           trash   - 永久清空回收站 (.trash)");
        PrintInfo(L"           <版本号> - 删除指定版本的 JDK，自动切换到最大版本（若为当前版本）");
        PrintInfo(L"  示例: remove env, remove all, remove 17, remove --user env");
        return 1;
    }

    const std::wstring& subCmd = filteredArgs[1];

    // ---------- remove env ----------
    if (subCmd == L"env") {
        PrintInfo(L"正在从 PATH 中移除 JMT 自身目录...");

        std::wstring path = RegistryOperator::getPath(target);
        auto entries = PathUtils::splitPath(path);
        entries = PathUtils::removeEntries(entries, ctx.exeDirectory);
        std::wstring newPath;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) newPath += L';';
            newPath += entries[i];
        }
        RegistryOperator::setPath(newPath, target);

        PrintSuccess(L"JMT 自身目录已从 PATH 中移除");
        return 0;
    }

    // ---------- remove all ----------
    if (subCmd == L"all") {
        PrintWarning(L"此操作将清除所有 JMT 配置，包括：");
        PrintInfo(L"  - 从 PATH 中移除 JMT 自身目录和当前 JDK 路径");
        PrintInfo(L"  - 恢复 Oracle javapath 环境变量");
        PrintInfo(L"  - 删除 .trash 回收站");
        PrintInfo(L"  - 删除 .temp 下载缓存");
        PrintInfo(L"请输入 'y' 确认，其他任意键取消：");
        int ch = _getwch();
        if (ch != L'y' && ch != L'Y') {
            PrintInfo(L"操作已取消");
            return 0;
        }
        PrintInfo(L""); // 换行

        PrintInfo(L"正在完全清理所有 JMT 配置...");

        // 1. 清除当前 JDK（删除 PATH 条目，恢复 Oracle javapath）
        if (!JavaEnvService::clearCurrentJdk(target)) {
            PrintError(L"清除当前 JDK PATH 失败");
        } else {
            PrintInfo(L"已清除当前 JDK PATH，已恢复 Oracle javapath");
        }

        // 2. 删除 JMT 自身 PATH 条目
        {
            std::wstring path = RegistryOperator::getPath(target);
            auto entries = PathUtils::splitPath(path);
            entries = PathUtils::removeEntries(entries, ctx.exeDirectory);
            std::wstring newPath;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) newPath += L';';
                newPath += entries[i];
            }
            RegistryOperator::setPath(newPath, target);
            PrintInfo(L"已从 PATH 中移除 JMT 自身目录");
        }

        // 3. 删除 .trash 回收站
        {
            std::wstring trashRoot = ctx.exeDirectory + L"\\.trash";
            if (IsDirectory(trashRoot)) {
                try {
                    fs::remove_all(trashRoot);
                    PrintInfo(L"已删除 .trash 回收站");
                } catch (const std::exception& e) {
                    PrintWarning(L"删除 .trash 失败: " + ToWideString(e.what()));
                }
            } else {
                PrintInfo(L".trash 不存在，跳过");
            }
        }

        // 4. 删除 .temp 下载缓存
        {
            std::wstring tempDir = JoinPath(ctx.exeDirectory, L".temp");
            if (IsDirectory(tempDir)) {
                try {
                    fs::remove_all(tempDir);
                    PrintInfo(L"已删除 .temp 下载缓存");
                } catch (const std::exception& e) {
                    PrintWarning(L"删除 .temp 失败: " + ToWideString(e.what()));
                }
            } else {
                PrintInfo(L".temp 不存在，跳过");
            }
        }

        // 5. 删除缓存文件
        if (IsFile(ctx.cacheFilePath)) {
            DeleteFileW(ctx.cacheFilePath.c_str());
            PrintInfo(L"已删除缓存文件");
        }

        // 6. 清理所有 JAVA_HOME* 变量
        auto varNames = RegistryOperator::enumerateEnvValueNames(target);
        bool hasJavaHome = false;
        for (const auto& name : varNames) {
            if (name.find(L"JAVA_HOME") == 0) {
                RegistryOperator::deleteEnvString(name, target);
                hasJavaHome = true;
            }
        }
        if (hasJavaHome) {
            PrintInfo(L"已清理 JAVA_HOME 环境变量");
        }

        PrintSuccess(L"完全清理完成");
        return 0;
    }

    // ---------- remove temp ----------
    if (subCmd == L"temp") {
        std::wstring tempDir = JoinPath(ctx.exeDirectory, L".temp");
        if (!IsDirectory(tempDir)) {
            PrintInfo(L".temp 目录不存在，无需清理");
            return 0;
        }
        try {
            fs::remove_all(tempDir);
            PrintSuccess(L"已删除 .temp 目录");
        } catch (const std::exception& e) {
            PrintError(L"删除 .temp 目录失败: " + ToWideString(e.what()));
            return 3;
        }
        return 0;
    }

    // ---------- remove trash ----------
    if (subCmd == L"trash") {
        std::wstring trashRoot = ctx.exeDirectory + L"\\.trash";
        if (!IsDirectory(trashRoot)) {
            PrintInfo(L"回收站不存在，无需清理");
            return 0;
        }

        PrintWarning(L"此操作将永久删除回收站中的所有 JDK 备份，不可恢复！");
        PrintInfo(L"请输入 'y' 确认，其他任意键取消：");
        int ch = _getwch();
        if (ch != L'y' && ch != L'Y') {
            PrintInfo(L"操作已取消");
            return 0;
        }
        PrintInfo(L""); // 换行

        try {
            fs::remove_all(trashRoot);
            PrintSuccess(L"回收站已清空");
        } catch (const std::exception& e) {
            std::wstring errMsg = ToWideString(e.what());
            PrintError(L"清空失败: " + errMsg);
            return 3;
        }
        return 0;
    }

    // ---------- remove <version> ----------
    if (std::regex_match(subCmd, std::wregex(L"^\\d+$"))) {
        std::wstring version = subCmd;
        PrintInfo(L"正在删除 JDK 版本 " + version + L" ...");

        // 获取当前 JDK 列表（使用缓存，若缓存无效则强制扫描）
        auto jdks = JdkScanService::scanJdks(false, ctx.cacheFilePath, true);
        if (jdks.empty()) {
            PrintInfo(L"缓存为空，强制扫描...");
            jdks = JdkScanService::scanJdks(true, ctx.cacheFilePath, true);
            if (jdks.empty()) {
                PrintWarning(L"未找到任何 JDK");
                return 2;
            }
        }

        // 查找要删除的版本
        auto it = std::find_if(jdks.begin(), jdks.end(),
                               [&](const auto& p) { return p.first == version; });
        if (it == jdks.end()) {
            PrintError(L"未找到版本 " + version + L" 的 JDK");
            return 2;
        }

        std::wstring jdkPath = it->second;

        // ---- 处理 PATH 切换（如果删除的是当前版本） ----
        std::wstring currentVer = JavaEnvService::getCurrentVersion();
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
                if (!JavaEnvService::setCurrentJdk(maxIt->second, target)) {
                    PrintError(L"切换到最大版本失败");
                    return 3;
                }
                PrintInfo(L"已自动切换到版本 " + maxIt->first);
            } else {
                // 无其他版本，清除当前 JDK
                if (!JavaEnvService::clearCurrentJdk(target)) {
                    PrintError(L"清除当前 JDK PATH 失败");
                    return 3;
                }
                PrintInfo(L"已清除当前 JDK PATH（无其他版本）");
            }
        } else {
            // 删除的不是当前版本，但为了防止 PATH 中残留该版本的 bin 路径，尝试移除
            std::wstring binPath = JoinPath(jdkPath, L"bin");
            std::wstring path = RegistryOperator::getPath(target);
            auto entries = PathUtils::splitPath(path);
            entries = PathUtils::removeEntries(entries, binPath);
            std::wstring newPath;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) newPath += L';';
                newPath += entries[i];
            }
            RegistryOperator::setPath(newPath, target);
        }

        // ---- 将 JDK 目录移动到回收站 ----
        if (IsDirectory(jdkPath)) {
            std::wstring trashRoot = ctx.exeDirectory + L"\\.trash";
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
                PrintInfo(L"移动失败（可能跨卷），正在复制并删除原目录...");
                try {
                    fs::copy(jdkPath, trashPath, fs::copy_options::recursive);
                    fs::remove_all(jdkPath);
                    PrintInfo(L"复制完成，原目录已删除");
                } catch (const std::exception& e) {
                    PrintError(L"复制或删除失败: " + ToWideString(e.what()));
                    return 4;
                }
            } else {
                PrintInfo(L"已移动到回收站: " + trashPath);
            }

            // 写入元数据文件（记录原始路径）
            std::wstring metaPath = JoinPath(trashPath, L".original_path");
            WriteFileText(metaPath, jdkPath);
        } else {
            PrintWarning(L"目录不存在: " + jdkPath);
        }

        // ---- 更新缓存（移除已删除版本） ----
        auto newJdks = jdks;
        newJdks.erase(std::remove_if(newJdks.begin(), newJdks.end(),
                                     [&](const auto& p) { return p.first == version; }), newJdks.end());
        JdkScanService::writeCache(newJdks, ctx.cacheFilePath);

        // ---- 删除任何残留的 JAVA_HOME<version> 变量（向后兼容） ----
        RegistryOperator::deleteEnvString(L"JAVA_HOME" + version, target);

        PrintSuccess(L"JDK " + version + L" 已移至回收站并清理相关配置");
        PrintInfo(L"如需还原，请使用 'jmt rollback " + version + L"'");
        return 0;
    }

    // 未知子命令
    PrintError(L"未知子命令: " + subCmd);
    PrintInfo(L"可用子命令: env, all, temp, trash, <版本号>");
    return 1;
}
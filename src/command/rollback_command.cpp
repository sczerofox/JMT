#include "command/rollback_command.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "console/color_print.hpp"
#include "system/utils.hpp"
#include "app/elevation_gate.hpp"
#include <windows.h>
#include <filesystem>
#include <regex>
#include <shellapi.h>
#include <set>

namespace fs = std::filesystem;

// 辅助：递归创建目录
static bool CreateDirectoryRecursive(const std::wstring& path) {
    if (IsDirectory(path)) return true;
    size_t pos = path.find_last_of(L'\\');
    if (pos != std::wstring::npos) {
        std::wstring parent = path.substr(0, pos);
        if (!CreateDirectoryRecursive(parent)) return false;
    }
    return CreateDirectoryW(path.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

// 辅助：彻底清理路径字符串（去除所有不可见字符和末尾反斜杠）
static std::wstring CleanPath(const std::wstring& raw) {
    std::wstring cleaned = raw;
    cleaned.erase(std::remove_if(cleaned.begin(), cleaned.end(),
                                 [](wchar_t ch) { return ch < 0x20; }), cleaned.end());
    while (!cleaned.empty() && (cleaned.back() == L'\\' || cleaned.back() == L'/'))
        cleaned.pop_back();
    return cleaned;
}

// 辅助：检查路径是否真实存在（文件或目录）
static bool PathExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES);
}

ExitCode RollbackCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    // ----- 0. 参数检查 -----
    if (args.size() < 2) {
        PrintError(L"用法: rollback <version> 或 rollback list");
        PrintInfo(L"  list  - 显示回收站中所有可回退的版本");
        return ExitCode::BadArgs;
    }

    // ----- 1. list 子命令（无需提权）-----
    if (args[1] == L"list") {
        std::wstring trashRoot = ctx.paths.trashDir;
        if (!IsDirectory(trashRoot)) {
            PrintInfo(L"回收站为空，没有可回退的版本");
            return ExitCode::Ok;
        }

        std::wstring searchPattern = trashRoot + L"\\jdk-*";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) {
            PrintInfo(L"回收站为空，没有可回退的版本");
            return ExitCode::Ok;
        }

        std::vector<std::pair<std::wstring, std::wstring>> entries;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::wstring dirName = fd.cFileName;
                std::wregex pattern(L"jdk-(\\d+)_");
                std::wsmatch match;
                if (std::regex_search(dirName, match, pattern) && match.size() > 1) {
                    std::wstring version = match[1].str();
                    std::wstring fullPath = JoinPath(trashRoot, dirName);
                    std::wstring metaPath = JoinPath(fullPath, L".original_path");
                    std::wstring originalPath = ReadFileText(metaPath);
                    if (!originalPath.empty()) {
                        originalPath = CleanPath(originalPath);
                        entries.push_back({version, originalPath});
                    }
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);

        if (entries.empty()) {
            PrintInfo(L"回收站中没有有效的 JDK 备份（元数据可能丢失）");
            return ExitCode::Ok;
        }

        PrintInfo(L"回收站中可回退的 JDK 版本：");
        for (const auto& [ver, path] : entries) {
            PrintInfo(L"  JDK " + ver + L"  -> 原始路径: " + path);
        }
        PrintSuccess(L"共 " + std::to_wstring(entries.size()) + L" 个版本可回退");
        PrintInfo(L"使用 'rollback <版本号>' 恢复指定版本");
        return ExitCode::Ok;
    }

    // ----- 2. 提权（非 list 子命令）-----
    if (!ctx.isElevated) {
        const ElevationDecision decision = ElevationGate::requestElevation(args, ctx);
        if (!decision.proceed) return decision.code;
    }

    // ----- 3. 恢复指定版本 -----
    std::wstring version = args[1];

    std::wstring trashRoot = ctx.paths.trashDir;
    if (!IsDirectory(trashRoot)) {
        PrintError(L"回收站不存在，没有可回退的版本");
        return ExitCode::NotFound;
    }

    // 查找匹配的最新条目
    std::wstring searchPattern = trashRoot + L"\\jdk-" + version + L"_*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        PrintError(L"未找到版本 " + version + L" 的回收条目");
        return ExitCode::NotFound;
    }

    std::wstring latestDir;
    FILETIME latestTime = {0, 0};
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (CompareFileTime(&fd.ftCreationTime, &latestTime) > 0) {
                latestTime = fd.ftCreationTime;
                latestDir = fd.cFileName;
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (latestDir.empty()) {
        PrintError(L"未找到有效的回收条目");
        return ExitCode::NotFound;
    }

    std::wstring trashPath = JoinPath(trashRoot, latestDir);
    std::wstring metaPath = JoinPath(trashPath, L".original_path");
    std::wstring originalPath = ReadFileText(metaPath);
    if (originalPath.empty()) {
        PrintError(L"元数据丢失，无法还原");
        return ExitCode::IoOrNetwork;
    }

    originalPath = CleanPath(originalPath);
    PrintInfo(L"原始路径（清洗后）: " + originalPath);

    if (PathExists(originalPath)) {
        PrintError(L"原路径已存在，请手动处理: " + originalPath);
        return ExitCode::NotFound;
    }

    // ----- 确保目标父目录存在 -----
    std::wstring parentDir = originalPath;
    size_t pos = parentDir.find_last_of(L'\\');
    if (pos != std::wstring::npos) {
        parentDir = parentDir.substr(0, pos);
        if (!CreateDirectoryRecursive(parentDir)) {
            PrintError(L"无法创建目标父目录: " + parentDir + L" (错误码: " + std::to_wstring(GetLastError()) + L")");
            return ExitCode::PermissionDenied;
        }
    }

    // ----- 尝试移动（同卷），失败则使用 xcopy -----
    BOOL moved = MoveFileW(trashPath.c_str(), originalPath.c_str());
    bool restoreSuccess = false;

    if (moved) {
        PrintInfo(L"移动成功");
        restoreSuccess = true;
    } else {
        DWORD err = GetLastError();
        PrintInfo(L"移动失败（错误码: " + std::to_wstring(err) + L"），尝试使用 xcopy 复制...");

        std::wstring xcopyCmd = L"/c xcopy /E /I /Y \"" + trashPath + L"\" \"" + originalPath + L"\"";
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = L"cmd.exe";
        sei.lpParameters = xcopyCmd.c_str();
        sei.nShow = SW_HIDE;

        if (ShellExecuteExW(&sei)) {
            WaitForSingleObject(sei.hProcess, INFINITE);
            DWORD exitCode;
            GetExitCodeProcess(sei.hProcess, &exitCode);
            CloseHandle(sei.hProcess);

            if (exitCode == 0) {
                try {
                    fs::remove_all(trashPath);
                    PrintInfo(L"复制成功，已删除回收站副本");
                    restoreSuccess = true;
                } catch (...) {
                    PrintWarning(L"复制成功，但无法删除回收站副本，请手动清理: " + trashPath);
                    restoreSuccess = true;  // 内容已恢复，标记成功
                }
            } else {
                PrintError(L"xcopy 复制失败（退出码: " + std::to_wstring(exitCode) + L"），请手动恢复");
                return ExitCode::IoOrNetwork;
            }
        } else {
            PrintError(L"无法启动 xcopy，请手动将目录从 " + trashPath + L" 复制到 " + originalPath);
            return ExitCode::IoOrNetwork;
        }
    }

    // ----- 删除元数据文件（如果还存在）-----
    DeleteFileW(metaPath.c_str());

    // ----- 强制刷新缓存（重要！）-----
    if (restoreSuccess) {
        PrintInfo(L"正在刷新 JDK 缓存...");
        auto jdks = JdkScanService::scanJdks(true, ctx.paths.cacheFile, true);
        PrintSuccess(L"版本 " + version + L" 已还原至 " + originalPath);
        PrintWarning(L"请运行 'jmt search' 或 'jmt use' 重新配置环境变量（若需要）");
    } else {
        PrintError(L"还原失败，请检查权限或手动操作");
        return ExitCode::IoOrNetwork;
    }

    return ExitCode::Ok;
}
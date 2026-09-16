#include "command/data_command.hpp"
#include "jdk/jdk_scan_service.hpp"
#include "jdk/jdk_download_service.hpp"
#include "console/color_print.hpp"
#include "system/utils.hpp"
#include "app/elevation_gate.hpp"
#include <windows.h>
#include <conio.h>
#include <filesystem>

namespace fs = std::filesystem;

ExitCode DataCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    if (args.size() < 2) {
        PrintError(L"用法: data <子命令>");
        PrintInfo(L"  子命令: output    - 导出当前 JDK 列表到 .data\\ver_out.txt");
        PrintInfo(L"          input     - 从 .data\\ver_out.txt 导入并安装 JDK");
        return ExitCode::BadArgs;
    }

    const std::wstring& subCmd = args[1];

    // ---------- data output ----------
    if (subCmd == L"output") {
        PrintInfo(L"正在扫描合法 JDK...");

        auto jdks = JdkScanService::scanJdks(false, ctx.paths.cacheFile, true);
        if (jdks.empty()) {
            PrintWarning(L"未找到任何合法 JDK，无法导出");
            return ExitCode::NotFound;
        }

        // 创建 .data 目录
        std::wstring dataDir = ctx.paths.dataDir;
        CreateDirectoryW(dataDir.c_str(), nullptr);

        // 写入 ver_out.txt（格式：version|path\n，与 .jmt_cache 一致）
        std::wstring outputPath = JoinPath(dataDir, L"ver_out.txt");
        std::wstring content;
        for (const auto& [ver, path] : jdks) {
            content += ver + L"|" + path + L"\n";
        }

        if (WriteFileText(outputPath, content)) {
            PrintSuccess(L"导出成功，共 " + std::to_wstring(jdks.size()) + L" 个版本");
            PrintInfo(L"文件: " + outputPath);
        } else {
            PrintError(L"写入文件失败: " + outputPath);
            return ExitCode::IoOrNetwork;
        }

        return ExitCode::Ok;
    }

    // ---------- data input ----------
    if (subCmd == L"input") {
        // 提权
        if (!ctx.isElevated) {
            const ElevationDecision decision = ElevationGate::requestElevation(args, ctx);
            if (!decision.proceed) return decision.code;
        }

        // 1. 读取并解析 ver_out.txt
        std::wstring dataDir = ctx.paths.dataDir;
        std::wstring inputPath = JoinPath(dataDir, L"ver_out.txt");
        if (!IsFile(inputPath)) {
            PrintError(L"未找到导入文件: " + inputPath);
            PrintInfo(L"请先执行 'jmt data output' 导出列表");
            return ExitCode::NotFound;
        }

        std::wstring content = ReadFileText(inputPath);
        if (content.empty()) {
            PrintError(L"导入文件为空");
            return ExitCode::NotFound;
        }

        // 解析每一行：version|path
        std::vector<std::pair<std::wstring, std::wstring>> entries;
        size_t start = 0;
        while (start < content.size()) {
            size_t end = content.find(L'\n', start);
            if (end == std::wstring::npos) end = content.size();
            std::wstring line = content.substr(start, end - start);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            if (!line.empty()) {
                size_t sep = line.find(L'|');
                if (sep != std::wstring::npos) {
                    std::wstring ver = line.substr(0, sep);
                    std::wstring path = line.substr(sep + 1);
                    if (!ver.empty() && !path.empty()) {
                        entries.push_back({ver, path});
                    }
                }
            }
            start = end + 1;
        }

        if (entries.empty()) {
            PrintError(L"导入文件格式错误，未解析到有效条目");
            PrintInfo(L"格式要求：每行 版本号|安装路径");
            return ExitCode::BadArgs;
        }

        // 2. 逐个检查并处理
        int successCount = 0;
        int exeCount = 0;
        int failCount = 0;
        int skipCount = 0;

        PrintInfo(L"=====================================");
        PrintInfo(L"    开始检查并导入 JDK 列表");
        PrintInfo(L"=====================================");

        for (const auto& [ver, path] : entries) {
            PrintInfo(L"");
            PrintInfo(L"--- JDK " + ver + L" ---");
            PrintInfo(L"  目标路径: " + path);

            // 检查路径下是否已存在合法 JDK
            if (JdkScanService::isValidJdk(path)) {
                PrintSuccess(L"JDK " + ver + L" 已存在，无需安装");
                skipCount++;
                continue;
            }

            // 不存在合法 JDK，询问是否安装
            PrintWarning(L"该路径下未找到合法 JDK，需要下载并安装");
            PrintInfo(L"是否下载并安装到该路径？(y/n): ");
            int ch = _getwch();
            if (ch != L'y' && ch != L'Y') {
                PrintInfo(L"\n  已跳过");
                skipCount++;
                continue;
            }
            PrintInfo(L"");

            // 提取父目录作为安装根目录
            std::wstring installRoot = path;
            size_t pos = installRoot.find_last_of(L'\\');
            if (pos != std::wstring::npos) {
                installRoot = installRoot.substr(0, pos);
                CreateDirectoryW(installRoot.c_str(), nullptr);
            }

            PrintInfo(L"正在安装 JDK " + ver + L" ...");
            std::wstring result = JdkDownloadService::downloadAndInstall(ver, installRoot);

            if (result == L"EXE_DOWNLOADED") {
                PrintWarning(L"JDK " + ver + L" 安装程序已下载到 .temp 目录，请手动完成安装");
                exeCount++;
            } else if (!result.empty()) {
                PrintSuccess(L"JDK " + ver + L" 安装成功: " + result);
                successCount++;
            } else {
                PrintError(L"JDK " + ver + L" 安装失败");
                failCount++;
            }
        }

        // 3. 更新缓存
        if (successCount > 0) {
            JdkScanService::scanJdks(true, ctx.paths.cacheFile, true);
        }

        // 4. 汇总
        PrintInfo(L"");
        PrintInfo(L"=====================================");
        PrintInfo(L"导入完成汇总：");
        PrintInfo(std::wstring(L"  无需安装（已存在）: ") + std::to_wstring(skipCount) + L" 个");
        PrintInfo(std::wstring(L"  成功安装: ") + std::to_wstring(successCount) + L" 个");
        if (exeCount > 0) PrintInfo(L"  已下载 EXE: " + std::to_wstring(exeCount) + L" 个（需手动安装）");
        if (failCount > 0) PrintInfo(L"  失败: " + std::to_wstring(failCount) + L" 个");
        PrintInfo(L"=====================================");

        if (exeCount > 0) {
            std::wstring tempDir = ctx.paths.tempDir;
            PrintWarning(L"EXE 安装包已下载到 " + tempDir + L" 目录，请手动运行安装");
        }
        if (successCount > 0) {
            PrintInfo(L"请重启终端或运行 'jmt search' 刷新环境变量");
        }

        return failCount > 0 ? ExitCode::IoOrNetwork : ExitCode::Ok;
    }

    // 未知子命令
    PrintError(L"未知子命令: " + subCmd);
    PrintInfo(L"可用子命令: output, input");
    return ExitCode::BadArgs;
}

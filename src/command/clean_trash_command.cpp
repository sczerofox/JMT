#include "clean_trash_command.hpp"
#include "../print/color_print.hpp"
#include "../utils/utils.hpp"
#include "../infrastructure/elevation_helper.hpp"
#include <filesystem>
#include <conio.h>

namespace fs = std::filesystem;

int CleanTrashCommand::execute(const std::vector<std::wstring>& args, JmtContext& ctx) {
    bool force = false;
    for (const auto& a : args) {
        if (a == L"--force") force = true;
    }

    // 提权（因为要删除文件，通常需要管理员权限）
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

    std::wstring trashRoot = ctx.exeDirectory + L"\\.jmt_trash";
    if (!IsDirectory(trashRoot)) {
        PrintInfo(L"回收站不存在，无需清理");
        return 0;
    }

    if (!force) {
        PrintWarning(L"此操作将永久删除回收站中的所有 JDK 备份，不可恢复！");
        PrintInfo(L"请输入 'y' 确认，其他任意键取消：");
        int ch = _getwch();
        if (ch != L'y' && ch != L'Y') {
            PrintInfo(L"操作已取消");
            return 0;
        }
        PrintInfo(L""); // 换行
    }

    try {
        fs::remove_all(trashRoot);
        PrintSuccess(L"回收站已清空");
    } catch (const std::exception& e) {
        std::wstring errMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        PrintError(L"清空失败: " + errMsg);
        return 3;
    }
    return 0;
}
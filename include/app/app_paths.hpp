#pragma once

#include <string>

// 运行期路径集中定义：此前 .temp/.trash 等字面量散落在服务与命令中，
// 现在只有这里知道目录布局。
struct AppPaths {
    std::wstring exeDir;      // jmt.exe 所在目录（无尾随反斜杠）
    std::wstring exePath;     // exeDir\jmt.exe
    std::wstring cacheFile;   // exeDir\.jmt_cache
    std::wstring tempDir;     // exeDir\.temp
    std::wstring trashDir;    // exeDir\.trash

    // 以指定目录为根计算全部子路径（测试可指向临时目录）
    static AppPaths rootedAt(const std::wstring& root);

    // 生产环境：根取当前 exe 所在目录
    static AppPaths fromExecutable();
};

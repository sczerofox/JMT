#pragma once

// 进程退出码：数值被 README、开发文档与 tests/integration 依赖，改动需三处同步
enum class ExitCode : int {
    Ok = 0,                 // 成功
    BadArgs = 1,            // 参数错误 / 未知命令
    NotFound = 2,           // 未找到 JDK / 版本 / 条目
    PermissionDenied = 3,   // 权限不足（提权失败或注册表写入失败）
    IoOrNetwork = 4         // 网络或磁盘错误
};

[[nodiscard]] constexpr int toInt(ExitCode code) noexcept {
    return static_cast<int>(code);
}

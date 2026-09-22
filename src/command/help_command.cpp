#include "command/help_command.hpp"
#include "platform/output.hpp"

HelpCommand::HelpCommand(const CommandRegistry& registry) : registry_(registry) {}

// 显示子命令详情（针对 jmt help <command>）
static void PrintSubcommandDetail(IOutput& out, const std::wstring& cmd) {
    if (cmd == L"remove") {
        out.line(OutputLevel::Info, L"");
        out.line(OutputLevel::Info, L"  子命令：");
        out.line(OutputLevel::Info, L"    env               从 PATH 中移除 JMT 自身目录");
        out.line(OutputLevel::Info, L"    all               完全清理所有 JMT 配置和缓存");
        out.line(OutputLevel::Info, L"    temp              删除 .temp 下载缓存");
        out.line(OutputLevel::Info, L"    trash             永久清空回收站");
        out.line(OutputLevel::Info, L"    <版本号>          删除指定版本 JDK（移至回收站）");
    } else if (cmd == L"download") {
        out.line(OutputLevel::Info, L"");
        out.line(OutputLevel::Info, L"  说明：能下载到 ZIP 时自动解压安装；只有安装包（EXE/MSI）时下载到 .temp 并提示手动安装");
        out.line(OutputLevel::Info, L"        源按优先级自动回退：南京大学 → 清华 TUNA → 华为云 → Adoptium 官方，无需选择");
        out.line(OutputLevel::Info, L"        版本覆盖面：8 / 11 / 16+ 有 ZIP；12~26 另有华为云 GA 包；8 还可下 EXE 手动安装");
    } else if (cmd == L"rollback") {
        out.line(OutputLevel::Info, L"");
        out.line(OutputLevel::Info, L"  参数：");
        out.line(OutputLevel::Info, L"    list              查看回收站中可恢复的版本");
        out.line(OutputLevel::Info, L"    <版本号>          恢复指定版本 JDK 到原始路径");
        out.line(OutputLevel::Info, L"  说明：不带参数执行 'jmt rollback' 等价于 'jmt rollback list'");
    } else if (cmd == L"search") {
        out.line(OutputLevel::Info, L"");
        out.line(OutputLevel::Info, L"  可选参数：");
        out.line(OutputLevel::Info, L"    --force           强制重新扫描，忽略缓存");
    }
}

ExitCode HelpCommand::execute(const std::vector<std::wstring>& args, AppContext& ctx) {
    if (args.size() > 1) {
        auto* cmd = registry_.findCommand(args[1]);
        if (cmd) {
            ctx.out->line(OutputLevel::Info, cmd->getHelp());
            PrintSubcommandDetail(*ctx.out, args[1]);
        } else {
            ctx.out->line(OutputLevel::Error, L"未知命令: " + args[1]);
        }
    } else {
        ctx.out->line(OutputLevel::Info, L"用法: jmt <命令> [参数...]");
        ctx.out->line(OutputLevel::Info, L"");
        ctx.out->line(OutputLevel::Info, L"JDK 版本管理：");
        ctx.out->line(OutputLevel::Info, L"  search             扫描并自动设置 JDK（--force）");
        ctx.out->line(OutputLevel::Info, L"  list               列出已安装版本");
        ctx.out->line(OutputLevel::Info, L"  use <版本号>       切换当前版本（--exact）");
        ctx.out->line(OutputLevel::Info, L"  download <版本号>  下载并自动配置 JDK（ZIP 自动装 / 安装包手动装）");
        ctx.out->line(OutputLevel::Info, L"  remove             删除 JDK（env/all/temp/trash/<版本号>）");
        ctx.out->line(OutputLevel::Info, L"  rollback           从回收站恢复 JDK（list/<版本号>）");
        ctx.out->line(OutputLevel::Info, L"");
        ctx.out->line(OutputLevel::Info, L"环境与其他：");
        ctx.out->line(OutputLevel::Info, L"  env                注册 JMT 目录到系统 PATH");
        ctx.out->line(OutputLevel::Info, L"  version            显示 JMT 版本信息");
        ctx.out->line(OutputLevel::Info, L"  help [命令]        显示此帮助或命令详情");
    }
    return ExitCode::Ok;
}

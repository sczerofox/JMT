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
    } else if (cmd == L"data") {
        out.line(OutputLevel::Info, L"");
        out.line(OutputLevel::Info, L"  子命令：");
        out.line(OutputLevel::Info, L"    output            导出当前 JDK 列表到 .data\\ver_out.txt");
        out.line(OutputLevel::Info, L"    input             从 .data\\ver_out.txt 导入并安装 JDK");
    } else if (cmd == L"download") {
        out.line(OutputLevel::Info, L"");
        out.line(OutputLevel::Info, L"  可选参数：");
        out.line(OutputLevel::Info, L"    --mirror          使用镜像加速下载");
        out.line(OutputLevel::Info, L"    exe               强制下载 EXE 安装程序到 .temp");
        out.line(OutputLevel::Info, L"    java              使用官方源下载（较慢）");
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
        ctx.out->line(OutputLevel::Info, L"  download <版本号>  从镜像/官方源下载并自动配置（--mirror/exe/java）");
        ctx.out->line(OutputLevel::Info, L"  remove             删除 JDK（env/all/temp/trash/<版本号>）");
        ctx.out->line(OutputLevel::Info, L"  rollback           从回收站恢复 JDK（list/<版本号>）");
        ctx.out->line(OutputLevel::Info, L"");
        ctx.out->line(OutputLevel::Info, L"环境配置：");
        ctx.out->line(OutputLevel::Info, L"  env                注册 JMT 目录到系统 PATH");
        ctx.out->line(OutputLevel::Info, L"  shell              打开已配置 JMT 环境的新终端");
        ctx.out->line(OutputLevel::Info, L"");
        ctx.out->line(OutputLevel::Info, L"数据管理：");
        ctx.out->line(OutputLevel::Info, L"  data               导出/导入 JDK 列表（output/input）");
        ctx.out->line(OutputLevel::Info, L"");
        ctx.out->line(OutputLevel::Info, L"其他：");
        ctx.out->line(OutputLevel::Info, L"  version            显示 JMT 版本信息");
        ctx.out->line(OutputLevel::Info, L"  help [命令]        显示此帮助或命令详情");
    }
    return ExitCode::Ok;
}

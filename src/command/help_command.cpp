#include "help_command.hpp"
#include "../print/color_print.hpp"

HelpCommand::HelpCommand(const CommandRegistry& registry) : registry_(registry) {}

// 显示子命令详情（针对 jmt help <command>）
static void PrintSubcommandDetail(const std::wstring& cmd) {
    if (cmd == L"remove") {
        PrintInfo(L"");
        PrintInfo(L"  子命令：");
        PrintInfo(L"    env               从 PATH 中移除 JMT 自身目录");
        PrintInfo(L"    all               完全清理所有 JMT 配置和缓存");
        PrintInfo(L"    temp              删除 .temp 下载缓存");
        PrintInfo(L"    trash             永久清空回收站");
        PrintInfo(L"    <版本号>          删除指定版本 JDK（移至回收站）");
    } else if (cmd == L"data") {
        PrintInfo(L"");
        PrintInfo(L"  子命令：");
        PrintInfo(L"    output            导出当前 JDK 列表到 .data\\ver_out.txt");
        PrintInfo(L"    input             从 .data\\ver_out.txt 导入并安装 JDK");
    } else if (cmd == L"download") {
        PrintInfo(L"");
        PrintInfo(L"  可选参数：");
        PrintInfo(L"    --mirror          使用镜像加速下载");
        PrintInfo(L"    exe               强制下载 EXE 安装程序到 .temp");
        PrintInfo(L"    java              使用官方源下载（较慢）");
    } else if (cmd == L"rollback") {
        PrintInfo(L"");
        PrintInfo(L"  参数：");
        PrintInfo(L"    list              查看回收站中可恢复的版本");
        PrintInfo(L"    <版本号>          恢复指定版本 JDK 到原始路径");
    } else if (cmd == L"search") {
        PrintInfo(L"");
        PrintInfo(L"  可选参数：");
        PrintInfo(L"    --force           强制重新扫描，忽略缓存");
    }
}

int HelpCommand::execute(const std::vector<std::wstring>& args, JmtContext& ctx) {
    if (args.size() > 1) {
        auto* cmd = registry_.findCommand(args[1]);
        if (cmd) {
            PrintInfo(cmd->getHelp());
            PrintSubcommandDetail(args[1]);
        } else {
            PrintError(L"未知命令: " + args[1]);
        }
    } else {
        PrintInfo(L"用法: jmt <命令> [参数...]");
        PrintInfo(L"");
        PrintInfo(L"JDK 版本管理：");
        PrintInfo(L"  search             扫描并自动设置 JDK");
        PrintInfo(L"  list               列出已安装版本");
        PrintInfo(L"  use <版本号>       切换当前版本");
        PrintInfo(L"  download <版本号>  从镜像/官方源下载 JDK 并自动配置（exe 仅下载包）");
        PrintInfo(L"  remove             删除 JDK（env/all/temp/trash/<版本号>）");
        PrintInfo(L"  rollback <版本号>  从回收站恢复 JDK");
        PrintInfo(L"");
        PrintInfo(L"环境配置：");
        PrintInfo(L"  env                注册 JMT 目录到系统 PATH");
        PrintInfo(L"  shell              打开已配置 JMT 环境的新终端");
        PrintInfo(L"");
        PrintInfo(L"数据管理：");
        PrintInfo(L"  data               导出/导入 JDK 列表（output/input）");
        PrintInfo(L"");
        PrintInfo(L"其他：");
        PrintInfo(L"  version            显示 JMT 版本信息");
        PrintInfo(L"  help [命令]        显示此帮助或命令详情");
    }
    return 0;
}
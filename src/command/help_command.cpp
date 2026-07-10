#include "help_command.hpp"
#include "../print/color_print.hpp"

HelpCommand::HelpCommand(const CommandRegistry& registry) : registry_(registry) {}

int HelpCommand::execute(const std::vector<std::wstring>& args, JmtContext& ctx) {
    if (args.size() > 1) {
        auto* cmd = registry_.findCommand(args[1]);
        if (cmd) {
            PrintInfo(cmd->getHelp());
        } else {
            PrintError(L"未知命令: " + args[1]);
        }
    } else {
        PrintInfo(L"可用命令：");
        for (const auto& pair : registry_.getAll()) {
            PrintInfo(L"  " + pair.first + L" - " + pair.second->getHelp());
        }
    }
    return 0;
}
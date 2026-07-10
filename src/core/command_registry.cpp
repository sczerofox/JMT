#include "command_registry.hpp"

void CommandRegistry::registerCommand(const std::wstring& name, std::unique_ptr<CommandBase> cmd) {
    commands_[name] = std::move(cmd);
}

CommandBase* CommandRegistry::findCommand(const std::wstring& name) const {
    auto it = commands_.find(name);
    if (it != commands_.end())
        return it->second.get();
    return nullptr;
}

const std::map<std::wstring, std::unique_ptr<CommandBase>>& CommandRegistry::getAll() const {
    return commands_;
}
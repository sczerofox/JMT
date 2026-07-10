#pragma once
#include <memory>
#include <map>
#include <string>
#include "command_base.hpp"

class CommandRegistry {
public:
    void registerCommand(const std::wstring& name, std::unique_ptr<CommandBase> cmd);
    [[nodiscard]] CommandBase* findCommand(const std::wstring& name) const;
    [[nodiscard]] const std::map<std::wstring, std::unique_ptr<CommandBase>>& getAll() const;
private:
    std::map<std::wstring, std::unique_ptr<CommandBase>> commands_;
};
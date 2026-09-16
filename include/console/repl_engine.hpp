#pragma once
#include "command/command_registry.hpp"
#include "app/app_context.hpp"
#include <string>

class ReplEngine {
public:
    ReplEngine(CommandRegistry& registry, AppContext& ctx);
    void run();
private:
    CommandRegistry& registry_;
    AppContext& ctx_;
    static void printBanner();
};
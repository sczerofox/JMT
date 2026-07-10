#pragma once
#include "../core/command_registry.hpp"
#include "../core/jmt_context.hpp"
#include <string>

class ReplEngine {
public:
    ReplEngine(CommandRegistry& registry, JmtContext& ctx);
    void run();
private:
    CommandRegistry& registry_;
    JmtContext& ctx_;
    static void printBanner();
    bool handleCtrlC();
};
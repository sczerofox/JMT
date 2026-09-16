#include "app/env_scope.hpp"

EnvScope EnvScope::parse(const std::vector<std::wstring>& raw) {
    EnvScope scope;
    for (const auto& arg : raw) {
        if (arg == L"--user") {
            scope.target = EnvTarget::UserOnly;
            scope.userRequested = true;
        } else if (arg == L"--sys") {
            scope.target = EnvTarget::SystemOnly;
        } else {
            scope.args.push_back(arg);
        }
    }
    return scope;
}

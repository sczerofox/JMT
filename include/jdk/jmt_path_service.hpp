#pragma once

#include <string>

#include "platform/output.hpp"
#include "platform/registry.hpp"

class JmtPathService {
public:
    JmtPathService(IRegistry& registry, IOutput& out) : registry_(registry), out_(out) {}

    // 注册 JMT 目录到 PATH（先删除旧条目，再添加）
    bool registerJmtPath(const std::wstring& exeDir, EnvTarget target = EnvTarget::Auto);

private:
    IRegistry& registry_;
    IOutput& out_;
};

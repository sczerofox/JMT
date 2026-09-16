#pragma once
#include <string>
#include "system/registry_operator.hpp"

class JmtPathService {
public:
    // 注册 JMT 目录到 PATH（先删除旧条目，再添加）
    static bool registerJmtPath(const std::wstring& exeDir, EnvTarget target = EnvTarget::Auto);
};
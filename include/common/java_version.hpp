#pragma once

#include <string>

// JDK 版本模型：不再把版本截断成主版本。
//   raw 保持原样（17.0.9 / 22 / 1.8.0_202），匹配与比较用规范化后的字段。
// 规范映射：
//   17.0.9    -> feature=17 interim=0  update=9   patch=0
//   22        -> feature=22
//   1.8.0_202 -> feature=8  interim=0  update=202 patch=0   （1.x 旧格式）
//   8u202     -> feature=8             update=202
struct JavaVersion {
    std::wstring raw;        // 原样显示用
    int feature = 0;         // 17 / 22 / 8
    int interim = 0;         // 0
    int update = 0;          // 9 / 202
    int patch = 0;
    std::wstring build;      // 可选：来自 IMPLEMENTOR_VERSION（如 "9"）

    // 解析失败（不是版本号）时 valid() == false
    static JavaVersion parse(const std::wstring& raw);

    [[nodiscard]] bool valid() const { return feature > 0; }
    [[nodiscard]] std::wstring display() const { return raw; }

    // 排序用：feature → interim → update → patch
    [[nodiscard]] int compare(const JavaVersion& other) const;

    // 查询串匹配：精确（17.0.9）、主版本（17）、前缀（17.0）、旧别名（8 / 8u202 / 1.8）
    [[nodiscard]] bool matches(const std::wstring& query) const;

    // 查询串是否指定了完整版本（含点或 u），用于决定下载源是否需要精确筛选
    static bool isFullVersionQuery(const std::wstring& query);
};

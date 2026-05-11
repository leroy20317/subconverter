---
doc_type: issue-fix
issue: 2026-05-11-tag-template-groups-not-expanded
path: fast-track
fix_date: 2026-05-11
tags: [tag-template, proxy-groups, loon, sing-box]
---

# %TAG% 分组模板未展开 修复记录

## 1. 问题描述

Loon 目标导出时会保留原始 `%TAG% = select,DIRECT`，而不是展开为实际 tag 分组名。进一步排查后发现，sing-box 的分组导出路径也存在相同的模板未展开风险。

## 2. 根因

`src/generator/config/subexport.cpp` 中：

- `proxyToLoon()` 的分组生成直接遍历 `extra_proxy_group`
- `proxyToSingBox()` 的分组生成也直接遍历 `extra_proxy_group`

这两条路径都跳过了 `buildProxyGroups(extra_proxy_group, nodelist)`，因此 `%TAG%`、`!!TAG`、`!!IN` 等模板规则没有像 Clash、Surge、Quantumult、Quantumult X、Mellow 那样先展开成实际分组配置。

## 3. 修复方案

在 Loon 和 sing-box 的分组输出前，先构造 `effective_proxy_group = buildProxyGroups(extra_proxy_group, nodelist)`，然后基于展开后的分组继续生成目标配置，保持与其他支持 proxy group 的 target 一致。

## 4. 改动文件清单

- `src/generator/config/subexport.cpp`
  - `proxyToLoon()`：改为遍历 `effective_proxy_group`
  - `proxyToSingBox()`：改为遍历 `effective_proxy_group`

## 5. 验证结果

- 对照 `subexport.cpp` 中各 target 的实现确认：Clash / ClashR / Surge / Quantumult / Quantumult X / Mellow / Loon / sing-box 现在都统一经过 `buildProxyGroups(...)`
- 已检查 `src/generator/config/subexport.cpp` 的 lint 诊断，未发现新增问题
- 尚未补自动化回归用例；最终导出结果建议继续用带 `%TAG%` 的配置做一次 Loon / sing-box 实测确认

## 6. 遗留事项

- 建议后续补一个最小回归样例，覆盖 `%TAG%` 在多 target 的分组展开行为

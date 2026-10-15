# macOS 15.6.1 研究入口

2026-10-15 合并更新：详细分析统一到 [15.6.1 分享包复盘](sequoia-24G90-share-review-20261015.md)，项目整体时间线统一到 [PROJECT-ARCHAEOLOGY](PROJECT-ARCHAEOLOGY.md)。本路径保留，避免旧交接和工具引用断裂。

原总结已[原文存档](../Archive/20261015-pre-consolidation/ARCHAEOLOGY-15.6.1.md)。其中“131 个真实 cache 镜像”“补丁才是 bug”“只有改内核才能继续”的叙事不能作为当前结论；具体撤回与证据见新复盘。

[10-04 冻结交接](archive/macos15-roots/HANDOVER-MACOS15-FROZEN-2026-10-04.md)、[详细 state](archive/macos15-roots/dyld-15.6.1-state.md)和原始证据保持原位。15.6.1 尚未在本机完成真实 cache CLI 验收；先完成当前 Ventura，再按新复盘中的隔离验证顺序恢复研究。

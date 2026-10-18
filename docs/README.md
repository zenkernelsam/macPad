# macPad 文档导航

更新：2026-10-18（新增10-16更新包、Safari／墙纸交接与10-17现场备份对照）。先区分原作者的验证记录、我们当前的Ventura复现和已冻结的Sequoia研究；它们不是同一设备的同一验收状态。

## 今晚继续 macOS 13

1. [AGENTS.md](../AGENTS.md)：操作与证据纪律，保持原位。
2. [当前交接状态](../coordination/REVIEWER_STATE.md)与[最新relay SYNC](../coordination/_relay/SYNC.md)：人工已于10-18恢复Ventura；[10-15暂停记录](../coordination/PAUSE_REQUEST.md)保留为历史。设备操作与当前worker进度以最新交接为准。
3. [工作清单与可复制提示](porting/ventura-22F82-evening-handoff-20261015.md)：已更新provider离线修复状态，新增真实reopen、服务消息唤醒、签名失效及同帧像素验收；R008/TCC按最新owner证据接续。
4. [22F82 复现手册](porting/ventura-22F82-session-reproduction-20261013.md)：已有部署／诊断工具；结合最新 R006–R008 使用。
5. [工具入口](porting/TOOLS-AND-PORTING.md)：当前可用工具与已撤回的旧建议。

## 以后恢复 macOS 15.6.1

- [本次分享包复盘](porting/sequoia-24G90-share-review-20261015.md)：过去的错误推断、成功案例的具体差异、证据限度和恢复顺序。
- [项目时间线](porting/PROJECT-ARCHAEOLOGY.md)：一份当前叙事，不再重复多份互相矛盾的总结。
- [原冻结交接](porting/archive/macos15-roots/HANDOVER-MACOS15-FROZEN-2026-10-04.md)及[详细研究日志](porting/archive/macos15-roots/dyld-15.6.1-state.md)：保持原文，按具体配置和后续纠正阅读。
- [本次文件核验](evidence/sequoia-share-audit-20261015/audit.json)：归档／关键文件哈希、检查范围与未验证项。
- [10-18增补证据](evidence/sequoia-share-update-20261018/README.md)：更新包完整校验、新旧清单、现场`.partial`完整性与运行脚本差异；第一版结论的新增内容已合并进原复盘§7。

“15.6.1 整体不可行”“原版 131 镜像已成功”“安装日志即真实 hook”等旧断言已在新复盘中纠正，不能从历史文档抽取后直接执行。

## 原作者与分享者材料：保持原位

- 本仓库 [README](../README.md)、[AGENTS](../AGENTS.md)，以及现有作者 evidence。
- [作者 MacWSBootingGuide 本地副本](../analysis/MacWSBootingGuide/README.md)，供交叉对照；没有更新或修改其 checkout。
- 群友原包 `analysis/macPad-15.6 2.1-分享包-20261013/`，完整内容和校验清单保持不变。提取副本在独立 `analysis/share-review-20261015/`，没有作为安装包部署。
- iCloud的两个新输入目录仅只读：更新发布包与`15.6提取包`均未写入；本轮选择性提取和IDA数据库置于`/tmp/macpad-share-review-20261018/`，没有完整解包或恢复个人状态。
- [coordination](../coordination/README.md)、历史 handover-chain 和 `_RELAY SYNC` 类交接材料：不移动、不改写；已有未提交文件属于原工程。

## 归档与整理记录

[Archive](Archive/README.md) 保存三份已被合并替代的旧总结，并导航已有历史归档。原始证据、原作者资料和交接工程文件没有搬动。新旧入口与哈希见 [整理清单](Archive/manifest-20261015.json)。

全量目录／哈希／主题检索覆盖见 [文档索引](evidence/sequoia-share-audit-20261015/document-index.json)。索引覆盖不代表每一条历史结论已被独立复验。

本次阅读已覆盖初始289份 Markdown、73份补充说明／摘录及三份归档旧汇总；原始采样、反汇编、结构化清单与环境附带文件另行分类，见[覆盖范围](evidence/sequoia-share-audit-20261015/corpus-coverage.json)。[完成审计](evidence/sequoia-share-audit-20261015/completion-audit.json)逐项记录交付、保存检查和仍待未来运行验证的边界。

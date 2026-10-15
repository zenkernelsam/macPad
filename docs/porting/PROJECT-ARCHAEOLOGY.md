# macPad 项目时间线与当前结论

更新：2026-10-15。本页合并项目历程入口；详细 Sequoia 复盘见 [分享包研究](sequoia-24G90-share-review-20261015.md)，当前设备动作以 [coordination](../../coordination/REVIEWER_STATE.md) 为准。原 10-13 版本已[存档](../Archive/20261015-pre-consolidation/PROJECT-ARCHAEOLOGY.md)。

## 三条资料线

| 资料线 | 对象 | 不可混用的边界 |
| --- | --- | --- |
| 原作者 | 主要为 Ventura 13.4／22F66，作者验证设备 | 作者的 Host／输入／应用成功不等于本机已通过 |
| 我们的复现 | iPad13,11，M1/T8103，iPadOS 16.3／20D47；当前 Ventura 13.4.1／22F82 | 当前 input 尚未验收，R008 candidate 未完成部署／review 闭环 |
| 群友分享 | 来源方报告 iPad14,4，M2，iPadOS 16.2／20C65，15.6.1／24G90 | 交付文件已本地核验；脱敏全新安装及本机 M1 适配未验收 |

## 2026-09-26 至 10-04：Sequoia 研究，随后冻结

目标是 macOS 15.6.1 真正的 dyld／cache／libSystem 执行原版 echo。先遇到 exec/trust、iOS cache 复用、syscall 536／slide-info、地址窗口、private pager format 13，最后记录了 T8103 PMAP-CS owner 问题。

- 09-28 的 `echo HELLO` 使用 disk libSystem shim，是探路结果，不是原定真实 cache 里程碑。
- “131 镜像已加载、原版 cache 成功、卡 ElleKit initializer”已被 [10-01 最终回复 §11](archive/macos15-roots/HANDOVER-REPLY-2026-10-01-FINAL.md) 撤回：那些镜像来自 iOS 辅助进程。旧汇总错误地复活了这一结果；不能继续引用。
- 10-03 详细 [state](archive/macos15-roots/dyld-15.6.1-state.md) 记录恢复基线 `b8fdbc1b…381b91e` 已有 `emptysr/highreserve` scaffold；“original dyld”在后续摘要中只是当时的恢复基线，不能解释成 Apple pristine。
- [10-04 冻结交接](archive/macos15-roots/HANDOVER-MACOS15-FROZEN-2026-10-04.md) 的具体 fault／内核写失败仍保留；没有真实 cache 的 `HI` 验收。
- **修正总判断：**失败限定于试过的设备与组合。“只能改签名内核才有出路”并未穷尽其他加载／准入／pager 契约，不能作为整个 15.6.1 路线不可行的证明。

## 10-04 至 10-07：转向 Ventura，CLI 成功

选择 13.4.1／22F82，使用该版本真实 dyld 和 cache，补齐签名、trust、os_variant interposition 及注入闭包。10-06 经 launcher 输出 `HI`，10-07 推进 literal chroot 与 CLI sweep。

[bringup-state](ventura-22F82-bringup-state.md) 的后续纠正明确：当前 Dopamine 动态 trustcache 在冷启动后需要恢复。旧“重启后持久”的说法不能继续引用。22F66 和 22F82 的 cache 身份也不可混为同一对；参见 `1656f07` 的更正。

## 10-08 至 10-13：桌面链与连续合成

[10-11 桌面证据](../evidence/ventura-22F82-desktop-chain-20261011.md)记录 XPC 路由、LaunchServices ASN 与依赖恢复；[10-13 证据](../evidence/ventura-22F82-input-transport-and-composite-20261013.md)记录 final-composite sequence、壁纸和输入故障。

连续合成与可见内容是显示证据。早期“某 hook 没调用”的结论须结合后来 provider 的实际修复重新审查；进程运行时间和 INSTALL 日志都不等于 hook 生效。

## 10-14 至 10-15：输入故障推进到真实 TCC 回复

- R002 修复原 no-op Substrate provider，得到实际 hook 执行见证。
- R003/R004 纠正 `SLSGetEventPort`、MIG 1000、audit pointer 和 probe 私有 globals 等误判。
- [R005](../evidence/r005-live-tcc-branch-20261015.md)：实际 WS fresh-miss 的 TCC callsite 返回 1，随后 1002；不证明 daemon 明确拒绝。
- [R006](../evidence/r006-tcc-producer-20261015.md)：原始 trace 确认 system-tccd transport 无效，1 是默认结果。
- [R007](../evidence/r007-system-tccd-20261015.md)：真实 Ventura system-tccd 的 dictionary reply 到达；bool accessor 为 0，但尚未区分缺字段／类型错误／显式 false。
- [R008](../evidence/r008-tccd-transport-repro-20261015.md)：候选生命周期／准入／探针进入源码和 fixture；reviewer 另外保存六项边界问题，设备验收未完成。
- `6d1b7f6`：用户要求停机并暂停。本文和资料整理不改变该状态。

## 10-15：分享包带来的新研究对象

本地核验分享包全部校验项，提取关键文件并与 inventory 比对。确认材料包含 native15 loader 选择、private-cache exec/fork 配套、绑定 cache 哈希的 pager sidecars，以及 G14G 版本转换。其 WindowServer 配置同样有 `-virtualonly`；其相关 TCC entitlement 列表也不是简单多了 PostEvent grant。

这些是具体的下一轮对照对象；helper 内部、pager 机制与 M1 适用性仍待实际 RE／运行证据。完整说明与恢复阶梯见 [15.6.1 复盘](sequoia-24G90-share-review-20261015.md)，今晚可用建议见 [Ventura 清单](ventura-22F82-evening-handoff-20261015.md)。

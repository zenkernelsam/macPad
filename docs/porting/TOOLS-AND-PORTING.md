# macPad 当前工具入口与历史误区

更新：2026-10-15。原工具长文已[原文存档](../Archive/20261015-pre-consolidation/TOOLS-AND-PORTING.md)，有些“已证明不可行／必须如此”的规则属于特定历史配置，不能直接复用。

当前目标与状态见 [文档导航](../README.md)和 [coordination](../../coordination/REVIEWER_STATE.md)。本文不解除人工暂停，不构成设备操作指令。

## 按当前问题选工具

| 工作 | 现有入口 | 验证边界 |
| --- | --- | --- |
| 22F82 启动／恢复 | `layout/usr/macOS/bin/macos_gui.sh`，其[复现手册](ventura-22F82-session-reproduction-20261013.md) | 先按当前交接核对版本和状态；旧手册没有 R008 的完整设备验收 |
| 源码／部署一致性 | `misc/device_pipeline.sh`、`misc/build_on_ios.sh`、artifact-contract 工具 | 使用实际 hash、平台、薄切片、fresh inode 和 trust，不仅看 make 成功 |
| chroot／进程启动 | `launchdchrootexec/main.m`、`libmachook/exec_hooks.c` | 主镜像与依赖 UUID 属于目标进程；不要拼接 iOS launcher 的日志 |
| TCC 最小请求 | `misc/macos_ws_event_probe.c` 的 `postmin`、`macwstccdprobe` | 前者用于 WS 实际请求；后者自身 preflight 的 audit 身份不等于 WS 原 sender |
| TCC 生命周期回归 | `misc/test_tccd_lifecycle_contract.py` 与 `coordination/artifacts/R008-review-edge-repro.py/json` | 既有绿测试和追加边界失败同时看，不能只报前者 |
| runtime 开关 | `misc/audit_runtime_switches.py` 和现有 inventory | 区分诊断和生产；本次资料审计没有新增 runtime 开关 |
| 输入／显示终点 | AppInput、WS dispatch、final-composite、Host sequence 等现有探针 | 精确 owner、同一事件序列、真实界面变化；`posted=YES` 单独不足 |
| 信任恢复 | `macws_boot_trust.py`、项目 postinst pipeline | 必需文件／hash 缺失、查询失败都不能算成功；恢复实际当前 build |
| 二进制行为 RE | IDA Pro MCP，按 AGENTS.md 检查实际输入文件 | 实例端口不代表固定 binary；offset 必须绑定 hash/UUID |
| Sequoia 对照 | `analysis/share-review-20261015/` 和[复盘](sequoia-24G90-share-review-20261015.md) | 离线材料，不可直接覆盖 22F82 rootfs／host |

## 从旧长文撤回的通用建议

- **private mmap 一概是死路：撤回。** 旧实验是限定失败，本次出现不同的 exec/pager 组合，需核实其实际机制；不能据此承诺本机适用。
- **静态 arm64e sprobe 是必定可执行的标准起点：撤回。** 历史已有其 exec admission 失败记录；必须先证明探针本身真的执行到了被测入口。
- **536 返回成功即可证明 cache 可用：撤回。** 映射、重定位、PAC、代码签名、首次执行和目标程序输出是不同检查点。
- **按日志前后顺序区分 parent/child 就够：撤回。** 用 PID/generation、主镜像和 UUID 归属，不用“banner 之后都是 child”的假定。
- **任意 SIGKILL=签名失败／任意非零=libmachook 错：撤回。** 必须读取实际 crash／exec reason 和发生层。
- **`rm` 后复制作为日常首选：改为保留回滚副本、stage 新 inode、签名验证后原子切换。** 不永久删除仅存证据。
- **使用原作者的固定 cache CDHash 对适配所有 Ventura：撤回。** 22F82 有自身身份；参见 `1656f07` 与本次复盘的错误版本 trust 见证。
- **旧 lldb/tmux 脚本可以原样执行：不作此保证。** 先检查凭据、已有会话和清理范围；按当前交接使用有归属的有限时调试会话。

## 验证当前文档变更

本轮仅修改文档和本地审计材料；无需重建 arm64／arm64e 或启动设备。核对新链接、Archive 原文哈希、保护路径未改动、无 Apple payload／凭据进入提交，以及 `git diff --check`。

# macPad → macOS 13 Ventura 接棒手册（2026-10-04）

这份文档交给下一位 Agent，目标是从已冻结并清理的 macOS 15.6.1 研究，切回 macPad 作者已经验证过的 Ventura 13 路线。它记录当前仓库、设备、可用资产、上游同步结果、版本边界、第一阶段验收和不能重复踩的坑。

## 0. 接棒目标

在当前越狱 iPad13,11 / M1 上恢复作者的 macOS Ventura rootfs 方案，优先得到真实可见/protocol 输出，再启动 GUI。第一阶段至少要有：

1. 设备 rootfs 的 `ProductVersion`、`ProductBuildVersion`、架构和缓存身份经过 inventory；
2. trust/postinst 完成且有逐字日志；
3. bounded 原版 `/bin/echo HI` 经真实目标 dyld/系统库路径运行并打印 `HI`；
4. 再逐步验收 `cat`、`sh`、WindowServer/Host、输入和 GUI。

进程存活、脚本返回 0、安装包构建成功或磁盘 shim 打印 `HI` 都不能单独算真实 rootfs 验收。

## 1. 当前仓库和上游状态

本仓库：`/Users/ciscohe/Desktop/macPad`。当前 `main` 已同步到自己的 fork `origin/main`，最近 merge commit：

```text
1db897f Merge upstream windowing and input stabilization
```

作者 upstream：`https://github.com/DCMMC/macPad.git`，本轮已 fetch 并合并 `upstream/main` 的最新提交：

```text
eada48e fix: stabilize windowing and input compatibility
```

它的父提交 `8c59f89` 已在我们历史中，因此本次新增只有 `eada48e`。变更集中在 macPad 的 UIKit/Stage Manager windowing、Floating Dock authoritative center、IME/keyboard/AppInput、MacWSHost diagnostics、rootless `@rpath/CydiaSubstrate.framework` load command、协议测试和部署脚本。它没有修改 15.6 dyld、kernel/PAC/PTE、shared-cache、rootfs 构建或 trust 修复。

本次合并已推送。合并前 tag：`pre-upstream-windowing-20261004`，用于比较/回滚参考。不要 force-push。

合并后的 focused tests：

```text
PYTHONPATH=misc python3 -m unittest \
  misc.test_artifact_contract \
  misc.test_hardware_keyboard_contract \
  misc.test_keyboard_snapshot_wire \
  misc.test_terminal_tab_dock_contract \
  misc.test_text_input_bridge \
  misc.test_windowing_startup_readiness

Ran 58 tests ... OK
```

输出中有一行 `ERROR: fixture diagnostic present`，这是测试夹具诊断打印，不是 unittest failure；最终结果为 `OK`。合并后尚未在设备上部署或重启 GUI。

工作区仍有大量未跟踪的 15.6 RE 源码、二进制、IDA MCP 文件（`.ida-mcp/`、`.qoder/`、`misc/*` probe、根目录 `scheck` 等）。它们按 15.6 handover 要求保留，不要在新分支开始时顺手删除、提交或移动。

## 2. 设备当前状态

目标设备是：

```text
hw.machine       iPad13,11
SoC              M1 / T8103
kern.osversion   20D47
XNU              xnu-8792.82.2~1 / RELEASE_ARM64_T8103
Jailbreak        Dopamine rootless（历史记录为 3.0.2）
```

15.6 rootfs 已经删除，设备当前没有 `/var/mnt/rootfs`。删除前用 inode、`SystemVersion.plist` 和 dyld SHA 三重确认：

```text
ProductVersion       15.6.1
ProductBuildVersion  24G90
rootfs inode         244472649
dyld SHA256           b8fdbc1b7cfd15cccbcd110c0c3cb1ff91d135d6664b84770d42df843381b91e
```

删除时无 chroot、WindowServer、runner 或实验控制进程。没有写 kernel/PAC/PTE，也没有运行 `fmt13_patch.py`。公共 `/var/jb/usr/macOS` runtime、正式 MacWS 服务、未知 `/var/mobile/dscq` 和当前 staging 没有被删除。

设备侧后续必须重新做只读 inventory；不要假定 rootfs 挂载点、缓存目录、trustcache 或旧 PID 仍存在。认证凭据不要写入本文、脚本、日志或 Git；使用安全临时环境变量和仓库约定的占位符。

## 3. 15.6 分支已冻结到哪里

15.6.1/24G90 的决定性结论已经写在：

- `docs/porting/HANDOVER-MACOS15-FROZEN-2026-10-04.md`
- `docs/porting/T8103-PMAP-NESTED-OWNER-FIX-PROPOSAL.md`
- `docs/porting/dyld-15.6.1-state.md`
- `docs/porting/MAC15-UNTRACKED-ARTIFACT-CATALOG-2026-10-04.md`

根因边界为 runtime-confirmed + RE-confirmed：T8103 PMAP-CS owner selection 在 shared-region unnest 后仍按旧 nested-region bounds 选空 nested pmap，不尊重已置位的 unnest ASID bitmap；原版 `/bin/echo HI` 仍未通过真实 24G90 dyld/cache 输出 `HI`。任何 kernel/PPL/PTE/PAC 写入都不属于 Ventura 接棒任务。

大 payload 已删除并记录在：

- `docs/evidence/mac15-payload-delete-manifest-20261004.jsonl`
- `docs/evidence/mac15-payload-delete-results-20261004.jsonl`
- `docs/evidence/mac15-device-top-inventory-final-20261004.jsonl`

本机删除：`macos-15.6.1-rootfs/` 约 20 GB、`analysis/dyld-cache-15.6.1/` 约 5.6 GB、`VirtualMacOniPad/.diag/guest-kext-15.6.1/`。15.6 的文档、dyld/kernel IDA 数据、打包脚本和分析目录仍保留，未来可重新下载/构建。

## 4. Ventura 版本事实：不要混淆 22D68 和 22F66

仓库 `AGENTS.md` 的广泛作者验证矩阵是：

```text
iPad13,6 / iPadOS 16.3.1 / 20D67  +  macOS Ventura 13.4 / 22F66
 iPad14,5 / iPadOS 16.0   / 20A8372 +  macOS Ventura 13.4 / 22F66
```

当前本机已有的 macOS 13 资产是：

```text
UniversalMac_13.2.1_22D68_Restore.ipsw       约 12 GB
VirtualMac/build/inputs/macos/22D68__MacOS/  约 3.8 GB
  dyld_shared_cache_arm64e
  dyld_shared_cache_arm64e.01
  .map / .a2s
```

`22D68` 是 macOS 13.2.1；`22F66`/`22F82` 是 13.4。它们不是同一个 build，不能把 22D68 的成功直接报告成作者 13.4/22F66 已验收。最稳妥的路线是：先阅读作者文档并决定是获取 22F66/22F82 的已安装 rootfs，还是以 22D68 做明确标注的 bring-up 对照；报告中始终写清 build。

已有 22D68 事实：

- cache 总虚拟跨度约 3.207 GB，落在 iPadOS 4 GB shared-region 窗口内；15.6 的约 4.77 GB 跨界问题不适用；
- `UniversalMac_13.2.1_22D68_Restore.ipsw` 的 OS 卷能提供 Templates/Data 骨架、`/bin`、`/sbin`、部分 `/usr`；
- IPSW 的 OS/cryptex/BaseSystem 卷没有完整磁盘 `libSystem.B.dylib` 和完整 framework 二进制，因为 macOS 13 系统库主要在 shared cache；
- 因此不能只把 IPSW OS 卷当成完整可启动 rootfs。优先使用作者已经安装好的 macOS 13.2.1/13.4 VM，或从 cache 合法抽取系统库并按证据验证；不要猜测性补库。

## 5. 关于“作者 iPadOS 13.1 和我们 iPadOS 13 是否差不多”

这里必须先消除命名歧义：

- 如果“iPadOS 13.1”指 **iPadOS 13.1 系统版本**，它与当前设备的 iPadOS 16.3 / 20D47 不是内核等价物。Darwin/XNU、AMFI/CoreTrust、rootless 越狱、Mach-O/VM、UIKit/SpringBoard、AGX 和私有 framework ABI 都跨了多个大版本；只能说同属 Apple silicon/iPad 生态，不能直接复用 binary offset、entitlement、launch contract 或 GPU ABI。
- 如果“13”指 **设备型号 iPad13,11**，那它就是当前 M1/T8103 设备型号，和作者资料中的 M1 目标属于同一型号家族；仍要用实际 `hw.machine`、`kern.osversion`、jailbreak backend 和 UUID 重新验收。
- 如果“13”指 **macOS 13 Ventura**，那讨论的是 guest userspace 版本，不是 iPadOS kernel。macPad 作者广泛验证的是 iPadOS 16.x 宿主 + macOS 13.4/22F66 userspace。

结论：不能把“iPadOS 13.1”和当前 iPadOS 16.3 当作内核层面基本相同；下一 Agent 必须按当前 20D47 设备重新做 inventory 和小步验收。

## 6. 下一 Agent 的必读顺序

1. `/Users/ciscohe/Desktop/macPad/AGENTS.md`（完整）
2. `/Users/ciscohe/Desktop/macPad/CLAUDE.md`
3. 本文
4. `README.md`
5. `docs/porting/HANDOVER-MACOS15-FROZEN-2026-10-04.md`（知道哪些不能重复）
6. `docs/porting/STATIC-b2-cache-rebuild-pipeline.md` 的 §19、§12、§17.4（22D68 资产和 4 GB 对照）
7. `docs/porting/AGENT-START-PROMPT.md`、`docs/porting/CLI-MILESTONE-2026-09-28.md`
8. `/Users/ciscohe/Desktop/VirtualMacOniPad/docs/AGENT-SUPER-HANDOVER.md`
9. `/Users/ciscohe/Desktop/VirtualMacOniPad/docs/VMGPU-REFERENCE.md`
10. `/Users/ciscohe/Desktop/VirtualMacOniPad/docs/macPad-AGENT-START-PROMPT.md`
11. `/Users/ciscohe/Desktop/VirtualMacOniPad/docs/macPad-PORT-TO-MACOS15-HANDOVER.md`
12. `/Users/ciscohe/Desktop/VirtualMacOniPad/docs/WORKLOG.md` 末尾

VirtualMacOniPad 是另一仓库：只读 `VMGPU/` 黄金基线和 `book-buster/`；本轮 macPad 任务不要修改、commit 或 push 那个仓库。

## 7. 推荐的最小执行计划

### Phase A — 只读 inventory，不安装

记录：

- 设备 `hw.machine`、`kern.osversion`、jailbreak/trust 工具；
- `/var/mnt/rootfs` 是否不存在；
- 当前 `/var/jb/usr/macOS`、`/var/jb/Applications`、launch jobs、剩余空间；
- macOS 13 source build（目标 22F66/22F82 或对照 22D68）、架构、UUID、cache 两片大小/hash；
- 设备和 host 是否都有作者需要的 `mount_bindfs`、`uicache`、`ldid`、`jbctl`、Python、tar、strings 等。

inventory 必须是只读、有限时、可复制 JSONL；不要递归扫描公共 runtime，不要直接启动 WindowServer。

### Phase B — 准备 13.x rootfs staging

不要直接把 15.6 的 `misc/install_rootfs_15.sh` 当成 13.x 安装器。先复制成版本明确的脚本，例如 `misc/build-rootfs-13.2.1.sh` / `misc/install-rootfs-13.2.1.sh`，再做：

1. 从已安装的 13.x VM 或合法挂载的 13.x rootfs 获取 `/System`、`/usr`、`/bin`、`/sbin`、Templates/Data 和 Data-volume skeleton；
2. 放入与 guest build 对应的 `dyld_shared_cache_arm64e{,.01}`；
3. 建立 `/tmp -> private/tmp`、`/etc -> private/etc`、`/var -> private/var`、`System/Volumes/Data` 等 symlink；
4. 记录每个 source、版本、UUID、SHA 和缺失项；
5. 先在 host 上检查 `/bin/echo` 的真实依赖闭包和 cache image，不写设备。

如果使用 22D68 对照，源码和文档中必须明写 `13.2.1/22D68`；如果目标是作者已验证路线，应获取并核验 `13.4/22F66` 或 `22F82`，不要用字符串替换假装 build 变更。

### Phase C — 13.x 安装器和 trust contract

现有 `layout/usr/macOS/bin/postinst.sh`、`layout/usr/macOS/bin/macos_gui.sh` 有大量 13.4/22F66/22F82 语义和 hash 分支。新增 22D68 支持前必须：

- 读取真实 22D68 `ProductBuildVersion`；
- 用 `misc/cdhash_slices.py` 或等价已核验流程计算两片 cache 的完整 CDHash；
- 在 host 回归中覆盖 22D68、22F66、22F82、未知 build；未知非空 build 必须 fail closed；
- 不能把 22D68 哈希写死成 22F66；
- 保留 `restore_cold_boot_trust` 的参数顺序和 shell branch regression；
- 通过 content-verified pipeline 部署，不直接 `scp` 覆盖已签名 vnode；
- 设备重启后重新核对 live trustcache membership。

所有 changed shell 先 `bash -n`，Python 先 unittest，运行时路径用源 hash/安装 hash 双核对。

### Phase D — 第一里程碑

先做 bounded 原版 CLI：

```text
/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI
```

同时保存 stdout、stderr、return code、`DYLD_PRINT_LIBRARIES`、cache path、dyld/libSystem UUID/来源、进程/崩溃和 thermal 状态。要求：

- 原版 `/bin/echo`；
- 真实目标 13.x dyld、真实系统库/cache；
- 不用 `tmp/shim`/磁盘 libSystem shim 冒充成功；
- 失败时保留完整原始日志并停止扩大范围；
- `cat`、`sh`、WindowServer、GUI 之后再做。

如果作者的历史“CLI passed”使用了 shim，必须按 `CLI-MILESTONE-2026-09-28.md` 的注记重新标为 shim witness，而不是 cache witness。

### Phase E — GUI 前检查

只有 Phase D 通过后，才审查：

- rootless MacWSWindowing load command (`@rpath/CydiaSubstrate.framework/CydiaSubstrate`);
- 13.4 WindowServer UUID/arm64ify preparation；
- `postinst` 完整 dependency closure/trust；
- macOS 13 AGX/Metal 13.4 适配和 M1 20D47 gate；
- macwshostd/display/input/Host contract。

先用作者已有 `macos_gui.sh`/coexist 流程；不要直接 exclusive、不要先改 GPU kernel、不要把 15.6 的 kernel blocker 带入 13.x。

## 8. macPad/upstream 变更如何使用

本次 `eada48e` 已合并，推荐使用其中的：

- iPadOS 16.0 前没有 active Chamois 时复用当前 Scene，避免错误 Split View；
- Floating Dock 修复放在 authoritative center/resize transaction，并保持 assertion lifecycle；
- rootless MacWSWindowing 的 CydiaSubstrate `@rpath` contract；
- AppInput exact PID/window route、IME/keyboard wire tests；
- bounded keyboard latency diagnostic（默认关闭）；
- artifact contract 和 startup readiness tests。

这些改动是 macPad iPadOS-side/runtime 兼容性改进，不等于 13.x rootfs 已安装或 GUI 已验收。先保留 upstream 的 fail-closed 版本/UUID gates，不要把窗口修复扩展成新设备万能兼容。

## 9. 设备安全和证据纪律

- 所有设备操作串行；不与其他 Agent 并发 SSH。
- 只使用安全临时认证环境；绝不把密码写到 handover、commit、日志或命令历史。
- 禁止运行 `fmt13_patch.py`、旧 kernel candidate、未经重新核验的 PTE/PAC/kernel text 写入。
- 禁止症状式 NOP、强制分支、全局 assert bypass、伪造 object/zero buffer。
- 二进制 RE 使用 IDA Pro MCP；先 server health、input path、imagebase、UUID，再 decompile/py_eval。不要用本地 Python/otool/strings byte-hunt 替代 RE。
- 每个事实标记 `runtime-confirmed`、`RE-confirmed`、`source-confirmed` 或 `THEORY`，附原始证据路径。
- 每个 commit 立即 `git push origin main`；不要 force-push。
- 不要把 rootfs、IPSW、cache 或商业/Apple payload 放入 Git。

## 10. 当前交接结束条件

下一 Agent 读完本文后，应能立即回答：

- 当前设备没有 15.6 rootfs，为什么；
- 22D68 与作者 22F66/22F82 的差别；
- 13.x rootfs 为什么不能只从 IPSW OS 卷复制；
- upstream `eada48e` 改了什么、没改什么；
- 第一个可审计实验是哪个；
- 什么结果才算真实 CLI 成功；
- 哪些 15.6 kernel/VM 方案禁止带入 Ventura。

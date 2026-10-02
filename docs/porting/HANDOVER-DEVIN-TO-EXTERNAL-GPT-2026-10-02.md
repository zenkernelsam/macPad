# macPad 超级交接：Devin → 外部 GPT（2026-10-02）

## 0. 接棒的第一屏：不要从旧实验计划直接执行

这份文档是用户因 Devin 订阅额度即将耗尽而请求的跨客户端交接。仓库在同一台 Mac：

`/Users/ciscohe/Desktop/macPad`

**总目标不变：在越狱 iPad13,11（M1/T8103）上，以真实 macOS 15.6.1 / 24G90 dyld、系统库和 shared-cache 跑通 macOS CLI；之后才考虑真实 GUI。不是 VM，不接受兼容 shim 替代核心 CLI 成果。**

第一里程碑：原版 macOS `/bin/echo HI` 经真实 dyld/系统库打印 `HI`。**截至本交接尚未达成。** 进程存活、构建成功、信任恢复成功都不等于该里程碑。

### 紧急状态

- 用户已经重启、重新越狱；本轮密码认证 SSH 成功，设备只读身份确认。
- 用户授权无人值守继续，不希望反复问确认。保持设备操作串行，无其他 agent 同时触碰设备；授权不等于允许不可逆删除或未经核验的内核写入。
- **冷启动 TC 基线已拿到并保存；不要要求再重启来重复已经完成的步骤。**
- **设备正式 `macos_gui.sh trust` 已运行成功。两个 24G90 cache CDHash 仍缺失，Ventura 对已加入。** 这为修改共同 cold-boot trust 函数提供运行见证。
- **本轮还没有改 `macos_gui.sh`、没有加回归测试、没有部署 source patch。** 下一步就在这里。
- 本轮没有启动 chroot/WindowServer，没有替换 dyld，没有运行 fmt13 kernel patch。
- 当前 dyld SHA 与历史恢复 SHA 不同。不能用旧 SHA 假定当前部署是什么。
- 原有 `post_reboot_tc_probe.sh` 和 `post_reboot_fmt13.sh` 不要直接运行，问题见 §6。
- 旧 `KERN_CODESIGN_ERROR=50` 与后来的 m3 `KERN_MEMORY_ERROR=10` 是两组不同配置/故障，不要混为一谈。
- **fmt13 支持缺口有强 RE/历史 runtime 证据，但 prepared kernel patch 没有设备验证，不能把它当成完成的修复。**

## 1. 信息优先级与必读顺序

新鲜设备见证及实际源码 > 本文最新接棒状态 > state doc 中匹配配置的证据 > 旧 handover/聊天总结。旧文档有被更正的断言，不能以最后一段的口气强弱决定真伪。

1. 完整阅读 `AGENTS.md`（1856 行，允许分块读取）；尤其顶部 patch/evidence discipline、Current Project Memory and Operating Baseline、Session-State Recovery、Kernel write safety。
2. `CLAUDE.md`（9 行，仅指向 AGENTS，不能忽略）。
3. 本交接文档。
4. `docs/porting/dyld-15.6.1-state.md` 的最后几个 2026-10-02 条目，重点 **“冷启动基线成功”** 和 **“共同 trust 恢复路径运行见证”**；再按故障引用回溯历史，不必一开始吞全部聊天。
5. `docs/porting/UPSTREAM-MERGE-AND-CLUES-2026-10-02.md`。
6. `docs/porting/TOOLS-AND-PORTING.md`（其中历史“private mmap 是死路”不是对最新实验的最终裁决）。
7. `docs/evidence/m1-dyld-pager-format13-20260930.md`、`docs/porting/STATIC-m3-dyld-pager-format13.md`。
8. 实际源码 `layout/usr/macOS/bin/macos_gui.sh`、`postinst.sh`、`macws_boot_trust.py`、`misc/test_restore_boot_contract.py`、`misc/device_pipeline.sh`。

上游 7 个 GUI/input/window commits 已经 merge，merge commit `cc56e15`；它们没有 dyld/15.6.1 修复。不要把 upstream merge 当成解决 shared-cache 故障的新代码。

## 2. 当前设备身份、认证和真实部署

### 2.1 本轮 runtime-confirmed 身份

```text
uid=0(root) gid=0(wheel) groups=0(wheel)
hw.machine: iPad13,11
kern.osversion: 20D47
kern.boottime: { sec = 1790941117, usec = 852384 } Fri Oct  2 19:38:37 2026
```

rootfs `SystemVersion.plist`：ProductVersion `15.6.1`，ProductBuildVersion `24G90`。设备内核对应历史研究目标 xnu-8792.82.2；源树 xnu-8792.81.2 只是参考，不能无条件等同实际 binary。

### 2.2 认证纪律

- SSH 端点及用户按 `AGENTS.md` 的 Device Access 节读取，记录命令使用 `MACWS_DEVICE` / `MACWS_DEVICE_PORT` 占位符，不在新文档硬编码网络身份。
- 端口本轮是 2222。`BatchMode=yes` 最初认证失败；不能据此说设备 offline。
- 密码仍为用户约定值，用户已经确认；不要在文档、代码、shell 参数 `-p <password>`、日志或提交中再写密码。
- 用临时 `SSHPASS` 环境配合 `sshpass -e`，不得打印该变量、不得 `set -x`；本交接不携带凭据。接棒客户端若没有安全凭据通道，先做本地工作，不猜测认证。
- 成功参数：`-o PubkeyAuthentication=no -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 -o ConnectTimeout=5`。
- 设备操作串行；不要并发 SSH 实验，不要 spawn background agent 来碰设备。

```bash
sshpass -e ssh -o ConnectTimeout=5 -o PubkeyAuthentication=no \
  -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 \
  -p "$MACWS_DEVICE_PORT" "$MACWS_DEVICE" '<bounded command>'
```

### 2.3 当前文件核验值（不要与旧值混用）

| 设备路径 | 本轮结果 |
|---|---|
| `/var/mnt/rootfs/System/Library/CoreServices/SystemVersion.plist` | 603 B；SHA256 `9af8c8d66fb9e5f022d93f46481c8834b787c2ec61e96da6d4a33965640ce2b2` |
| `/var/jb/usr/macOS/bin/macos_gui.sh` | 264670 B；SHA256 `af9b213980fe679fd02f43ec82e72906ec3ac77dd9d8ad3f923c2a8a2492021b` |
| `/var/jb/usr/macOS/bin/macws_boot_trust.py` | 19206 B；SHA256 `2c727c302a55b15470f9bc1cf4ec8c45e87091e8b448974d866e8ca1dd955135` |
| `/var/mnt/rootfs/usr/lib/dyld` | 1239632 B；SHA256 `b8fdbc1b7cfd15cccbcd110c0c3cb1ff91d135d6664b84770d42df843381b91e` |
| `/var/mobile/run_dbg_hold_v2` | 53072 B；SHA256 `5af5df15f95d5b1e80809ad9c89e512b2cb8cfc05469e03717f937537b873ff5` |
| `/var/mnt/rootfs/usr/macOS/bin/macos_gui.sh` | 不存在；不要使用旧 TC probe 的错误搜索路径 |

设备实际脚本与 host source 不完全一致，不能整文件覆盖而不比较。历史恢复 dyld SHA `9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1` **不是当前值**。

设备工具：Python `/var/jb/usr/bin/python3`（本轮 traceback 为 3.9）；bash `/var/jb/usr/bin/bash`；ps `/bin/ps`；sysctl `/var/jb/usr/sbin/sysctl`；TC `/var/jb/basebin/jbctl`。`/var/jb/usr/bin/ps` 不存在，已纠正，不再重复。

最后一次 `/bin/ps -axo pid,comm` 对目标名筛选仅见 PID 372 的 iOS `macwshostd`。没见 WindowServer/launchdchrootexec/autosignd/runner；这不证明历史上从未启动过 chroot。运行 `trust` 后尚未再次做全部进程列表核验。

## 3. 本轮决定性证据：冷启动 cache 信任恢复缺口

### 3.1 完整 CDHash

| build | 主 cache | .01 |
|---|---|---|
| 24G90 / 15.6.1 | `2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e` | `8c7ba7e588b0edd43f7334e2de11688cd4732192` |
| 22F66/22F82 / Ventura | `b5da39409492ac85e5a8e8ab618fe77e2d7a2980` | `bbb765988e2677b98d47a549d612fa0d4af25f69` |

冷启动原始完整 TC 输出（69 项）在 `docs/evidence/cold-boot-trustcache-20261002.raw`。`TRUSTCACHE_QUERY_RC=0`；24G90 两个完整哈希均不在输出中。该文件是新保存的原始证据，不含认证凭据。

### 3.2 实际调用及逐字运行见证

在用户授权后运行了：

```bash
/var/jb/usr/bin/bash /var/jb/usr/macOS/bin/macos_gui.sh trust
```

通过 iOS Python `subprocess.run(..., stdout=PIPE, stderr=STDOUT, text=True, timeout=180)` 保存完整输出，不 `tail` 截断，再查 `jbctl trustcache info` 并按完整 40 位哈希匹配。

```text
COMMAND /var/jb/usr/bin/bash /var/jb/usr/macOS/bin/macos_gui.sh trust
BOOT-TRUST progress files=0 images=0
BOOT-TRUST {"added": 77, "backend": "libjailbreak", "cached": 0, "files": 93, "hashes": 78, "images": 77, "resource_hits": 0, "scan_seconds": 0.142, "total_seconds": 0.181}
[macos_gui] Cold-boot trust closure ready (complete dependency closure; live membership verified).
TRUST_RESTORE_RC 0
TRUSTCACHE_QUERY_RC 0
HASH 2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e PRESENT False
HASH 8c7ba7e588b0edd43f7334e2de11688cd4732192 PRESENT False
HASH b5da39409492ac85e5a8e8ab618fe77e2d7a2980 PRESENT True
HASH bbb765988e2677b98d47a549d612fa0d4af25f69 PRESENT True
LIVE_HASH_COUNT 178
```

**runtime-confirmed**：共同信任恢复函数完成，却恢复了错误 build 的 cache 对并报告 ready；24G90 对仍缺失。

**没有做**完整 `production`。这次有意用正式 `trust` 子命令隔离同一个 `restore_cold_boot_trust`：实际设备 `5472-5477` 明确调用它；不停止/启动 GUI。完整 production 还会清理文件、改 jobs，不适合无人值守单变量对照。报告必须写清是共同函数验收，不是完整 production 验收。

### 3.3 为什么需要显式 cache 哈希

`postinst.sh` 已有 24G90 分支；`macos_gui.sh` 的 cold-boot helper 仅硬编码 Ventura 对。helper 扫描普通 Mach-O CodeDirectory，不能靠普通 Mach-O 扫描发现 `dyld_v1` shared-cache。Dopamine dynamic TC 是 reboot-volatile。

项目 cache 使用动态 TC 准入，不要重新签 shared-cache 或编辑其内容。普通 Mach-O 的签名策略不能机械套到 DSC。

这仅解释冷启动恢复缺口，**不能解释旧实验中“TC 已验证存在仍 codesign fault”**。不要因为本轮找到一个真 bug 就宣称全部问题都由它引起。

## 4. 下一位 agent 应从这里开始：测试先行的最小 trust 修复

### A. 本地修复（本交接时尚未执行）

1. 读 `misc/test_restore_boot_contract.py`（当前 105 行，已有 8 个 tests）；先补失败回归。
2. 对 `layout/usr/macOS/bin/macos_gui.sh::restore_cold_boot_trust` 做最小 build→hash 选择。
3. 复用 `layout/usr/macOS/bin/postinst.sh:1062-1080`：用 iOS Python/plistlib 读取 `$ROOTFS/System/Library/CoreServices/SystemVersion.plist` 的 `ProductBuildVersion`。
4. 保留 Ventura `22F82|22F66|空值` 历史 fallback，不破坏既有验证平台；24G90 用上表完整 pair。
5. 未知非空 build 建议 fail closed，报错并 `return 1`，不得登记已知但错误的 cache 对、更不得报 ready。空值 fallback 风险须在测试/文档中明确，不要把它当可靠 build 检测。
6. 尽量将选定的 `--hash` 参数追加到已有 positional args；不要破坏扫描 path 列表、thermal gate、helper live-membership 验证或 manifest/resource-index 参数。
7. 回归不只 grep `24G90` 字符串：应执行实际 shell selection 分支，覆盖 24G90、两种 Ventura build、空 build 和未知 build；断言 pair、原 args 保留、未知失败，并验证选择发生在 helper 调用前。
8. 不要增删现有注释，除非用户明确要求；保持紧凑、使用现有库，不引入新依赖。

本地验证候选：

```bash
python3 -m unittest misc.test_restore_boot_contract
bash -n layout/usr/macOS/bin/macos_gui.sh
bash -n misc/device_pipeline.sh
git diff --check
```

再按范围运行 boot-trust 相关 tests；可行时全 `misc/test_*.py`，但不要把 unrelated 旧失败掩盖为本 patch 成功。

### B. 部署与运行验收

1. 先核验当前 host/device 脚本差异和设备 repo/Theos 是否存在；不要因路径历史记载而假定存在。
2. 使用 `misc/device_pipeline.sh` 的内容校验链，先审阅 `--component runtime` 的实际实现、覆盖范围和权限/SSH 方式。它 defaults 的设备/端口**不适合本轮**，必须传正确环境；它没有给任意 dyld kernel patch 提供自动安全证明。
3. 不要为了仅脚本改动直接跑 full package/postinst；后者会扩大信任/重签和部署范围，破坏受控比较。若 pipeline 无法最小部署，报告阻塞并补最小经校验通路，不擅自 ad-hoc 覆盖 signed dylib。
4. 保留设备原脚本及 inode/content 身份；禁止不经比较将当前 device script 换成较旧 host version。
5. 部署后再次正式 `macos_gui.sh trust`；必须 query rc=0 且两完整 24G90 hashes 为 present，helper verified；保留完整 stdout/stderr/rc，独立核验安装文件 SHA。
6. 初次 baseline 已保存，TC membership 单向添加，不清空 TC 来重做 baseline；下一次真正 reboot acceptance 是后续独立关卡，不要宣称已做。
7. 每步写入 state doc。只有设备补后见证才能把 boot-critical gate 标为完成。
8. 每次 commit 后立即 `git push origin main`；stage 仅自己明确修改的文件，不把现场 untracked binaries/config 都加进去。

### C. 回到真正 CLI

在信任修复验收后，先识别当前部署 dyld 的补丁组合、真实 cache 路径、runner 配置，再跑单个 bounded 原版 `/bin/echo HI` child。别一次叠 TC、dyld 变体、fmt13、ptrace、清 flags 等多个变量。

原始里程碑形态仅供语义说明，实际 runner/TC/closure 先核验：

```bash
/var/mobile/run_nocskill /var/jb/usr/bin/env -i PATH=/usr/bin:/bin \
  /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI
```

需要捕获异常时才用 `RUN_DBG_HOLD=30 /var/mobile/run_dbg_hold_v2 ...`，先查 runner 源码与实际 binary 的对应关系；HOLD 是否真正保持孩子要用 PID/状态验证，不能假定。

## 5. 已知技术前沿与证据档位

### 5.1 shared-region / 私有映射

- iOS arm64 SR：`0x180000000..0x280000000`，4 GiB。
- macOS 15.6.1 cache header：sharedRegionSize `0x12c760000`，大于 4 GiB；split cache 的系统范围路径受限制。
- dyld `mapSplitCachePrivate` 固定基址，private slide=0；cache 的真 rebase/PAC fixups 即使 slide=0 也必须执行。
- dyld `deallocateExistingSharedCache` 在 `check_np(&base)` 返回非零时跳过 NULL teardown；空 SR 曾实测 ret=12。
- `emptysr_e/c` 是只对 ret=12 补做 teardown 的诊断/候选；`deallocnp` 是所有非零都强制 teardown 的历史诊断，不作为默认 fix。
- `highreserve_e/c` 是高 VA PROT_NONE **非固定 hint、必须恰好命中，否则 exit 87** 的诊断。它不替代后续真正 file mappings。
- 受控历史试验越过边界推进到 m3，不代表已经跳到 cache text 成功。
- `build_dyld.py` 的 DEFAULT 有历史 `fcntl_nop`/coverage skip 等，**不能拿 DEFAULT 当 clean verified baseline**。`dearm64e` 广泛转换也不是最终真实 PAC 修复。

本地 prepared frontier 配方（历史准备，不是本轮部署）:

```text
crossarch plataccept hardpriv emptysr_e emptysr_c highreserve_e highreserve_c
```

产物 `analysis/dyldwork/dyld_fmt13_frontier.bin`（analysis 可能 ignored，不随 git 带走）；历史 unsigned host SHA `d6f23861f1dceeca0f066de466383d07feebcabdf3ada76e8ce69758c951df01`、1240752 B。重新签名后 SHA/CDHash 都变化，不能拿 unsigned hash核验 signed deploy。

### 5.2 m3 / format13（不要再找不存在的 printf）

m3：VA `0x1ee188000`，size `0x24000`，file offset `0x6c188000`；cache slide version 5。

历史 runtime 原 cache 物理路径为 `/var/mnt/rootfs/private/tmp/dsc/dyld_shared_cache_arm64e`，chroot symlink `/System/Library/dyld/dyld_shared_cache_arm64e -> /tmp/dsc/...`。其他日志用 `/macdsc`/Cryptexes；**当前实际使用哪个必须再核验，不能都当同 vnode**。

RE-confirmed via static note / IDA captures：

- dyld `0x349ec..0x349f8` 写 linking header version 7 / page_size 0x4000 / ptr_format 13。
- `0x34b84` 调 syscall 550 `map_with_linking_np`；wrapper `0x769cc`。
- kernel pager `sub_FFFFFE00080661A4` 的 dispatch `0xfffffe00080667e8..67f4` 只容纳 format 1..12，13 落 default。
- default 记录 `DYLD_PAGER_SLIDE_ERROR` triage `0x04000008` 并返回 KERN_FAILURE=5，VM fault 最终可变成 KERN_MEMORY_ERROR=10。
- syscall 550 前端校验 version/page_size/offsets/CS coverage，缺 pointer_format 支持性校验；接受不等于 fault 时能 fixup。
- release 内核没有该 printf 字符串，查 msgbuf 无输出不排除此路径。看 `.ips` 的 kernel triage 和真实 pager/link_info。
- 历史 `shadow=3/resident=0/external=1` 单独不识别 dyld pager；普通 COPY native 对照也出现 shadow=3 且读取正常。
- `DYLD_PAGEIN_LINKING=0` 受 dyld internalInstall gate，不能默认当有效禁用开关。

已存在 dyld 正规 in-process fallback：`0x34790` 的路径选择、`0x34b88..` syscall 返回检查。研究是否能通过真实能力条件进入完整 rebase/PAC 路径是可选方向，不允许格式13改12、跳过 fixups或假成功。

### 5.3 旧执行页 codesign 路径仍有未解项

历史页 offset 约 `0x47c000`：validated=0xf、tainted=0、nx=0、wpmapped=0；xpmapped=1 **在 pmap_enter 前乐观置位，不是执行成功证据**。

entry flags `0x210abac0`；参考源布局 flags2 在 +0x48，bit24 pmap_cs_associated、bit19 permanent、bit29 no_copy_on_read。RB_ENTRY=3 pointers/24B 的支持性核实，不自动证明所有设备布局。

**THEORY**：空 SR submap clip/保留导致 CS association 错位。需要从 actual binary 和 fault snapshot证明，不能凭源码里非-PPL assert说“只能继承”，也不能把排除几个源码分支推成“必定 PPL”。老 state doc 有过强断言，本交接不继承其确定口气。

决定性诊断：实际 fault page 属主 VM object/shadow chain、pager/vnode/UBC、code_signed、blob base/start/end与页 offset、csb_pmap_cs_entry。UBC cs_blobs 已纠正为 +0x50，+0x10 是 PAC'd ui_vnode，不要再把恒非零 vnode 当 blob。

## 6. 现有工具不是安全证明：接棒必须审计的风险

### 6.1 `misc/fmt13_patch.py`：禁止直接运行（包括所谓 verify）

源代码在顶层初始化 KRW 后自行验证并写入；**没有安全的纯 dry-run 子命令**。注释提到 verify 不代表实现了 `--verify`，多传该字符串仍可能进入写路径。

当前源实现风险（源码审查，不是设备运行结论）：

- 仅把 IDB VA + slide 当 runtime VA；少数 expected words 对不上会 abort，但不能替代运行时定位和完整窗口匹配。
- CAVE 用 `bytes.fromhex("7100355f ...")` 储存反汇编 word 形式，之后按 little endian 取 word；**必须经 IDA/汇编器逐条复核字节序**，不能假定先前“构建验证”已解决。不要用本地 ad-hoc hex 数学取代 IDA。
- 覆写 format 2/3/6 handler 的 kernel 全局代码，声称这几个格式对 arm64e shared-cache dead；**不等于它们对系统其他进程 dead**。重定向其 slots 为 failure 改变全局能力，不可视为无害。
- `--undo` 只恢复 dispatch，未恢复被覆盖的 handler 全部字节；原 formats 的 entry可能重新指到被覆盖代码。**不是完整 rollback**。
- 没有证明完整备份、失败时原子恢复、执行并发安全、kernel text instruction-cache 同步方案。
- 写错 kernel text 或 PAC pointer 会 panic；本设备历史已经发生过 panic。

因此：把这份脚本当算法/地址线索草稿，不把它当可部署修复。先证明真实 runtime地址、完整 original bytes、PAC ABI、whole-system格式影响、回滚和 coherency，再讨论执行。

### 6.2 reboot wrapper / TC probe

- `post_reboot_fmt13.sh` 自动 restore + TC add + kernel write + echo，多变量、跳过 cold baseline；cache hashes reader是否支持 DSC 也需验证。不直接运行。
- `post_reboot_tc_probe.sh` 只 grep短前缀，把 query失败和无匹配混在 `|| ABSENT`，只 `tail -30`，启动路径未含真实 `/var/jb/usr/macOS/bin`。没有修复，不直接运行。
- TC检查必须先确认 root和 query rc，再完整40 hex逐项/大小写不敏感匹配。
- 自动重签/复制脚本可能删除原文件或就地写 signed inode，先检查，不因为在仓库就默认安全。

### 6.3 csprobe/pagewalk/runner

- `csprobe.py` / `csprobe2.py` 的 VM object 字段不同：前者历史 +0xac/+0x7c 等，后者 +0xa4/+0x74 等；必须用实际 kernel IDB和runtime交叉核验，不能混用。
- `csprobe2.py` 的 submap union decode `& ~0xffff` 以及 SR扫描偏移 **没有本轮设备验证**；结构里的 sr_q/root_dir 等会改变推算，需核实，不当成精确工具。
- unpac脚本有旧实现，项目要求47-bit；统一前先证实字段类型，NULL/packed/page indices不能当PAC pointer。
- vm_pages历史 lowGlo定位、stride有多次纠正：把末尾当基址、0x40与0x30混用都是旧错，详读 pagewalk 和日志。
- `run_dbg.c` exception reply已改为请求 `msgh_remote_port` + request NDR，历史trap control见证；不要恢复到 receiving port（会 MACH_SEND_INVALID_DEST）。
- runner的 ptrace/unkill thread 等可能改变fault条件，诊断不等于普通 exec 成功，不默认开启。
- 不允许全局关签名策略、清 CS flags 的竞态当成产品修复；如诊断使用必须说明变量和作用范围。

## 7. IDA Pro MCP：跨客户端复用，不继承错误实例标签

### 7.1 当前工具和最新 health

Devin 会话可见四个 MCP server：

```text
ida-pro-mcp-Instance1
ida-pro-mcp-Instance2
ida-pro-mcp-Instance3
ida-pro-mcp-Instance4
```

交接准备时实际调用 Instance1 的 `server_health {}`：

```json
{"status":"ok","idb_path":"/Users/ciscohe/Desktop/macPad/analysis/kc_raw_16.3_T8112.bin.i64","module":"kc_raw_16.3_T8112.bin","input_path":"/Users/ciscohe/Desktop/macPad/analysis/kc_raw_16.3_T8112.bin","imagebase":"0xfffffe0007004000","auto_analysis_ready":true,"hexrays_ready":true}
```

其他三实例本轮未health核验；**必须每次先列工具再调用 server_health，实例名与二进制绑定会变**。

历史候选端点（从 AGENTS 记载，接棒需验证）：

- Instance1 `http://127.0.0.1:13337/mcp`：kernel。
- Instance2 `http://127.0.0.1:13338/mcp`：dyld。
- Instance3 `http://127.0.0.1:13339/mcp`：amfid。
- Instance4 endpoint未核验，不猜端口。

这些是已有本地 IDA 服务，可在另一个客户端配置相同服务器；不要启动另一个IDB误覆盖当前实例。新配置用该客户端实际支持格式，优先 `.devin/` 或用户级 Devin `~/.config/devin/`；在用户没要求时不要擅写 `.claude/`/`.cursor/`。旧兼容配置记载在 `~/.qoder-cn/mcp.json`/settings，不要为了读取server配置泄露任何headers或token。

### 7.2 工具使用约束

- RE前先搜索项目文档，避免重做已排除假设。
- 正式分析通过 IDA MCP：`find_regex`、`xrefs_to`、`decompile`、`lookup_funcs`/`list_funcs`、`py_eval`。**具体 schema 以 tools/list 为准，不能猜字段。**
- `py_eval` 使用 `ida_nalt.get_input_file_path()`、`idaapi.get_imagebase()`、`ida_bytes.get_bytes()`、`idc.generate_disasm_line()` 等进行binary/address/bytes provenance分析。
- 不用 ad-hoc Python byte hunting、otool/strings管道找binary xrefs/拼补丁字；需要patch bytes也先由IDA证明。
- 本地/设备Python合法用途：设备文件读取/复制/哈希、plist/日志JSON、packaging、已部署bytes复核；不是替代IDA的binary RE。
- 没有MCP wrapper时，可用MCP HTTP协议 initialize→notifications/initialized→tools/list→tools/call；保存服务返回 mcp-session-id、处理SSE/JSON内容，不是裸REST调用 `/decompile`。
- MCP无法连时先用既存RE captures，标为历史证据，不能编造本轮decompile。

### 7.3 要在IDA加载的绝对路径

- kernel：`/Users/ciscohe/Desktop/macPad/analysis/kc_raw_16.3_T8112.bin`（文件名T8112误命名，实际T8103目标；imagebase 0xfffffe0007004000）。
- dyld：`/Users/ciscohe/Desktop/macPad/analysis/dyld_15.6.1_arm64e_thin`（historical base 0）。
- amfid：`/Users/ciscohe/Desktop/macPad/analysis/dyldwork/amfid_bin`（historical base 0x100000000）。
- 参考源：`analysis/xnu-xnu-8792.81.2`、`analysis/dyld-dyld-1286.10`。

已有本地、尚未tracked的IDA证据：

```text
.ida-mcp/kernel-dyld-pager-format-proof.txt
.ida-mcp/dyld-cache-format13-proof.txt
.ida-mcp/dyld-prepare-m3-write-proof.txt
.ida-mcp/enter_helper.json
.ida-mcp/vcs.json
```

这些不一定存在于仅clone的fork；在这台Mac上接棒可直接读，不用先重跑全binary scan。

## 8. 不可丢失的行动约束

- 中文回复；证据分 `RE-confirmed via binary+offset`、`runtime-confirmed via raw log/crash`、`THEORY + confirm/refute实验`。
- 不把源码comment当当前runtime evidence；不把expected offset当actual binary layout。
- 状态文档每实验立即更新：exact cmd、完整相关日志、rc、部署SHA/CDHash、改变变量、结论和不证明的内容。
- 原始输出中敏感身份脱敏时明确标注，不能称脱敏部分为逐字完整；不得提交密码、keys、tokens、SSH private材料或Apple rootfs/framework payload。
- 日志不只 `tail`；管道丢rc要显式处理。认证失败、query失败、missing tool各自分类，不等于设备离线/哈希缺失。
- 修复上游应有的状态，不NOP required setup，不强行成功返回，不全局绕过abort/check，不用zero blob假对象，不用匿名zero pages替代DSC fixup。
- genuine CLI success只认`HI`；GUI只认真实pixels/round trips，不认uptime。
- kernel读写地址不能hardcoded slide推算后盲信，PAC pointer不raw-write；未知identity fail closed。
- 设备串行，不与邻居agent并发触碰；未明确要求不使用subagents。
- 不永久删除host/device文件，移至可恢复Trash；不运行会删除现有文件的production/cleanup路径来“顺手修复”。不可逆操作须具体授权，不能用“无人值守”替代。
- deploy走content-verified pipeline；signed dylib不要in-place scp，保留fresh inode和backup。
- 有test infra先失败test再修，遵循现有风格，不引入未确认依赖，不增删无关注释。
- Git：不改config、不force、不reset/checkout覆盖用户改动；不stage全部现场；只明确source/evidence文档；每commit立即push `origin main`。有并行agent提交时重新检查HEAD/diff，不替别人捎带commit。

## 9. 聊天记录、技能和跨客户端记忆边界

### 9.1 已核验的当前会话文件

当前session ID：`coffee-soap`，title `Switch to Devin CLI and continue previous tasks`。

**当前ATIF transcript：**

`/Users/ciscohe/.local/share/devin/cli/transcripts/coffee-soap.json`

本轮实际读取开头确认：

```json
{"schema_version":"ATIF-v1.7","session_id":"coffee-soap","agent":{"name":"devin","version":"3000.11.3","model_name":"GPT-6.1 Sol Medium Thinking"}}
```

这只是摘录元信息，不是整个JSON内容。该文件已包含本轮设备TC检查和API/Outposts讨论；会话在本交接期间持续增长，最后一个未完成turn不一定已导出。可在结束后再读最新版本。

**前序完整聊天历史文件（上一轮summary给出，已验证存在）：**

`/Users/ciscohe/.local/share/devin/cli/summaries/history_83dd6af563c64fa6.md`

更早历史在同目录，例如 summary 引用 `history_752be851eec94cc5.md`，不是当前接棒第一读取目标。

Devin session数据库：`/Users/ciscohe/.local/share/devin/cli/sessions.db`，仍有WAL/SHM且接棒时db约1GB。**不要编辑或拷走一份无WAL的live DB来当完整备份，不需要读取整库。** 现成transcript+summary+repo evidence足够安全接棒。

raw transcript可能包含用户明文凭据、工具临时环境和长工具输出的截断（本轮grep已见 `[truncated, original length ...]`）。**不把整个聊天JSON/summary提交Git或公开分享。** 另一GPT在同机按需读取，但不得在回答/日志重显凭据。原始evidence文件比截断工具输出更可靠。

继续Devin本会话（若后面额度恢复）：

```bash
devin -r coffee-soap
```

ATIF不是对所有agent客户端都可直接import的格式；可作为按role/tool_calls阅读的文本证据，不假装能够迁移服务商内部hidden state。

### 9.2 实际技能与工具

本会话调用的 `devin-cli` skill 是本地官方文档入口，不是核心macOS实验依赖：

`/Users/ciscohe/.local/share/devin/cli/_versions/3000.11.3/share/devin/docs`

重点 `models.mdx`、`reference/commands.mdx`、`reference/configuration/config-file.mdx`。其他客户端没有此skill时直接read即可。会话可见的 declarative-repo-setup/upload-secrets 与本研究无关，不必假装迁移。

普通Devin CLI 3000.11.3公开文档未发现第三方模型BYOK/custom base_url；`/model`可切Devin提供的模型，不能保证绕开quota。用户提供的 Outposts reference 是自托管执行worker，不是第三方模型推理key配置。因此这次换客户端，而不是绕CLI内部认证。

工具映射：read/grep/glob/edit/write、exec、todo、MCP工具可用替代客户端等价能力。**工具能力来自客户端实际配置，不是读文档就自动有权限。** 无shell/文件/MCP能力时要如实报告，不能假称执行过。

## 10. 仓库快照和交接提交范围

交接开始的已提交HEAD：`a6f7d7e`（tools+docs fmt13/map-walk/TC probe）。此前 `bd5e53a`、`734d6b8`、`cc56e15` 是merge report链。

本交接要提交的只有：

- `docs/porting/dyld-15.6.1-state.md`（本轮+118行runtime记录，以及交接链接）；
- `docs/evidence/cold-boot-trustcache-20261002.raw`；
- 本文。

本交接**不提交、不删除**所有其他现场untracked：`.ida-mcp/`、`.qoder/`、misc probe源/binaries/objects，以及根目录scheck。这些可能含他人工作；同机保留，接棒先 `git status --short`。`analysis/`大部分产物不靠git持久化，不能以clone缺失说实验没做过。

交接提交最终SHA及push结果以交接agent最后回复和 `git log` 为准；不要在本文件硬编码自身commit造成循环。用户已经要求commit后立即push。

### 10.1 交接验证与已有测试基线失败

本轮执行 `git diff --check` 通过，raw TC 行数核对为69，交接文档凭据模式检查无匹配。未修改任何源码或测试文件。

```bash
python3 -m unittest misc.test_restore_boot_contract misc.test_agents_memory_ledger
```

逐字失败摘要：

```text
FAIL: test_package_declares_ios_tools_used_during_postinstall (misc.test_restore_boot_contract.RestoreBootContract.test_package_declares_ios_tools_used_during_postinstall)
AssertionError: Items in the second set but not the first:
'plutil'
'odcctools'
'gawk'
Ran 12 tests in 0.002s
FAILED (failures=1)
```

12项中11项通过。`control` 当前和 `git show HEAD:control` 均为 `Depends: python3, ldid`，测试期待5项依赖；这是本轮未改源码时可复现的基线不一致，不是尚未实施的24G90补丁回归。不要擅改包依赖或测试来让交接全绿；接棒修trust时单独标记这个既存失败，明确新tests是否通过。完整命令失败 exit=1，未隐瞒。

## 11. 给下一位 GPT 的启动 prompt（可直接复制）

```text
你是 macPad 项目的接棒设备侧 Agent。仓库 /Users/ciscohe/Desktop/macPad。

请先完整读 AGENTS.md 和 CLAUDE.md，再读：
/Users/ciscohe/Desktop/macPad/docs/porting/HANDOVER-DEVIN-TO-EXTERNAL-GPT-2026-10-02.md
以及 docs/porting/dyld-15.6.1-state.md 最后几个 2026-10-02 runtime条目。
不要从旧summary直接运行reboot/fmt13/deploy脚本。

目标：在越狱 iPad13,11 / M1 上，以真实 macOS 15.6.1/24G90 dyld、系统库、shared-cache跑通原版 /bin/echo HI；真的打印HI才算首里程碑。GUI以后再做，不接受核心shim替代。

最新设备证据已经落盘：重启+重越狱后24G90两cache完整CDHash均缺失；正式macos_gui.sh trust运行rc=0并报告ready，却只加入Ventura对。共同restore_cold_boot_trust遗漏24G90已runtime-confirmed。尚未改macos_gui.sh、尚未加regression、尚未部署patch。本轮没启动chroot/WS、没替换dyld、没写内核。

下一步是先加失败回归到misc/test_restore_boot_contract.py，再最小修build→hash选择（参考postinst的ProductBuildVersion读取，保留Ventura fallback，24G90选对应pair，未知build明确失败）。测试覆盖真正shell分支和live helper调用顺序；聚焦测试/bash -n/git diff --check；核验device_pipeline最小部署和设备脚本差异，再device trust见证两哈希present。每步写state doc，每commit立即git push origin main。

用户授权无人值守继续，不要反复确认；但设备操作必须串行，不与其他agent同时碰设备，不执行未经具体授权的不可逆删除，不盲写kernel/PAC指针。认证使用安全临时环境，勿打印/提交密码。当前dyld SHA不同于历史，先读handover里的identity table。

旧KERN_CODESIGN_ERROR=50和m3 KERN_MEMORY_ERROR=10是不同路径。format13内核缺口有RE证据，但fmt13_patch.py未runtime验收，含需要审计的byte-order/覆盖旧handler/回滚/运行地址风险，禁止直接运行（所谓--verify也不是只读模式）。先完成trust fix，再单变量bounded echo fault实验。

所有binary RE走IDA Pro MCP：先列servers/tools，再server_health核验真实input_path/imagebase；Instance1本交接health确认T8103 kernel，其他实例绑定必须重查。不要local Python/otool/strings byte-hunt替代IDA；既存.ida-mcp captures可先读。若你的客户端没有这些工具，先如实说明缺少什么，不假装已连接。

当前聊天本地ATIF：/Users/ciscohe/.local/share/devin/cli/transcripts/coffee-soap.json。
前序历史：/Users/ciscohe/.local/share/devin/cli/summaries/history_83dd6af563c64fa6.md。
按需本地读，raw chat含凭据/截断输出，勿公开或commit。证据优先于旧聊天断言。

中文回复，所有根因明确分RE-confirmed/runtime-confirmed/THEORY，不把source comment当runtime证据，不把uptime当输出。先给出你核对的当前状态和3-6步执行计划，然后从失败回归测试继续；不要重新从头推导整个项目。
```

# macOS 15.6 实验遗留物用途目录（2026-10-04）

这份目录回答一个实际维护问题：工作区里和设备上留下的文件，最初为什么存在、记录了什么、是否仍有价值，以及清理或恢复 macOS 15.6 时应如何处理。它是在 macOS 15.6 分支冻结后建立的，不把“未跟踪”误判成“无用”。

## 结论和边界

`docs/porting/HANDOVER-DEVIN-TO-EXTERNAL-GPT-2026-10-02.md` 明确要求保留当时的 `.ida-mcp/`、`.qoder/` 和 `misc/` 探针源/二进制/对象；因此本次只登记，不删除这些本地遗留物，也不把它们自动加入 Git。它们包含离线复核材料、设备实验的可执行版本和不能从提交历史完整恢复的现场状态。

本目录中的“可恢复”表示未来回到 24G90 时可用于复核已经得到的边界；不表示可以直接部署或运行。所有会改变 vnode 签名状态、shared-region/VM 状态、进程代码签名标志或内核数据的探针都标为“禁止直接运行”。15.6 已冻结，恢复工作必须重新做身份、输入、回滚和时限核验。

## 本地未跟踪物

| 路径或族 | 当初用途 | 现有证据/关联 | 目前处置 |
|---|---|---|---|
| `.ida-mcp/dyld-cache-format13-proof.txt`、`kernel-dyld-pager-format-proof.txt` | IDA Pro MCP 对 dyld/cache/pager 格式的离线反汇编和结构证明 | `docs/evidence/m1-dyld-pager-format13-20260930.md`；交接文档要求保留 | 保留作 RE 复核材料；不当作新的设备运行见证 |
| `.ida-mcp/dyld-prepare-m3-write-proof.txt`、`enter_helper.json` | IDA 记录的 M3/内核进入点、指令窗口和 helper 反编译结果 | 交接文档及 `docs/porting/dyld-15.6.1-state.md` 的 kernel/pager 条目 | 保留；不能据此直接写 kernel/PAC |
| `.ida-mcp/vcs.json` | IDA MCP 本地会话/版本控制元数据 | 本地工具状态 | 保留本机，不提交；不含项目运行时契约 |
| `.qoder/settings.local.json` | 本地工具设置 | 无项目源码引用 | 保留本机，不提交；清理时不触碰 |
| `docs/evidence/mac15-device-delete-results-20261004.jsonl` | 548 个明确 15.6 文件的删除结果、3 个跳过项和 0 errors 的逐项记录 | 对应 `mac15-device-delete-manifest-20261004.tsv` | 应提交；它是清理证据，不是设备脚本 |
| `misc/addfilesigs.c` | 调用 `F_ADDFILESIGS_RETURN/INFO`，验证 DSC 内嵌 superblob 是否能挂到 vnode | 源文件头部说明；与 cache code-signing 边界有关 | 保留源码；禁止直接运行，因会改变 vnode 签名相关状态 |
| `misc/cache_exec_probe.c`、`misc/exec_fault_test.c` | 让 cache 文件页经历 RX 映射、数据读和指令取，区分 page validation/exec fault 路径 | 各源文件头部；15.6 state 的 KERN_CODESIGN/KERN_MEMORY 分界 | 保留源码；禁止在冻结设备上直接跳转执行 cache 页 |
| `misc/cache_page_probe.c` | 原生 iOS task 对指定 cache offset 做 `pread` 与普通 `mmap` 的只读对照 | `docs/porting/dyld-15.6.1-state.md` 的 page probe 条目 | 保留源码和历史构建物；不作为 macOS exec task 等价证明 |
| `misc/cache_page_copy.c` | 非固定 VA、独立 iOS task 中测试 `VM_PROT_COPY`/私有 COW；明确不等价于 macOS shared region | `docs/evidence/m1-dyld-pager-format13-20260930.md` | 保留源码；诊断用途，禁止借它宣称 CLI 修复 |
| `misc/cache_page_fixed.c` | raw SVC 的固定地址 cache page 映射；可选 prefix/stop/read，探测 4 GB 窗口和 pager 行为 | `docs/porting/dyld-15.6.1-state.md` | 保留源码；禁止直接运行，除非未来有单变量、限时、独立回滚方案 |
| `misc/hold.c`、`hold.o`、`hold` | freestanding 无限等待 child，避免半填充 shared region 期间再调用 dyld/libc | 源文件头部；历史 runner 实验 | 保留源码/构建物供复核；禁止启动，避免遗留进程 |
| `misc/mpriv.c`、`mpriv`、`mpriv_dyn`、`mpriv_e`、`mregions`、`mregions2` | macOS dyld `_start` 阶段扫描低地址映射、Mach-O header 和可读页，区分真实内容与空洞 | 源文件头部；清理 manifest/top inventory 中有记录 | 保留，主要是 15.6 历史；不在 Ventura acceptance 中运行 |
| `misc/otest.c`、`otest` | raw SVC 的 open/openat/fstatat 路径对照，核对 `/tmp/dsc`、绝对路径和 cryptex 解析 | `docs/porting/CONSOLIDATED-2026-09-29.md` | 保留作路径语义参考；不需要重跑 |
| `misc/smap.c`、`smap` | 复刻 dyld 的 syscall 536 文件映射形状，含 dynamic region 匿名 entry | 源文件头部；`CONSOLIDATED-2026-09-29.md` | 保留源码；禁止直接运行，syscall 536 会改变当前 task VM 状态 |
| `misc/sr536.c`、`sr536` | 直接调用 syscall 536，按完整/截断/noexec/slide 变体定位映射拒绝 | 源文件头部；15.6 state 和删除 manifest | 保留作离线/未来单变量工具；禁止无新计划部署 |
| `misc/srteardown.c` | 测试 `shared_region_check_np(NULL)` 后 permanent submap 是否可被 FIXED mmap 覆盖 | 源文件头部；15.6 state 已记录其输出边界 | 保留源码；禁止直接运行，涉及 shared-region VM 状态 |
| `misc/run_dbg`、`misc/run_nocskill` | 受控 spawn/exception runner；前者带 CS_DEBUGGED/exception 观察，后者用 KRW 清 `CS_HARD|CS_KILL` | `run_dbg.c`/`run_nocskill.c` 及 15.6 state | 保留构建物供 hash/历史复核；不得运行 `run_nocskill`，不得把 runner 当修复 |
| `misc/excsnap`、`misc/tstate` | exception/thread/VM region 快照和 PC/SP/FP/LR 观察 | `excsnap`/`tstate` 的对应源码和 state 条目 | 保留；只读诊断也必须重新核对设备身份和时限 |
| `misc/scheck.c`、`scheck`、根目录 `scheck.c`/`scheck` | 直接调用 syscall 294 `shared_region_check_np`，报告 region 空/已填/不存在 | `docs/porting/CONSOLIDATED-2026-09-29.md`；交接文档明确保留 | 保留作历史对照；不因返回值而执行 teardown |
| `misc/sprobe_wk` | 旧版 shared-region/pager 诊断构建物；源码不在当前工作区 | 文件类型、历史设备 inventory；无可维护源码 | 保留但标为不可重建；不得部署 |
| `misc/tstate` | 已编译的 `tstate.c` thread/VM 只读诊断 | `misc/tstate.c` | 保留本地；未来需重新编译/签名，不把旧二进制视为可信 |
| `misc/triagescan.py` | 通过 Dopamine kread 读取 kdebug triage 记录，定位 fault/VM 事件 | 源文件头部；15.6 state 的 triage 条目 | 保留源码；只读不等于无需身份核验，当前冻结不运行 |
| `misc/vadiff.c` | `task_for_pid` + `mach_vm_read_overwrite` 将 child VA 与 cache 文件范围逐字节对照 | 源文件头部 | 保留源码；只读诊断，不能越过设备串行和进程清理契约 |
| `misc/vnwatch.py` | 用 KRW 读取 cache vnode/UBC resident count，同时运行 bounded echo child | 源文件头部 | 保留源码；冻结期间不运行，避免同时触发 chroot/VM 观测和遗留 child |

编译产物（例如 `run_dbg`、`sr536`、`mpriv*`、`scheck`）没有独立的知识含量；知识在对应源码、日志和 dated state 条目中。它们保留是为了核对历史 CDHash/复现实验形状，不能绕过当前 pipeline 直接 `scp` 到设备。

## 设备上本轮仍看到的族

重启后只读 inventory（2026-10-04）显示 `/var/mobile` 和 `/var/mnt/rootfs/private/tmp` 仍有大量历史探针、日志、cache 候选和脚本；这与“本轮删除 548 个明确 manifest 项”并不矛盾，说明 manifest 是有界删除而非全目录清空。`/var/jb/usr/macOS`、rootfs 本体、原始 dyld/cache 仍属于保留区。

设备上文件应按下面四类处理：

1. **15.6-only**：`dyld_*` 候选、`cache*`/`cachereg*`、`p536*`、`sr*`、`k*` KRW/owner probe、`echo_probe*`、`post_reboot_*`、`f1*`、`m*`/`n*`/`t*` 实验输出，以及对应 runner/pid/raw/out/log。它们的用途已经在 15.6 state 或本目录的本地工具表中记录，复核完 manifest 后才可删除。
2. **Ventura/项目共享**：`/var/jb/usr/macOS` 公共 runtime、正式 launch jobs、autosignd 和 rootfs 本体；不能按名字模糊删除。
3. **系统文件**：例如 `/tmp/com.apple.trustd`、`/var/tmp/com.apple.trustd`；即使名字出现在匹配结果中也必须保留。
4. **未知/用户文件**：不因“看起来像 probe”删除；先登记路径、类型、大小、inode、mtime 和证据引用。

因此，后续设备清理必须先把非递归顶层 JSONL 拉回本地，按这四类 review，再生成新的逐项删除 manifest。不能用递归 `du`、`find` 或宽泛正则直接删除；也不要把公共目录 `/var/jb/usr/macOS` 当成 15.6 staging。

本轮第二次 review 已将明确的 15.6 目录记录并删除：`f1_leftovers_20261001`（31 个文件）、`libsys_cache_extracts`（43 个文件）、`bak_shim`/`bak_shim2`（6 个文件），以及 rootfs private tmp 下 17 个 `mr*`/`mrt*` 树（51 个文件）。逐目录清单和 inode 校验结果在 `docs/evidence/mac15-device-cleanup-dir-manifest-20261004.jsonl` 与 `mac15-device-cleanup-dir-results-20261004.jsonl`；四个残留旧目录 `hook_disabled_20261001`、`rst`、`shim_bak2`、`x` 也已按 inode 记录后删除。

清理后有意保留：`/var/mobile/dscq`（约 387 个文件、226 MB 的未知 iOS `usr/lib` staging）和 `/var/mobile/macws-runtime-stage`（当前 MacWS/GPU staging）；`/var/jb/tmp` 的 GPU/OSLog 文件没有按名称猜测删除。最终非递归证据为 `docs/evidence/mac15-device-top-inventory-final-20261004.jsonl`。用途登记完成后才删除，避免丢失实验 know-how。

## 恢复 15.6 时的最小入口

先读 `HANDOVER-MACOS15-FROZEN-2026-10-04.md`、`T8103-PMAP-NESTED-OWNER-FIX-PROPOSAL.md` 和 `dyld-15.6.1-state.md` 的最新条目；重新验证设备 build、dyld SHA、cache 两个完整 CDHash 和 trust 状态。任何 binary RE 仍走 IDA Pro MCP；不能用这些旧 helper 代替 IDA，也不能运行 `fmt13_patch.py` 或任何未重新验签的 kernel candidate。

第一里程碑仍是原版 `/bin/echo HI` 经真实 24G90 dyld、系统库和 shared-cache 打印 `HI`。历史“进程活着”“trust helper 返回 0”“普通 mmap 成功”都不是该里程碑。

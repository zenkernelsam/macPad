# HANDOVER REPLY — 2026-09-28：macOS 缓存 536 EINVAL（populate）调查回复

> 对应任务书：`docs/porting/HANDOVER-DYLD-536-POPULATE-2026-09-28.md`
> 状态：**进行中（先被设备状态挡住）**。本文按任务书要求记录：进度、证据、下一步、负结果。
> 每次更新请在此追加"更新"小节并 git commit。

---

## 00. 当前状态（最新在最上）

### 更新 1 — 设备处于 exec-veto cascade，实验无法开跑（阻塞点）
**结论：本 boot 的 chroot'd macOS exec 路径被系统性 veto，需重启 + 重新越狱激活后才能继续。**
未自行重启（按任务书 §5/用户指示），已报告用户等待处理。

**证据（本回合实测，设备 `up 6:00`，未重启）**
| 探针 | 结果 |
|---|---|
| `chroot /var/mnt/rootfs /bin/echo PROBE`（带/不带 `DYLD_SHARED_CACHE_DIR`） | `rc=137`、**零输出** |
| `chroot … /tmp/sprobe`（**静态 freestanding，不经 dyld**） | `rc=137` ⇒ veto 与 dyld/符号无关 |
| `launchdchrootexec 0 0 $R /bin/echo LCE_OK` | 内层 `rc=137`（仅打印 launcher 行） |
| `run_bash.sh -c "echo RB_OK"` | `Killed: 9` |
| `bash /var/mobile/restore_env.sh` | 跑完但 spawn 自检 `proc not found for pid … — killing`；之后探针仍 137 |

**同时确认「不是」什么**（避免误判为通用 CS 崩坏）
- iOS 侧 exec 全正常：`/var/jb/usr/bin/echo`、`zsh`、`jbctl`、`ldid`、`python3` ✅
- 进程健康：`amfid`(370)、`launchd`(1) 均在 ✅
- trustcache 正常：`Jailbreak Trustcache 0` **200 条**；我们部署的 dyld cdhash **hit=3**；`/var/jb/usr/bin/zsh` CDHash **hit=1** ✅
- 日志中 `zsh/watch.so (code signature invalid)` 属 zsh 可选模块的既有小问题，**不是** veto 的成因（zsh 主体可跑）

**需要的动作（仅用户可做）**：**重启 iPad 并在 Dopamine 里重新激活越狱**（必要时再 `Refresh Trustcache`）。
> 为什么不能从 SSH 自救：exec-veto 是内核/AMFI 在 **exec 准入** 上的 per-boot 状态，`restore_env.sh`
> 只能补 TC；补完 TC 仍 137 说明卡在准入而非 TC。

**重启后我方将立即执行的既定流程（已就绪，无需再决策）**
1. `bash /var/mobile/post_reboot_final.sh`（或 `restore_env.sh`）恢复 TC
2. cachereg 后台挂 blob（**fd 保持打开**，两片缓存一起）：
   `cachereg <cryptex dyld>/dyld_shared_cache_arm64e <…>.01 &`
3. **判决实验（任务书 §3.5，性价比最高）**：部署 `/var/mobile/dyld_sf0e.bin`
   （`crossarch+slide0+e5entry+e5cave`；files[0].sf_slide=0 ⇒ 关掉随机 slide），三连跑：
   `cd /var/mnt/rootfs && timeout 15 env -i PATH=/usr/bin:/bin \
     DYLD_SHARED_CACHE_DIR=/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld \
     /var/jb/usr/bin/chroot . /bin/echo HELLO`
   - **看 stderr 前 8B（little-endian）= errno**：`22→0`（或进到 fsignatures/下一阶段错误）= §3.5 随机 slide 理论成立 ⇒ 直接产出最小修复。
   - 仍 22 ⇒ 按任务书 §4.1/§4.2 转向内核侧：读 `sub_8459570`(setup) 填的 56B rec `+16` 语义，
     或枚举 `sub_8017E5C`(file enter) 的 return 站点对照我们的 `(submap,&off0,size=0x67f5c000,0,0,flags,pager)`。

---

## 01. 已确认的环境事实（本回合复核）
- 三台 IDA MCP 均健康：**Instance1**=dyld(`dyld_15.6.1_arm64e_thin`，imagebase 0)、
  **Instance2**=kernel(`kc_raw_16.3_T8112`，imagebase `0xfffffe0007004000`)、**Instance3**=amfid(`amfid_bin`)。
- 设备文件齐备：`/var/mobile/dyld_sf0e.bin`(1239648)、`dyld_plat.bin`(1239648)、`cachereg`(68816)、`restore_env.sh`。
- 当前部署的 `/var/mnt/rootfs/usr/lib/dyld` 与 `dyld_plat.bin` 同尺寸（1239648）。

## 02. 负结果（已排除，供后续直接引用）
（本回合全部为设备状态类，见 00 节表格；技术性负结果待实验开跑后追加。）

## 03. 待办清单（重启后按序）
- [ ] 恢复 TC（post_reboot_final.sh）
- [ ] cachereg 挂 blob（保持 fd 打开）
- [ ] **§3.5 判决实验：dyld_sf0e.bin 三连跑 + 记录 errno 前 8B**
- [ ] 若 22→0：写最小修复 + 验收（`/bin/echo HELLO` 不再报 map failed）
- [ ] 若仍 22：内核侧按 §4.1（setup 56B rec+16 语义）/§4.2（file enter 站点枚举）继续

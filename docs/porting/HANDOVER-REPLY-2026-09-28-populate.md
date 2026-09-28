# HANDOVER REPLY — 2026-09-28：macOS 缓存 536 EINVAL（populate）调查回复

> 对应任务书：`docs/porting/HANDOVER-DYLD-536-POPULATE-2026-09-28.md`
> 状态：**进行中（先被设备状态挡住）**。本文按任务书要求记录：进度、证据、下一步、负结果。
> 每次更新请在此追加"更新"小节并 git commit。

---

# 00. 当前状态（最新在最上）

### 更新 3 — 【根因钉死·无需设备】随机化唯一开关 = `files[0].sf_slide`；`slide0` 就是最小修复
**包装层 `sub_FFFFFE0008459134`（Hex-Rays，实测反编译）：**
```c
v11 = v6[2];                        // v6 = copyin 的 files[] 12B/rec；v6[2] = files[0].sf_slide
v34 = 0;
if ( v11 ) {                        // ★★ 只有 sf_slide != 0 才做随机化（有 if 保护，不会除零）★★
    <取 rand u32 到 v34>
    v13 = (v34 % v11) & 0xFFFFC000; // = rand32() % sf_slide，再 16K 对齐
} else {
    v13 = 0;                        // ★ sf_slide == 0 ⇒ slide 恒为 0（无随机化）
}
// 随后对每条 file rec： v18[1] = v13;                  （rec+8 = slide）
// 对每条 sms： *((_QWORD*)v22 - 4) += v13;             （sms+0 = va）
//             if ( *((_QWORD*)v22 - 1) ) *(_QWORD*)v22 += v13;（sms+24 = slide，非零才加）
// 末尾： v33 = sub_FFFFFE0008061EF0(populate)； if (v33 > 3) return 22;
```
**结论（三重证据：反汇编 + Hex-Rays + 与 §3.5 的数学预期一致）**：
1. **随机化唯一由 `files[0].sf_slide` 控制**；`=0` ⇒ `v13=0` ⇒ 所有 VA/slide 保持原值 ⇒ §更新 2 的 C/D 溢出/回绕校验**永不命中** ⇒ `populate` 不再返回 4 ⇒ **EINVAL(22) → 0**。
2. `dword_FFFFFE000A9FCA58`（属 `vm.shared_region_*` sysctl 表）**只是日志门**（用于 `if (!err || trace<1) && !err` 这类判空），**不是** slide 开关；`vm.shared_region_pivot` 亦无关。
3. ⇒ **最小修复 = dyld 侧 `slide0`**：把 `files[0].sf_slide` 写 0（patch 点 `0x3552c LDR W9,[X19,#0x1780]`，即 `build_dyld.py` 的 `slide0` key）。**已备好 `dyld_sf0e.bin`（crossarch+slide0+e5entry+e5cave）**，只等设备可用即可三连跑验收。

### 更新 2 — 【里程碑·无需设备】EINVAL 的确切内核站点已定位（证据完备）
**目标问题的答案（第一半）：`sub_FFFFFE00080623D4`（per-record enter worker）有 6 条 `return 4` 路径，
全部是「VA/size 页对齐与溢出」+「slide 代际一致性」校验；`return 4` ⇒ populate>3 ⇒ 包装层一律 EINVAL(22)。**

| # | 站点（IDB VA） | 条件（反编译+反汇编双证） |
|---|---|---|
| A | `0xFFFFFE00080625A0-A8` | `W8=event+4`（region 已种入的 slide）与 `W9=rec+16` 比较：`CCMP W9,W8,#4,NE` ⇒ 不相等则 `B loc_2C08`；`event+4==0` 时由首条非零 `rec+16` 种入（`STR W8,[X27,#4]`） |
| B | `0xFFFFFE00080625D0-D8` | 仅当 `event[115]` 或 `vm.shared_region_trace_level==14`：`LDRH W8,[X23,#0x10]; TST W8,#0x3FFF; B.NE loc_2C08`（rec+16 低 14 位必须为 0） |
| C | `0xFFFFFE0008062698-9C` | `LDR X9,[X21,#8]; ADDS X9,X8,X9; B.CS loc_2C08` ⇒ **VA+size 加法溢出** |
| D | `0xFFFFFE00080626B8-D4` | `page-1 + (VA+size)` 按 16K 向下取整（`AND X10,X11,X10`），`CMP X10,X8; B.CC loc_2C08` ⇒ **取整后 < VA（回绕）** |
| E | `0xFFFFFE0008062C08` | `MOV W19,#4` → `BL sub_FFFFFE0008061C40` → `return v22(=4)` ⇒ **populate 返回 4** |

**与任务书 §3.5（随机 slide 理论）严格吻合**：包装层 `slide = rand32() % files[0].sf_slide & ~0x3FFF`
被加到每条 sms 的 VA(+0) 与 slide(+24)；sf_slide=0x20000000 ⇒ X∈[0,512MB) 随机，
main 尾 VA=0x22560C000、region 顶 0x280000000 ⇒ **X>0x5AA34000（≈55%）时 C/D 直接命中 ⇒ return 4 ⇒ EINVAL**，
且越界点在 populate 中途 ⇒ 部分条目先进去再 rollback ⇒ 解释"时好时坏"。
**⇒ 预期最小修复（待设备验证）**：dyld 侧 `slide0`（`files[0].sf_slide=0`，内核跳过随机化）——即已备好的 `dyld_sf0e.bin`。
> 注：sf_slide=0 时 A 走 `event+4==0` 分支（通过，无需种入）；B 仅在 trace_level==14 或 event[115] 时生效，正常不触发。

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

### 更新 4 — 判决产物已字节级校验（文件读取，不需要 exec，可在 veto 状态下做）
对 `/var/mobile/dyld_sf0e.bin` 逐字节核对（`dd bs=1 skip=<十进制> | od`，注意 dd 不认十六进制）：
| 偏移 | 语义 | dyld_plat.bin | **dyld_sf0e.bin** | 现部署 dyld |
|---|---|---|---|---|
| 0x76270 (483952) | crossarch（svc→mov x0,xzr） | `aa1f03e0` | **`aa1f03e0` ✓** | `aa1f03e0` |
| 0x3552c (218412) | `files[0].sf_slide=0`（MOV W9,#0） | `b9578269`（原指令） | **`52800029` ✓✓** | `b9578269` |
| 0x38d08 (232712) | e5 cave 首词 | `d503201f` | **`d10083ff` ✓**（`sub sp,sp,#0x60`，真代码） | `d503201f` |
| 0x76e04 区 | e5entry 分支 | 无分支 | **多出一条 `b`（失败路径跳 cave）✓** | 无 |
结论：**`dyld_sf0e.bin` = crossarch + slide0 + errno 探针 cave，构建正确**；cave 内容 = `sub sp,sp,#0x60; str x0,[sp]; mov x1,sp; mov x2,#8; mov x0,#2; mov x16,#4; svc; ...`，
即 **write(2, &x0, 8) 后 exit** ⇒ **stderr 前 8 字节（little-endian）= 536 的原始返回值**，与任务书"errno 字节探针"一致。
另：当前部署的 `/var/mnt/rootfs/usr/lib/dyld` md5 = `b9509df1feb5…` = **dyld_plat.bin**（未打 slide0）。

### 更新 5 — 【实验设计修正 + 一键脚本】构建 `dyld_sf0plat.bin`（隔离随机 slide 这一变量）

**发现的设计隐患**：任务书 §3.5 的 `dyld_sf0e.bin` 在 `0x35c24` 是**未打 `plataccept`**（`540001c1` = 原 `B.NE`），
而 `dyld_plat.bin` 已打（`d503201f` = NOP）—— 而 `plataccept` 正是此前"平台预检"的挡路点。
若平台预检先命中，`sf0e` 的判决会**不确定**。

**→ 已构建隔离版产物 `dyld_sf0plat.bin` = crossarch + plataccept + slide0 + e5探针**（其余取自 dyld_plat）：

| 偏移 | 含义 | dyld_plat | dyld_sf0e | dyld_sf0plat |
|---|---|---|---|---|
| 0x76270 | crossarch | aa1f03e0 | aa1f03e0 | **aa1f03e0 OK** |
| 0x35c24 | plataccept(NOP) | d503201f | 540001c1 (缺) | **d503201f OK** |
| 0x3552c | slide0(MOV W9,#0) | b9578269 | 52800029 | **52800029 OK** |
| 0x76e04 | e5entry（原 pacibsp d503237f 被 b 0x38d08 取代） | d503237f | 17ff07c1 | **17ff07c1 OK** |
| 0x38d08 | e5cave（write(2,&x0,8); exit） | d503201f | d10083ff | **d10083ff OK** |

- 设备侧已上传：`/var/mobile/dyld_sf0plat.bin`（md5 `6c7769b41183…`，本地==设备 OK）。
- **一键脚本**：`/var/mobile/run_sf0plat.sh`（本地与设备 bash -n 均 OK）：
  健康/veto 检测(必要时 restore_env.sh) → cachereg 挂两片 blob(fd 保持) → 部署 dyld_sf0plat.bin(备份当前到 dyld_before.bin)
  → **三连跑**并打印 err 前 8B → 结论速读（`01 00…`=过线；`16 00…`=仍 22）。
- **重启/恢复越狱后只需一条命令**：`bash /var/mobile/run_sf0plat.sh`。

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

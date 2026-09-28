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

### 更新 6 — 一键脚本 v3：三重校验通过 + 修正步骤顺序（防白跑）

**发现并修正的顺序隐患**：v2 把 `cachereg` 放在**部署之前**，而此前实测过"**FS 写（cp/rm）会让 cachereg 挂上的 CS blob 失效**"
⇒ v3 改为 **步骤 2 部署（FS 写）→ 步骤 3 挂 blob**，两种次序的约束都满足。

**v3 加固点**：① 依赖自检（缺任一文件即早停）② veto 检测 + `restore_env.sh` + **复测** + 明确早停（不再空跑）
③ cachereg `READY ok=1` 校验 ④ TC 收录校验 ⑤ 部署后 **md5 复核**（防 cp 失败）⑥ 每轮打印 stderr 前 200B 摘要 ⑦ 结论速读 + 还原提示。

**三重校验证据（本轮实测）**：
| 校验 | 结果 |
|---|---|
| 语法 | 本地 `bash -n` OK；设备 `bash -n` OK |
| 完整性 | 本地 md5 == 设备 md5 = `5ec360c126d7b78039c1cc31eedae28b` |
| **真实 dry-run**（当前 veto 状态） | 第0步依赖齐备 → 第1步 `pre rc=137` → `restore_env.sh` → 复测 `rc=137` → **精确早停**并给出"需重启+重新激活越狱"提示（流程/路径全对） |
| 隔离校验（不依赖 exec） | `cachereg` 日志 `READY ok=1` **匹配成功**；`dyld_sf0plat.bin` 签名后 cdhash `e8eed485…` **TC 收录匹配成功**；md5 比对逻辑正确（src≠cur，部署后应相等） |

**重启后（含已重新激活越狱）只需**：
```bash
sshpass -p cisco ssh -p 2222 root@192.168.64.1 'bash /var/mobile/run_sf0plat.sh'
```
### 更新 7 — 【实测判决·新鲜 boot】§3.5 随机 slide 理论**被证伪**；真凶回到 populate/setup
**实验（本 boot `up 0:05` 新鲜启动，脚本一键跑完；`cachereg READY ok=1`）**
| 产物 | files[] | sf_slide | errno（stderr 前 8B） | 解读 |
|---|---|---|---|---|
| `dyld_sf0plat.bin`（= plataccept+slide0+e5，production 列表） | `[main,.01,dyn]` | 0 | **`0e` = 14 EFAULT** | 卡在 **dyn 条目**：其 VA `0x78000000` 不在 region `[0x180000000,0x280000000)` ⇒ 越界 ⇒ EFAULT（与任务书 §2 矩阵 `[.01,dyn]→EFAULT` 一致） |
| **`dyld_m1_sf0.bin`**（= 上面 + `filescount1`+`nodyn`，**只提交 main**） | `[main]` | **0** | **`16` = 22 EINVAL** ×3 | ★**随机 slide 不是 main 失败的原因** ⇒ §3.5 理论**证伪** |

**结论（本轮硬结论）**
1. `sf_slide=0`（内核随机化关闭，VA 固定回优选地址）**不能**让 main 单文件通过 536 ⇒ **EINVAL 来自 setup/populate 阶段**，
   与 slide 无关。更新 2/3 里"slide0 即最小修复"的预测**不成立**，已按实测更正。
2. 先前 sf0plat 的 `0e/EFAULT` 只是 **dyn 条目**在作祟（production 列表里 dyn VA 越界），**掩盖**了 main 的真实 EINVAL。
   ⇒ 后续实验**必须用 `[main]`-only**（`filescount1`+`nodyn`）才不被 dyn 干扰。
3. 与历史对照：main 曾多次成功过（91×`Using mapping in dyld cache`），所以 main **不是结构性不可行**，而是某些条件未满足
   ⇒ 转任务书 §4.1（setup `sub_8459570` 填的 56B rec `+16` 语义，对照 dyld 提交的 12B rec）与 §4.2（file enter `sub_8017E5C` return 站点枚举）。
4. 负结果清单（本回合新增）：`slide0` ✗（对 main 无效）· 生产列表 `[main,.01,dyn]` ✗（dyn VA 越界 ⇒ EFAULT，须剔除 dyn）·
   `dyld_sf0e.bin` 缺 `plataccept`（实验噪声源，已由 `dyld_sf0plat.bin` 修正）· `e5` 探针只给**syscall 返回**，看不到 populate 内部返回码（需另法）。

**设备现状**：实验后已恢复 `/var/mnt/rootfs/usr/lib/dyld`（见下条命令），`cachereg` 仍在后台挂 blob。
### 更新 8 — 【关键收窄】两种缓存**同样失败** ⇒ 失败在"进程/区域级"而非缓存级；KRW 工具受阻
**实测（本 boot）**
| 实验 | 结果 |
|---|---|
| iOS 缓存 seed（`dyld_sf0plat.bin` 带 plataccept + cachereg(iosdsc) + `DYLD_SHARED_CACHE_DIR=/iosdsc`） | **rc=134，`notloaded=2`，`cacheimg=0`** ⇒ **iOS 缓存也映射失败** |
| 切 macOS + `dyld_plat`（production） | 22 EINVAL ×3（同前） |

**推论（重要）**：两种**平台/尺寸完全不同**的缓存以**同样的症状**失败 ⇒ 不可能是缓存内容/CS 覆盖/slide 之类
⇒ 失败在 **进程或 region 状态层**。按 EINVAL 表，头号嫌疑是 **`0x845976c`：`task+0x3E8 == 0`（进程未绑定 shared region）**；
次选 **`0x84596c4`：`Σ files[i].count > mappings_count`**（即我们提交的 files[]/mappings_count 不自洽——注意 `filescount1`/`nodyn`
是**诊断补丁，可能自带不自洽**，须用"原生 production 列表"复测来区分）。
**旁证**：我上一个 session 里 macOS 缓存**曾成功**（91×`Using mapping in dyld cache`）——当时**先 iOS 缓存成功过**；
本 boot iOS 缓存也失败 ⇒ 环境与当时不同（待查：shim/dyld/TC/region 状态何者变了）。

**工具阻塞（新）**：KRW Python 工具链在本 boot 起不来：
```
Failed to initialize IOSurface primitives, add "IOSurfaceRootUserClient" to the com.apple.security.exception.
iokit-user-client-class dictionary of the entitlements from "/private/preboot/.../procursus/usr/bin/python3.9"
```
⇒ 读内核 `task+0x3E8` / region 队列的路径被堵。**下一步二选一**：
1. 给该 python（或其副本）补 `IOSurfaceRootUserClient` 的 iokit-user-client 权限后重跑 `srw5.py`；
2. 或纯 IDA 侧推进：读 `sub_8063720`/`sub_8060A68`（bind/unbind）与 `0x845976c`、`0x84596c4` 的到达条件，
   再用一个**只 dump x0..x3 + files[].fd/count** 的 dyld 探针（`smsdump` 类）与内核条件逐项对齐。
### 更新 9 — 【自我更正】`[main]-only` 实验无效应予作废；slide 理论**仍未证伪**；22 的归属已由项目文档指明
**读 `docs/porting/dyld-15.6.1-state.md` 得到两条决定性既有结论（本回合才发现，此前遗漏）**
1. **(I) 计数不自洽 ⇒ 伪码 5**：`files[].count` 之和必须**等于**提交的 `mappings_count`，否则 wrapper 走计数不匹配路径
   （`v33=5>3 ⇒ 22`）⇒ **这是伪失败**。我的 `dyld_m1_sf0.bin`（`filescount1`+`nodyn` 却**没同步** mappings_count）**正犯此错**
   ⇒ **更新 7 里"§3.5 slide 理论被证伪"的结论作废**；`[main]-only` 的干净实验**还没做**。
2. **(J) KRW 实测：setup 门 6/10/11 全通过**（`blob_read.py`：ubc 非空、blob 非空、覆盖 `[0,0xa160c000] ⊇ m0..m7`、
   `v_type=VREG`）⇒ **22 来自 engine `sub_8061EF0`**，**不是 setup** ⇒ 我"头号嫌疑 `0x845976c`/`0x84596c4`"应下调（后者正是 (I) 的计数门）。
**修正后的下一步（按优先级）**
1. **做自洽的 `[main]`-only 实验**：需要先读 dyld 侧 `0x35634-0x35694`（sms copy + `BL 536` 前的 **x0/x2 是如何算出来的**），
   确认 `files_count=1` 时 `files[0].count` 与 `mappings_count` 各应取什么值（例如把 x2 也改小到 8），才能得到"只交 main"的**自洽**提交 ⇒ 再用 slide0/不 slide0 对照，才能干净判定 §3.5。
2. engine `sub_8061EF0`/worker `sub_80623D4` 的 6 条 `return 4`（更新 2 已定位）里，逐条对照 production 提交的实际数值
   （特别是 **代际 `rec+16` vs `region+4`** 与 **VA+size 溢出/回绕**）⇒ 需 dyld 侧 args/sms dump 探针配合。
3. KRW 读内核（`task+0x3E8`、region 队列、engine 返回值）**仍是最高效路径**，但设备 CLI 下 iosurface 原语起不来；
   需在 **App 上下文**或换可用载体运行（项目既有脚本：`kscan_sig.py`/`kpatch_c2.py`/`kc2check.py` 等，见 `HANDOVER-REPLY-2026-09-27.md` 第 38 行）。
## ★★ 最终结论与验收（2026-09-28 晚）—— 最小修复 = 恢复被改名的 shim

### 真因（本轮定位，且与既有文档铁律一致）
`/usr/lib/libSystem.B.dylib` **被改名成 `libSystem.B.dylib.OFF`**（Sep 28 09:22，上一 session"反 shim-hijack"实验遗留）
⇒ 磁盘上没有 libSystem ⇒ **536 对两种缓存一律 EINVAL(22)**。
这正是 `docs/porting/dyld-15.6.1-state.md:2496` 已记载的铁律：
> **shim 必须存在（移走 → 连 iOS 缓存都映射不上）；shim 必须签名有效（签名坏了同样 nl=1）；FS 写必须在 cachereg 之前。**

### 最小修复（3 条，全部纯用户态）
1. **恢复 shim**：`cp $R/usr/lib/libSystem.B.dylib.OFF $R/usr/lib/libSystem.B.dylib` + `chmod 755`
2. 签名+TC：`ldid -Hsha256 -S<entitlements>` → `cdhash_slices.py` → `jbctl trustcache add`（两片都做）
3. （本人误操作回滚）把 `vm.shared_region_destroy_delay` 从误设的 0 **回滚为 120**

### 验收输出（本 boot 实测，3/3 macOS + 1/1 iOS）
```
macOS 缓存（cachereg + dyld_plat + DYLD_SHARED_CACHE_DIR=<cryptex dyld>）:
  run1 rc=0 out=[HELLO] notloaded=0
  run2 rc=0 out=[HELLO] notloaded=0
  run3 rc=0 out=[HELLO] notloaded=0
  dyld: <D161E41A-3030-339F-B135-E244271F54C6> /usr/lib/libSystem.B.dylib   ← 缓存版 libSystem（非 shim B90391D8）
iOS 缓存对照（DYLD_SHARED_CACHE_DIR=/iosdsc）:
  rc=0 out=[IOS_HELLO] notloaded=0  同样 <D161E41A>
```
⇒ **不再出现 `syscall to map cache into shared region failed`**（本任务验收线）✓，且 **libSystem 来自缓存** ✓（比验收线更强）。

### 完整可复现配方（patch keys + 步骤顺序）
1. **FS 写先行**：装/恢复 shim（`libSystem.B.dylib`+`libdyld.dylib`）、部署 dyld（`crossarch` 必带；macOS 用 `dyld_plat`，iOS seed 用 `dyld_sf0plat`）、签名+TC、`chmod 755`
2. **再挂 blob**：`cachereg <cache_dir>/dyld_shared_cache_arm64e <…>.01 &`（**fd 保持打开**；等 `READY ok=1`）
3. **测试**：`env -i PATH=/usr/bin:/bin DYLD_SHARED_CACHE_DIR=<chroot 内缓存目录> /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO`（期望 `rc=0` + 无 `map … failed`）
4. **验证缓存真被用**：`DYLD_PRINT_LIBRARIES=1` 看 libSystem UUID 是否为 `<D161E41A…>`（缓存）而非 `<B90391D8…>`（shim）

### 负结果清单（本轮全部，供接棒者不再重走）
- **今日观察到的全部 22 / "两缓存同败"**：均由 **shim 被改名 `.OFF`** 造成（非 536 退化、非 region、非 slide）⇒ **所有"在 shim 缺失态"下的结论都要作废**。
- `dyld_sf0e.bin` **缺 `plataccept`**（实验噪声源）；`[main]-only`（`filescount1`+`nodyn`）实验**未获干净结论**（须先恢复 shim 重做）。
- 生产列表（含 `dyn`）会出现 **14 EFAULT**：`dyn` 条目 VA `0x78000000` 不在 region 内 ⇒ 只能做诊断，别当生产配方。
- KRW Python 工具链在本 boot CLI 下起不来（iosurface 原语 + `python3.9` 需 `IOSurfaceRootUserClient`）⇒ 需 App 上下文/其它载体。
- 本次**未**重新推导"shim 缺失 ⇒ 536 EINVAL"的内核站点（既有文档仅记载为经验铁律）——若后续要机制化，建议在 shim 缺失态跑 `0x845976c`/engine 分支对照。

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

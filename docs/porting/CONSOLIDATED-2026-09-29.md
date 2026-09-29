# CONSOLIDATED STATE — 2026-09-29（全文档归一版）

> **这是当前唯一权威的整合视图。** 三天里 22 份文档、多次上下文丢失造成了大量
> 相互矛盾的旧结论。本文把**已证实 / 已否证 / 当前卡点**分栏整理，旧的按时间序
> 记录仍在 `dyld-15.6.1-state.md`（2571 行编年史），索引见文末。
> 每条结论标注证据级别：runtime-confirmed / RE-confirmed / THEORY / DISPROVEN。

---

## 0. 北极星与当前位置

**目标**：在 iPad13,11（M1/T8103，iPadOS 16.3 / 20D47，xnu-8792.82.2，Dopamine
rootless）上，chroot 进 `/var/mnt/rootfs`（macOS 15.6.1 rootfs），跑通 macOS
WindowServer + GUI 应用。兜底版本 = macOS 13.4（原作者验证过）。

**两条并行的 CLI 路线**（这是最容易混淆的点）：

| 路线 | 状态 | 说明 |
|---|---|---|
| **shim 路线**（磁盘 libSystem 手写桩 + msh） | ✅ **CLI milestone 已达成 09-28** | `echo/cat/sh(msh)/date` 已跑通；ls 缺 libutil/libncurses 桩 |
| **缓存路线**（536 灌真 macOS DSC，真 libSystem 从缓存来） | ⚠️ **536 曾被打通（rc=0）**，但消费缓存页时被内核 CS 击杀 | 是当前调查主线 |

**长距离 GUI 阻塞**（独立战场，见 AGENTS.md 结构表）：AGX UC-init `0xe00002c2`、
compositor 缺 queue、framebuffer 全零。CLI 不通则 GUI 无从谈起。

---

## 1. 环境事实（runtime-confirmed，可直接引用）

- 设备：iPad13,11 M1；iPadOS 16.3 (20D47)；Dopamine rootless，前缀 `/var/jb`。
- chroot 根：`/var/mnt/rootfs`；macOS 缓存在 chroot 内路径
  `/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e{,.01}`。
- dyld 解释器路径：`/var/mnt/rootfs/usr/lib/dyld`（曾被未知进程覆盖过一次——部署后要验 md5）。
- **KASLR slide 每次重启会变**，运行时地址 = IDB EA + slide；用 `kfind_slide.py`/
  `libjailbreak slide` 现取，别复用旧值（旧记录 0x241c4000 / 0x1eebc000 / 0x1a129000 各属不同 boot）。
- **IDA 实例（09-29 重启后已重载，地址规则变了！）**：
  - Instance1 = dyld 15.6.1 arm64e thin，imagebase 0，偏移即 EA。
  - Instance2 = kernelcache（kc_raw，文件名说 T8112 实为 T8103），
    imagebase = `0xfffffe0007004000`；**旧文档里 `0x8xxxxxx` 风格的偏移映射为
    `EA = 0xfffffe0000000000 + old_offset`**（例：sub_8017E5C → 0xfffffe0008017e5c）。
    主内核 `com.apple.kernel:__text` = `0xfffffe0007f1c000..0x868c000`。
  - Instance3 = amfid。
- **xnu 源码在仓内**：`analysis/xnu-xnu-8792.81.2/`（注意内核实际是 8792.82.2，源码版本略旧但结构吻合）。

---

## 2. 签名 / exec 准入（已定论，不再推导）

runtime-confirmed 铁律：

1. **cdhash = `sha256(CodeDirectory[0:CD.length])[:20]`**（只哈希 CD 本体）；
   `misc/cdhash_slices.py` 对每个 slice 都算。
2. 改过的 dyld 签名配方：`ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist F`
   **绝不加 `-Cadhoc`**（改过的文件带 flags=0x2 → SIGKILL）。设备端 ldid。
3. 部署必须 `rm -f` 再 `cp`（新 inode）+ `chmod 755`（缺 +x → EACCES 静默）。
4. `jbctl trustcache add <40hex>`；`trustcache info` 输出大写 hex，grep 加 `-i`。
5. 每 slice 都要算 cdhash；`fat_arm64ify.py`/`arm64ify_macho.py`（设备版）转 fat 后再签。
   thin arm64e 直接部署会被 SIGKILL（subtype 0x80000002）。
6. 连续多次 CS-invalid exec 触发 **exec-veto cascade**：连已知好的二进制也 rc=137。
   恢复 = `bash /var/mobile/restore_env.sh` 或重启+重新越狱激活。
7. **FS 写必须在 cachereg 挂 blob 之前**（写文件会使已挂 blob 失效）。

---

## 3. syscall 536 完整函数地图（RE-confirmed + xnu 对照）

```
sysent[536] @ sysent+536*24 (sysent=0xfffffe0007999680)
  → sub_FFFFFE0008459134  _shared_region_map_and_slide_2_np wrapper
      copyin files[] 12B/rec {fd, count, sf_slide} + mappings[] 48B/rec
      slide = sf_slide ? (rand32 % sf_slide) & ~0x3FFF : 0
      每条 mapping: va += slide; slide_start(ptr) += slide
      → sub_FFFFFE0008459570  shared_region_map_and_slide_setup
          → sub_FFFFFE0008063590  vm_shared_region_trim_and_get(task)
              → sub_FFFFFE00080608E8  读 task+0x3E8 = task->shared_region
          逐文件校验：fd解析 / FREAD / VREG / mount(根卷或Cryptexes) /
                      file_check_mmap(MAC聚合 sub_867A738) / uid==0 /
                      逐 mapping: !(init_prot&0x10) → ubc_cs_blob_get 覆盖检查
          返回 errno（0/1/12/22/透传）
      → sub_FFFFFE0008061EF0  vm_shared_region_map_file（engine）
          → sub_FFFFFE00080623D4  per-mapping worker
              init_prot & 0x10 → 匿名 enter（sub_FFFFFE0008019768）
              否则 → file-backed enter：
              sub_FFFFFE0008017E5C = vm_map_enter_mem_object_helper
      engine kr 映射：{0,0x17→0} 放行；kr>3 → EINVAL(22)
      → sub_8459D90  cleanup
```

### 已知 errno 出口表（RE-confirmed 站点）

**setup（sub_8459570）**：mappings 溢出(0x84596c4) / region==NULL(0x8459780) /
rootdir 不匹配(0x8459764→EPERM) / fd==-1&count≥2(0x8459d74) / 未页对齐(0x8459d04) /
非 FREAD(EPERM) / 非 VREG(0x8459d50) / file_check_mmap 透传(0x8459a08) /
uid≠0(EPERM) / 卷不匹配(EPERM) / ubc_getobject==NULL(0x8459ce0) /
**CS 覆盖不足(0x8459cbc)**。

**engine（sub_8061EF0）**：`sr_first_mapping != -1`（已填充→KERN_FAILURE→22）。

**worker→enter（sub_8017E5C，6 个 EINVAL 站点）**：0x8017eb4 / 0x8017ed4 /
0x8017ef4（参数校验）/ 0x8018474（`*(X16+0x20) < foff+size`）/ 0x8018ae4 /
0x8019354。**◀ 哪个站点命中尚未钉死——用 §7 的 msgbuf 方法直接读内核 printf。**

### shared_region 结构（xnu + KRW 验证）

```
vm_shared_region: +0x00 ref_count / +0x04 slide / +0x08 queue / +0x18 sr_root_dir
                  +0x30 sr_first_mapping(-1=空) / +0x71 mapping_in_progress / +0x76 stale
task+0x3E8 = shared_region（RE-confirmed via sub_80608E8）
kind→几何：a4=1&kind=0x100000C → base 0x180000000, size 4GB 写死
            （⚠️ macOS 缓存总跨度 ~4.77GB > 4GB：main 放得下，.01 尾部越界结构性）
```

### check_np（294）语义

非零指针 = 查询：ret 0=已填充 / **12=region 存在但空** / 22=无 region。
**NULL 指针 = 删除 region**（危险，实验别用）。

---

## 4. 536 真相时间线（每天结论对照，含作废标记）

| 日期 | 结论 | 现状 |
|---|---|---|
| 09-26 | EINVAL；探针 SIGILL=cave 字节序 bug（已修 `_le()`）；exec EACCES=缺 +x | ✅ 仍有效 |
| 09-27 早 | 「slide-info v5 vs 内核 v1-4」为真因 | ❌ **DISPROVEN**（见 §5） |
| 09-27 晚 | 首次 536=0（noslide 变体）；post-reuse SEGV；iOS 缓存可被采纳 | ✅ 536 确实能过 |
| 09-28 | **真配方：`sf_slide` 处理是缓存特异的**——iOS `0x539b0000` 非 16K 对齐必须清零（dyld_sf0）；macOS `0x20000000` 合法必须保留（dyld_plat 即可） | ✅ 当前有效 |
| 09-28 | `set_blob_cov.py` 扩 csb_end_offset 反而是 macOS 缓存失败诱因——**别扩覆盖**，只跑 cachereg 挂 blob | ✅ |
| 09-28 | 清 mapping slide（`clrslide`/`nsl2` 类）= **自伤元凶**（挂死/被击杀） | ✅ |
| 09-28 | 两缓存均可 536=0、`using=91`；之后命中 CS「Invalid Page」击杀（fault 在**非 536 映射**的 624K r-x 文件上——越狱乐注入 dylib 嫌疑） | ✅ 当前真实卡点 |
| 09-28 | CS 覆盖门 0x8459cbc 曾被认为是 EINVAL 首因 | ⚠️ 部分作废——`init_prot&0x10` 旁路后 EFAULT 说明该门存在但非唯一失败点 |
| 09-29 凌晨 | 「chroot 进程没有 shared region（check_np=22）」| ⚠️ **部分作废**——今天干净 boot 实测 check_np=**12**（region 存在且为空）；之前的 22 读数可能来自污染态/check_np(NULL) 副作用 |
| 09-29 凌晨 | 「region 里是 iOS 缓存」 | ⚠️ 勘误——更可能是 macOS 缓存；libutil 证据不足 |
| 09-29 凌晨 | dyld 不从缓存「绑定」libSystem：cat/ls/sh 仍 rc=134（`___error`/libutil），符号解析走 shim | ✅ 当前卡点之一 |

---

## 5. ❌ 已否证理论清单（别再查）

1. **「slide-info v5 被 iOS 16.3 内核拒」**——macOS 缓存 `slideInfoVersion=0`（无旧式
   slide-info）；09-29 实测清掉所有 `VM_PROT_SLIDE` 位后 536 **仍 EINVAL**；
   且 dyld_plat（不动 slide）本就成功过。`parse_dsc_slideinfo.py` 报的 "v5" 读的是
   mappingWithSlide 内嵌 blob，与内核 sanity 路径无关/或解析错位。**该理论作废。**
2. 「无 shared region → EINVAL」——干净 boot check_np=12 排除。
3. 「region 已填充（pop30≠-1）→EINVAL」——空 region 下同样 EINVAL。
4. 「sf_slide 随机化越界 55%」——slide0 下仍 EINVAL（对 macOS 缓存本就应保留）。
5. 「blob 覆盖不足」——KRW 重演 blob [0,0xa160c000) 全覆盖，cs_blobs 非空。
6. 「e0 单条是凶手」——单条 e1/跳过 e0/字段修改 全部失败或挂死，与 entry 内容无关。
7. 「mount/vtype/uid/MAC file_check_mmap」——setup 全门 KRW 重演通过。
8. marker 探针（mk*/m2*）不可靠——SIGNALED 11 / 零 marker。
9. `noslide`@0x35eec 补丁无效——只去掉「附加 OR」，记录自带 0x20 位时不生效。
10. zf0 实验无效——写错 bit（0x400 而非 ZF=0x10）。
11. 给 DSC 文件本身改字节 → 页哈希不匹配 → SIGKILL。文件在 blob 挂载期间不可改。

---

## 6. 已证实的关键行为规则（state-management，违反=污染实验）

1. **shared region 按 (rootdir,cpu) 去重且持久**；chroot 下所有进程共享同一 region。
   首次成功映射后 `check_np=0`；换缓存/换配方需要干净 region（重启最稳）。
   映射中途被杀可自动释放（09-29 实测 check_np 回落 12），但**别赌**——实验设计
   假设 region 是一次性消耗品。
2. **⚠️ 任何在目标映射前跑的不带 `DYLD_SHARED_CACHE_DIR` 的 chroot 命令都会先行
   绑定/污染 region**（restore_env 内部自检、基线 echo 都算）。干净实验 = 重启后
   第一条 chroot 命令就是目标测试。
3. **`check_np(NULL)` 会删除 region**——探测永远传非零地址。
4. **fd 泄漏即 blob 失效**：cachereg 退出 → vnode cs_blob 丢 → 536 CS 门必挂。
   cachereg 必须常驻（后台 &）。
5. 单条 mapping[0]（fd 真实）→ EFAULT(14)；8 条全量 → EINVAL(22)；fd=-1 匿名 → 0。
   即：**匿名路径干净，file-backed enter 失败，且 EINVAL 与 EFAULT 是两个独立失败面**。
6. `sub_8017E5C`（file enter）只在**子映射（submap）**语境下失败：同文件用户态
   `mmap(fd,PROT_READ,MAP_PRIVATE)` 成功 ⇒ 文件/pager/object 本身健康。

---

## 7. 新取证能力（09-29 新增——本会话产出）

### 内核 msgbuf 可读（拿 SHARED_REGION_TRACE_ERROR 的真实 kr）

`shared_region_trace_level` 默认=1（ERROR 级 printf 常开），populate 失败会打
`"shared_region: mapping[%d]: address:... size:... offset:... maxprot:.. prot:.. failed 0x%x"`
——**直接给出失败条目序号和真实 kern_return_t**。

读取途径（RE-confirmed 自 `sysctl_kern_msgbuf` = sub_FFFFFE00083E4A38）：

```
msgbufp   = *(0xfffffe000aa030a0 + slide)          // → msgbuf struct
msg_size  = *(msgbufp + 4)
rptr      = *(msgbufp + 8)
wptr      = *(msgbufp + 0xc)
buf_ptr   = *(msgbufp + 0x10)                      // 环形缓冲数据地址
```

用 libjailbreak KRW 读 buf 扫 `"shared_region"` 即可。**这是结束 EINVAL 猜测链的
正确工具**——之前 `log show`/`dmesg` 在本环境不可用。

### 其他 09-29 实测

- host 重启（外因，无新 panic）后 IDA 实例需重载；kernel IDB 基址规则见 §1。
- 空 region（check_np=12, base=0）+ `clrslide`+`slide0` 仍 EINVAL——但**该 patch 集
  本身被污染**（含 hasexisting/prereuse 诊断补丁 + 对 macOS 有害的 slide0/clrslide），
  不能据此推新结论。**有效对照必须是 `crossarch+plataccept`（dyld_plat）± 单变量。**
- 探针在 0x35380（mapSplit 入口）反复触发几十次 + 进程 rc=124 挂死：疑似 cave 或
  dyld 高层 retry——出现此现象先核对 cave 回跳落点（tickonly 曾因 `17fff255` 落到
  0x35688 而非 0x35694 造成 300 万次假 retry）。

---

## 8. 生产配方（runtime-confirmed，照抄）

### 8.1 dyld 构建（`analysis/dyldwork/build_dyld.py`）

| 用途 | patch keys |
|---|---|
| macOS 缓存生产 | `crossarch plataccept`（= `dyld_plat`） |
| iOS 缓存生产 | `crossarch plataccept` + sf_slide 清零 cave（= `dyld_sf0`） |
| 诊断（536 前后观察） | 生产集 + `rethdr`/`ck2d` 类 cave；**勿混 hasexisting/prereuse 进生产** |
| ⚠️ 有害勿用 | `clrslide`/`nsl*`（清 mapping slide = 自伤元凶）、`slide0`（对 macOS 缓存）、`set_blob_cov.py`（扩覆盖） |

### 8.2 设备端部署链（重启后按序）

```sh
export PATH="/var/jb/usr/sbin:/var/jb/usr/bin:/var/jb/sbin:/var/jb/bin:/usr/sbin:/usr/bin:/sbin:/bin:/var/mobile:$PATH"
bash /var/mobile/restore_env.sh          # 补 jailbreak trustcache（内存态，重启必做）
# FS 写 → 签名 → TC（在 cachereg 之前）：
#   arm64ify/fat → ldid -Hsha256 -S<ent>（无 -Cadhoc）→ cdhash_slices.py → jbctl trustcache add ×每slice
#   rm -f /var/mnt/rootfs/usr/lib/dyld && cp → chmod 755
# 挂 blob（常驻）：
/var/mobile/cachereg <cache dir>/dyld_shared_cache_arm64e <…>.01 &   # 等 READY ok=1
# ⚠️ 不要跑 set_blob_cov.py（天然覆盖是对的，扩覆盖反而坏）
# 第一条 chroot 命令必须是目标实验（§6.2）：
env -i PATH=/usr/bin:/bin \
  DYLD_SHARED_CACHE_DIR=/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld \
  /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO
```

验收：`rc=0` + `HELLO` + `Using mapping in dyld cache`×~91 + `notloaded=0` +
libSystem UUID=`D161E41A`（缓存版，非 shim `B90391D8`）。

### 8.3 shim 路线部署（CLI milestone 配方）

见 `CLI-MILESTONE-2026-09-28.md` 全文：build_shim.sh → libSystem.B.dylib/msh/dash
部署 + `ln -sfn /bin/dash private/var/select/sh`。**shim 必须存在且签名有效**——
移走它连 iOS 缓存都映射不上（铁律）。

---

## 9. 当前真实卡点（按距离终点排序）

1. **536 的 EINVAL 精确定位**——msgbuf 法（§7）可一针见血。注意：dyld_plat 配方
   下 536 已被证明能成功（09-28 rc=0×3），所以要先在干净 boot 复现成功基线，
   再研究失败态差异。
2. **缓存页消费的 CS 击杀**（09-28 定性）：`vm_fault.c:2863` 无条件 printf 带文件名，
   ktriageinfo 说 "memory corruption in executable text"；fault 区域是**非 536 途径**
   映射的 624K r-x 文件（越狱注入 iOS dylib 嫌疑最大，也可能 `launchdchrootexec`
   注入的 `libmachook`）。⇒ **第一批嫌疑：DYLD_INSERT_LIBRARIES 链上的 iOS 构建物
   在 macOS 进程里 plain-mmap**。纯 chroot（无注入）+macOS 缓存是干净对照。
3. **libSystem 绑定**：536=0 之后 dyld 仍把 libSystem 解析到磁盘 shim（`___error`/
   `libutil` 缺失 → cat/ls/sh 134）。候选：cache 内 image 索引/平台标记/
   `ProcessConfig` 的 `isProtectedLibSystemPath` 门（静态已定位 gate1=DyldCache+0xA8、
   gate2=Security+0x1A，见 STATIC-libsystem-cache-vs-shim.md）。
4. **4GB region 墙**：macOS 缓存 4.77GB 总跨度 > 4GB region——main 分片可容纳
   （含 libSystem），`.01` 尾部越界是结构性的；届时再说（CLI 只需 main）。

---

## 10. 设备操作陷阱（血泪汇总）

- `chdir: No such file or directory` 前缀属 launchdchrootexec 环境怪癖，无害。
- `zsh/watch` 模块签名报错无害；但 `log show` 在该 shell 下不可用。
- python3 设备端 `-c`/stdin 模式会 segfault（checkin 限制）——**只用文件脚本**。
- `sysctl` 路径 `/var/jb/usr/sbin/sysctl`；`vm.shared_region_pivot` 写 EPERM。
- KRW 只读安全；**写内核 text 会挂死（PPL）**；PAC 指针字段写 → panic。
- panic 留档：`/private/var/mobile/Library/Logs/CrashReporter/Panics/`；
  09-28 01:04 的 `pmap_mark_page_as_ppl_page_internal` 是已知旧 panic；
  09-29 的重启无新 panic（外因）。
- scratch 缓存 `/var/mnt/rootfs/macdsc/` 已被多次原地改坏——勿用；
  干净源 = 主机 `~/Desktop/dyld-cache-15.6.1/dyld_shared_cache_arm64e`。

---

## 11. 文档索引（每份的角色与新旧）

| 文件 | 角色 | 状态 |
|---|---|---|
| `CONSOLIDATED-2026-09-29.md` | **本文——整合视图，先读** | 当前 |
| `dyld-15.6.1-state.md` | 2571 行编年史（每日 milestone 追加式） | 当前活文档（顶部摘要仍是权威，但时间序里埋着作废结论——以本文为准） |
| `CLI-MILESTONE-2026-09-28.md` | shim 路线 CLI 配方与验收 | 当前有效 |
| `HANDOVER-REPLY-2026-09-28-populate.md` | 09-28 收官：536 打通+CS 击杀定性 | 当前有效（但开头"随机 slide 是根因"小节已被同文件后段自我推翻） |
| `HANDOVER-DYLD-536-POPULATE-2026-09-28.md` | populate 任务书 | 历史（任务已完成） |
| `kernel-syscall536-re-handover.md` | sysent/setup/engine 全分析+errno 站点表 | 当前有效（EA 需按 §1 新基址换算） |
| `kernel-syscall536-finding.md` | 「536=40 → Sandbox file_check_mmap」 | ⚠️ 历史——40 是旧配置实测；当前恒 22 |
| `syscall536-errno-and-probe-sigill.md` | errno 探针+字节序 bug | 当前有效 |
| `HANDOVER-15.6.1-2026-09-26.md` | 首份 handover（exec 门+536 初步） | 历史（部分结论已被覆盖） |
| `HANDOVER-HELLO-2026-09-27.md` + REPLY-09-27-HELLO | hello-world SEGV 狩猎 | 历史（已完成，墙已移到 CS 击杀） |
| `HANDOVER-DYLD-ADMIT-2026-09-27.md` + REPLY-09-27(-shim) | dyld 准入破解+iOS 缓存采纳 | 历史有效（配方以本文 §8 为准） |
| `dyld-15.6.1-deep-re-handover.md` + `dyld-15.6.1-full-analysis.md` | dyld 静态分析任务书+报告 | 参考 |
| `STATIC-libsystem-cache-vs-shim.md` | cache-vs-shim 决策门（ProcessConfig 门链） | 当前有效（卡点 #3 的基础） |
| `re-analysis-15.6.1.md` / `hit-rate-table.md` / `patch-ledger.tsv` | 15.6.1 移植探针与台账 | 当前有效（GUI 移植线） |
| `PORT-TO-MACOS15-HANDOVER.md` / `AGENT-START-PROMPT.md` | 移植施工图/启动 prompt | 参考 |
| `rootfs-15.6.1-install.md` | rootfs 安装 runbook | 参考 |
| `TOOLS-AND-PORTING.md` / `runtime-switches.tsv` | 工具箱清单 | 当前有效 |
| `HANDOVER-REPLY-2026-09-27.md` / `HANDOVER-REPLY-2026-09-28-536.md` | 各阶段回复 | 历史（结论已并入本文） |

---

## 12. 下一步（建议顺序）

1. **重启后干净 boot，直接跑 `post_reboot_macfirst.sh`（dyld_plat + macOS 缓存
   作为第一条 chroot 命令）**——复现 09-28 的 536=0 基线。若复现不了，才轮到
   msgbuf 取 kr。
2. 若 EINVAL 复现：用 §7 msgbuf 读 `mapping[%d] failed 0x%x` → 钉死站点。
3. 536 成功后主攻 **CS 击杀**：先纯 chroot（无 libmachook 注入）对照——判断
   fault 的 624K r-x 文件是否注入链产物。
4. libSystem 绑定问题沿 `ProcessConfig+0x208/0x12A` 门链走（STATIC 文档已铺好）。

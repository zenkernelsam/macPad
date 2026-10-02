# macOS 15.6.1 dyld shared-cache bring-up — live state

> **2026-09-30 最新勘误（优先于旧摘要）**：设备缓存旧式 slide 头为零，
> 但 `mappingWithSlide` 表的 m1..m5 均有有效 v5 blob；不能据旧头宣称“无 slide-info”。
> IDA 实证：私有缓存 page-in linking 提交 pointer format 13，而 iPadOS 16.3
> dyld pager 分派范围仅 1..12，13 落失败路径。**兼容性缺口已确认，
> 本次 m3 fault 是否实际走此路径仍待运行时证明**；不恢复旧 syscall 536 归因。
> 旧 `CacheFinder+0xb9f8` 定位没有捕获加载基址证据，暂时撤回，也不据此推断异步异常。
> 完整证据、已备取证工具和下一关见
> `docs/evidence/m1-dyld-pager-format13-20260930.md`；
> 设备原文见 `docs/evidence/m1-cache-slide-metadata-20260930.txt`。
>
> **2026-09-30（静态侧）根因闭合 —— 以
> `docs/porting/STATIC-m3-dyld-pager-format13.md` 为准：**
> ① 交接文档的"ring 里没有 printf ⇒ 格式分派未走到"**前提错误**：release 内核里
> `printf` 被 `CONFIG_NO_PRINTF_STRINGS` 宏化、格式串被 DCE（`misc_protos.h:180-186`），
> 真机内核字节级 0 命中；这些消息改走 **kdebug triage**
> （`kdebug_triage.c:327-329`，eventid `0x04000008` = DYLD_PAGER_SLIDE_ERROR）。
> ② 真机 `dyld_pager_data_request`（I1 `sub_FFFFFE00080661A4`）里 `format-1<=0xB`
> 才进 switch，**13 落到汇聚点 → ktriage + KERN_FAILURE(5)**，无 printf ⇒
> data_request=5 ⇒ `vm_fault.c:1892` ⇒ `vm_fault.c:5693` ⇒ `KERN_MEMORY_ERROR=10`，
> 与实测 `code0=0xa` 吻合（**不再是 THEORY**）。
> ③ **根因在内核 550 漏检**：真机 `map_with_linking_np`（I1 `sub_FFFFFE000845A084`）
> 校验 version/page_size/binds/chains/CS 覆盖，**唯独不校验 `mwli_pointer_format`**
> ⇒ 550 对不可 fixup 的 blob 返回成功、失败推迟到每页首 fault 且静默。
> ④ 修复靶点：dyld 侧 `0x34790 B.NE loc_34C00 → B loc_34C00`（走 dyld 自身
> in-process 回退，低风险）；根因修复在内核 550 加格式白名单。
> ⑤ **新确定性判据**：crash report 的 `TASK_CRASHINFO_KERNEL_TRIAGE_INFO_V1`
> 会带 triage 字符串（`kern_exit.c:770-774`）——查
> `"dyld_pager_data_request hit a page sliding error"` 即可裁决，无需 kdebug/msgbuf。
>
> **2026-09-30（goal 第 1 轮）CLI 主障碍已定量 —— 见
> `docs/porting/STATIC-cache-layout-exceeds-4gb-shared-region.md`：**
> ① 15.6.1 缓存总布局 `0x180000000..0x2ac75c000`（≈4.77GB）**超出 iOS 16.3 共享区
> `0x180000000..0x280000000`（4GB）≈745MB**；主缓存最高端 `0x22560c000`，
> `.01` 从 `0x22560c000` 续到 **`0x2ac75c000`**（= 当前 `EXC_GUARD(DEALLOC_GAP)` 的 gap 地址）。
> ② 出错指令是 **dyld 自己的手写 `mmap` 桩**（`x16=197`，字节序列在 rootfs 中**仅**
> `/usr/lib/dyld` 出现，pc 恒为 `dyld_base+0xae8`）；`vm_map.c:8693-8702` 的
> `VMDS_FOUND_GAP` → `vm_map_guard_exception(GAP)` 即该 reason 的另一产生点。
> ③ **负结果**：藏 `.01` 文件无效（布局由缓存头元数据决定）⇒ "只用主缓存"必须**重建缓存**。
> ④ 本轮修好：`$R/dev` 缺失 → `mkdir -p` + `mountdevfs` → **`/dev/ptmx` 出现**；
> 补挂 bash/sh/cat/ls/date/libmachook_arm64 的 TC；cachereg 增覆盖 cryptex inode。
> ⑤ `launchdchrootexec` **不做** `jbctl proc_set_debugged` ⇒ 作者 `run_bash.sh` 在我们 fork 上
> 必 `Killed: 9`（可修，需 Theos 构建）；实验一律用 `/var/mobile/run_dbg_hold_v2`。
>
> **2026-09-30（静态侧）设备实测更正（详见 STATIC 文档 §9）：**
> ① 上条的 `.ips` triage 判据**在本机不成立**：全量 grep 40+ 份 echo/chroot 报告，
> 无 `dyld_pager`/`triage`/`1ee188000` 字段（triage 只在 sysdiagnose logarchive 里）。
> ② `<D161E41A>` **不是**"用 macOS 缓存"的判据：未 chroot 的 iOS 进程
> （`/var/jb/usr/bin/ls`）同样打印它。旧判据作废。
> ③ 裸 `chroot` ⇒ `Killed: 9`；必须用 `run_dbg_hold_v2`（内含 `jbctl proc_set_debugged`）。
> 设备上 `run_nocskill` 已过期（`proc not found`），`restore_env.sh` 的 HELLO×3 全部失败。
> ④ **F1 实测有效方向**：打补丁后（signed SHA `14e2751b…`，CDHash `cd023af6…`，
> `/var/mobile/dyld_f1_34790.bin` 可复用）跑
> `run_dbg_hold_v2 chroot /var/mnt/rootfs /bin/echo HI`：**m3 写错误签名
> （type=1 code0=0xa far=0x1ee188000）不再出现**；新失败点为 dyld 内原生
> `mov x16,#0xc5; svc #0x80`（mmap）触发 **EXC_GUARD(type=12)**，
> `code1=0x2ac75c000`（>4GB 高地址区）、`csflags=0x26803b0d`。
> `HI` 仍未打印。设备 dyld 已恢复原版（SHA/inode 双复核）。

> **📣 2026-09-29 起：先读 `docs/porting/CONSOLIDATED-2026-09-29.md`**
> （全文档归一版——已证实/已否证/当前卡点分栏）。本文件仍是按时间序的
> 编年史；**凡与本文件旧段落冲突，以 CONSOLIDATED 为准**。
> 09-29 重要更正：① kernel IDB（Instance2）重启后基址规则 =
> `EA = 0xfffffe0000000000 + 旧0x8xxxxxx偏移`；② 「slide-info v5」理论已否证；
> ③ 内核 msgbuf 可经 KRW 直读（msgbufp @ 0xfffffe000aa030a0），能拿到
> `mapping[%d] failed 0x%x` 真实 kr——详见 CONSOLIDATED §7。

**READ THIS FIRST after context loss.** Active task: get dyld to map the
macOS 15.6.1 shared cache on iPadOS 16.3 (xnu-8792.82.2) so macOS binaries
run in chroot. This file is the single source of truth — update it whenever
a fact/offset/result changes, BEFORE context is lost.

**▶ 完整移交文档（给下一位 agent 的自包含复现+继续指南）：
`docs/porting/HANDOVER-HELLO-2026-09-27.md` + `HANDOVER-DYLD-ADMIT-2026-09-27.md`**

---
## 📌 顶部摘要（2026-09-28 最新；下方旧段落如与之冲突，以本节为准）

### 1) 签名/准入（已定论，不再推导）
- 内核 exec 准入查的 cdhash = **`sha256(CodeDirectory[0:CD.length])[:20]`**（只哈希 CD 本体；工具：`misc/cdhash_slices.py`，对每个 slice 都算）。
- 配方：`ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist F`（**不加 `-Cadhoc`/`-M`**）→ `jbctl trustcache add <40hex>` → `rm -f` 后 `cp`（新 inode）+ `chmod 755`。
- 判据：`re-clears=2` ⇒ 准入过；`re-clears=1` ⇒ 被杀（确定性 8/8）。`SIGNALED 9` 先查 cdhash 是否按 `CD[0:cdlen]` 算对并进 TC。

### 2) 【已达成】536 把 iOS dyld 缓存映射进 chroot shared region
- **真因 = `files[].sf_slide` 非 16K 对齐**（`shared_file_np` = 12B `{fd,count,sf_slide}`；iOS 主分片 `sf_slide=0x539b0000`）⇒ 清零后 **536 成功**。
- 产物 `analysis/dyldwork/dyld_sf0.bin`（`crossarch+plataccept` + cave@0x35690→0x9b578 清零 `sf_slide`）。
- 验收 3×：`rc=0 HELLO`、`Using mapping in dyld cache ×91`、`re-using existing shared cache ×2`、`cache not loaded ×0`、`check_np base=0x180000000`。
- 前置：① `misc/restore_env.sh` 补 TC；② `cachereg_ios /iosdsc/*`（46 片）附 CS blob；③ `set_blob_cov.py` 把 46 片 `csb_end_offset`=文件大小。

### 3) 当前卡点：libSystem 的平台/兼容
- `/bin/echo` 过（只需 `_err`）；`/bin/cat`/`/bin/sh` 失败于 `___error` Expected in **shim 的 UUID(B90391D8)** ⇒ **libSystem 实际仍取磁盘 shim**。
- 移走 shim → `wrong platform to load into process`；`platstub`（`loadableIntoProcess→1`）强载 iOS 库 → **SIGILL(132)**。
- ⇒ “库全走（iOS）缓存”受限于 **iOS 库 ≠ macOS 进程**；**结论：真正需要的是 macOS 自己的缓存**。

### 4) 下一步（需重启拿干净 region）
- **实测：shared region 跨进程持久**（`check_np` 连续两次 = `0 / base=0x180000000`）；所以要换缓存必须**重启**（bindfs 新建 root 尝试失败：`mount_bindfs: No such file or directory`）。
- **实测：macOS 缓存 `slideInfoVersion=0 / slideInfoOffset=0`（根本没有 slide-info）** ⇒ 旧“slide-info v5”障碍**对本缓存不适用**。
- 重启后跑设备上 `/var/mobile/post_reboot_mac.sh`（自动：校验干净 region → 复原 TC → cachereg macOS 缓存 → blob 覆盖 → 部署 `dyld_noslide.bin` → 验收 HELLO/using/notloaded + cat/ls/sh）。

### 5) 重启后必重建（易失）
- **jailbreak trustcache（内存）** → 用 `misc/restore_env.sh`；**cachereg 的 cs_blob（vnode 级）** → 重跑 `cachereg*`；rootfs 本身不丢。
- ⚠️ 旧 `run_nocskill` 硬编码 kernel slide → 重启后 `proc not found`；**实测 TC 复原后直接 `chroot` 即可**（不需要 run_nocskill）。

### 6) 已作废/易误解的旧说法
- ❌“签名配方不对导致 SIGNALED 9” → 真变量是 cdhash 算法（§1）。
- ❌“22 = CS 覆盖门（0x8459cbc）是首因” → 对 iOS 缓存，**首因是 `sf_slide`**；CS 覆盖门只在特定配置下暴露（覆盖面需 ⊇ mapping 区间）。
- ❌“macOS 缓存卡在 slide-info v5” → 本缓存无 slide-info（§4）。
- ❌ marker 站点（mk*/m2*）不可靠（`SIGNALED 11`、零 marker）→ 改用 `cknp2`/自建 cave 探针。

---

## ★★★★★ 2026-09-28（三·晚）⭐⭐⭐⭐⭐ MILESTONE：**setup 全部 EFAULT 站点实测排除；file-backed 在所有 offset 都挂；嫌疑收敛到 `vm_map_enter_mem_object` 的 copy_strategically 层**

> ⚠️ **对上一节的两条修正**：
> 1. `zf0` 实验 **无效**：patch 写的是 `0x407`（bit10=0x400）不是 `VM_PROT_ZF=0x10`——匿名路径从未在同址测过。"同址匿名也失败⇒地址侧问题"结论**作废**。
> 2. "file@非零 offset 成功（139 SEGV）"是 cave 未执行假象：VA[0]=0x1c0000000 / +0x4000 / +0x40000000 三个偏移**全部 EFAULT/134**——file-backed 与地址无关。
> 3. `dynamicDataOffset` 原值 = `0x12c75c000`（header+0x1F0），写 0 是修改不是还原。

### 错误矩阵（runtime-confirmed, e5errno 读原始值）
| files[] | 结果 |
|---|---|
| {fd=-1} 匿名 dynamic 区 | **536=0 成功** |
| {fd=macOS 缓存} mapping[0] 单条 | **EFAULT(0x0e)**（e5 字节 `0e` 两次复现）|
| {fd=macOS 缓存} 全 8 条 | **EINVAL(0x16)** |
| m[0] 挪 offset 0x4000 / 0x40000000 / initProt+0x10 | **EFAULT/134**（与地址无关）|

### setup（`sub_FFFFFE0008459570`）全部 EFAULT(1) 站点——已逐一**实测排除**
| 站点 | 实测 |
|---|---|
| `sr+0x18 rootdir != a8` | fc0 同区同任务通过 ⇒ 相等 |
| `fileglob+0x10 & 1` | 实测 `0x100000001` bit 已置 ✓ |
| `va_uid != 0` | 文件 root:wheel uid=0 ✓ |
| `vnode+0xD8(mount) != rootdir+0xD8` → `/private/preboot/Cryptexes` 子树比较 | mac 文件/根目录/macdsc 全部同 mount `0xfffffe22a96d4340` ⇒ 首检过 |
⇒ **EFAULT 在 worker 层**（`sub_80623D4` → `sub_8017E5C`/`sub_8019768`），不在 setup。
EINVAL(22) 全 8 条是**另一独立失败**：最可疑为逐映射 `ubc_cs_blob_get` 覆盖检查（rec3 flg=0x44/rec5=0x5/rec6=0x20 有 AUTH/CRYPTO 标志位）。

### worker 层结构（源码 vm_shared_region.c:1726+ + IDA 对齐）
- `init_prot & 0x10(ZF)` → `map_port=NULL` → `vm_object_allocate`+`vm_map_enter`（**匿名，成功**）
- 否则 → `map_port=file_object->pager` → **`vm_map_enter_mem_object`**（=内核 `sub_8017E5C`，**失败**）
- 两边都以 `vmkf_already=TRUE | VM_FLAGS_FIXED | copy=TRUE` 进入。
- **file-only 步骤**：`memory_object_to_vm_object` → `pager_ready` wait → `memory_object_map(pager,prot)` → **`vm_object_copy_strategically`(copy=TRUE)** → `vm_map_enter`。匿名路径全跳过。
- `sub_8017E5C` 自身只返回 4/17/29；`sub_8019768` 唯一字面 `return 1` 是 bounds（实测 min=0/max=0x100000000 通过）→ 1 来自更深的子调用（`801CE34` 尾保护同步/`801FC88`/copy 链）。
- **旁证**：用户态 `mmap(fd, PROT_READ, MAP_PRIVATE)` 对同一文件 0x67f5c000 **成功** ⇒ 文件/object/pager 健康，问题**子映射特异**。

### 新确认结构偏移（runtime-verified）
`fileglob+0x10` 低位 bit0 必须置位；`vnode+0xD8` = mount 归属（同 fs 同值）；`vnode+0x54` v_flag bit9=0x200 = `VSHARED_DYLD`（`vnode_isdyldsharedcache`=该位；cryptex 真缓存**已自带**，iOS 副本无）；`ubc+0x20` ui_size、`+0x28` ui_flags、`+0x50` cs_blobs；`ui_control+0x18`≈map 计数（macOS 0x146 vs iOS 0x1）、`+0x20`≈vm_object。
AMFI `hook_file_check_mmap@0xfffffe000a659664`：`prot&4 && !isdyldsharedcache → cred_sb_evaluate`（macOS 缓存被跳过）。

### 下一棒（按序）
1. 读 `moc`（ui_control）真实布局 + `vm_object+0x70 copy_strategy`/`+0x7C flags`（internal/pager_ready/true_share）——对比 iOS 文件对象差异。
2. 若 copy_strategically 嫌疑成立：`object->copy_strategy` 取值定路径（COPY_DELAY→shadow / COPY_NONE→1.7GB 物理复制 / COPY_CALL→pager 拒）。
3. 设备实验（等环境自愈后）：fc0 改 dynamic VA=0x180000000 测 **匿名@offset0**——若成功⇒坐实"file-path 独挂"；若 EFAULT⇒offset0 另有毒。
4. 追 `sub_801FC88`（CE34 内的 protect/pmap 同步）return-1 站点。

### 环境新陷阱（今天实测）
- **解释器 = `/usr/lib/dyld`**（非 /usr/bin/dyld）；该文件今天被未知写入者覆盖过一次（12:51，1228848B 非 TC 版→全 chroot 137）。部署探针要**两个路径都放**。
- **exec-veto 级联**：连续多次 CS-invalid exec 后**所有** chroot exec 全 137（连 `true`、连 TC 内 dyld），zsh 自身 dlopen 也开始报 CS invalid。此前会自愈；发生时先验证 `chroot . /usr/bin/true`。
- **python3 stdin/`-c` 模式 segfault**(rc=139 无输出，Dopamine checkin 限制）——**只用文件脚本**：`/var/mobile/{set_vshared.py,fgdump.py,vnd8.py,objdump2.py,blob_read.py}`。
- `sysctl` 全路径 `/var/jb/usr/sbin/sysctl`；`vm.shared_region_pivot` 从当前 shell 写会被 EPERM。
- **scratch 缓存已污染**：`/var/mnt/rootfs/macdsc/` 被我多次原地改（内嵌签名与内容失配）⇒ 勿再用于 CS 实验；恢复源 = Mac `/Users/ciscohe/Desktop/macPad/analysis/dyld-cache-15.6.1/dyld_shared_cache_arm64e`（0xa1b18000，dynoff 0x12c75c000，prot 0x500000005）。

---

## ★★★★★ 2026-09-28（二）⭐⭐⭐⭐⭐ MILESTONE：**536 失败二分定位——fd=-1 路径干净；主文件双错误：EINVAL(setup) vs EFAULT(enter@offset0)**

### 二分结果（runtime-confirmed，均经 e5errno 探针读原始 errno）

| 变体 | 提交内容 | 结果 | 结论 |
|---|---|---|---|
| `fc0` (W28=0) | files=[仅 dynamic 匿名区 fd=-1] 1 mapping | **536 成功** | task 有 region、root dir 匹配、fd=-1/匿名路径全干净 |
| `nodyn`+`filescount1` | files=[main] 8+1 mappings | **EINVAL(0x16)** | 真实文件路径挂 |
| `map1`(+nodyn) | files=[main] count=1 → 仅 mapping[0] | **EFAULT(0x0e)** | setup 过了，enter 挂 |
| `zf0`+map1 | mapping[0] init_prot=ZF(匿名 enter，同址 offset0) | **EFAULT** | 非文件对象问题，是地址/子映射侧 |
| `slide0` | files[0].sf_slide=0(slide=0) | 仍 EINVAL | 随机 slide 出窗假设**否** |
| `noslide`（旧） | NOP |=SLIDE @0x35eec | 仍 EINVAL | ⚠️ 该 patch 只去"附加 OR"；若记录本身含 0x20 位则无效——**不能排除 slide 路径，待真清位** |

### 两个独立失败
- **EINVAL(22)**：多映射时在 **setup 阶段**（enter 之前）触发——嫌疑集中在逐映射 `ubc_cs_is_range_codesigned`（macOS 文件已挂 detached blob，`F_GETSIGSINFO`(105) 实测 rc=0 platform=0；iOS 文件 vnode **无 blob 却通过** → "缺 blob"不成立；detached-blob 的类型/覆盖度可能不同）或逐映射地址/CS 校验。
- **EFAULT(14)=KERN_INVALID_ADDRESS**：单 mapping[0](VA 0x180000000→子映射 offset 0)在 `vm_map_enter(_mem_object)` 失败；**同址匿名 enter 也失败** ⇒ 地址侧问题；**offset 0x78000000 的匿名区成功** ⇒ 子映射非全坏。候选：`sms_slide_start` copyin(slide_info)（若记录 prot 自带 SLIDE 则 noslide 无效——**当前首要验证**）、子映射 offset0 特殊处理、nested-pmap 限制。

### 运行时实测 region 几何（KRW, slide=0x1eebc000）
- chroot region：`base=0x180000000 size=0x100000000 nest=同` `cpu_subtype=0x0`(被 arm64ify 的主 exec 决定！) `first_map=-1`。
- 子映射实测 `min=0 max=0x100000000`，残留匿名 entry `[0x78000000,0x78004000)`（dynamic 区遗留，不消）。
- 两系统 region `cpu_subtype=0x2(arm64e)`；我方 region subtype=0 → **RE 时 sr_cpu_subtype==ARM64E 的 ptrauth/auth 分支不会走**。

### 新探针（build_dyld.py）
`fc0`@0x3538c `MOV W28,#0`；`nodyn`@0x354ec `files_count=W28`；`map1`/`map4`@0x3553c `MOV W13,#1/#4`(files[i].count)；`slide0`@0x3552c `sf_slide=0`；`zf0`@0x35ef0 `init_prot=0x407`；`noslide`@0x35eec NOP。
**e5errno 探针是唯一可信 errno 源**（e5entry@0x76e04→e5cave@0x38d08,write(2,errno,8)）；0x38d08 作为 cave **可用**（早前 SIGILL 判错因）。

### 关键内核结构偏移（xnu-8792.82.2, arm64e）
`vm_shared_region`: +0x18 rootdir, +0x30 first_map, +0x38 base, +0x40 size, +0x48/+0x50 nest, +0x76 stale。
`ipc_port`+0x48=ip_kobject → `vm_named_entry`+0x10=backing.map → `_vm_map`+0x20=min,+0x28=max,+0x18=first entry,+0x30=nentries。
`shared_file_np`={fd,count,sf_slide} 12B；`shared_file_mapping_slide_np`=0x30B{sms_address,sms_size,sms_file_offset,sms_slide_size,sms_slide_start,sms_max_prot,sms_init_prot}。
`F_GETSIGSINFO`=105 可查 vnode 附着 blob（ENOENT=无）。
⚠️ `vm.shared_region_trace_level=7` 已开但 **kprintf 不进 dmesg**(iOS)——trace 路线不可用。
`vm.shared_region_destroy_delay` 可写（0=即销）；`vm.shared_region_pivot` 可写=全标 stale。

### 下一步
1. 用 `slidecave2`（真清 SLIDE 位+清 sf_slide）+ map1：errno 变→slide copyin 确认；不变→direct enter offset0。
2. mapN(2/4/8）逐加映射，定位首个触发 EINVAL 的 mapping→再查其 foff/CS/属性。
3. 若 slide 排除：KRW 在 enter 失败前后对比子映射 entry 表，或用 cave 把 mapping[0].sms_address 挪到 0x1840000000(offset≠0)判 offset0 特异性。

---

## ★★★★★ 2026-09-28 ⭐⭐⭐⭐⭐ MILESTONE：**cdhash 算法搞错 = 之前所有 `SIGNALED 9` 的真因**（runtime-confirmed）

**结论（不要再推导）**：内核 exec 准入查的 cdhash 是
`sha256(CodeDirectory[0 : CD.length字段])[:20]` —— **只哈希 CodeDirectory 本体**
（长度取 CD 头 +4 的 `length`），**不是** `sha256(CD..superblob尾)`（会多算对齐 padding），
也**不是**整个 superblob。上一轮 `try_dyld.sh`/临时脚本用了 `sha256(b[o:])`（到尾）→
算出的 hash 永远不在核心里 → `jbctl trustcache add` 加的是**无效项** → AMFI `CS_KILL` →
`SIGNALED 9`（零 dyld 输出）。**签名配方（ldid 裸签 flags=0x0）从头到尾没问题。**

**铁证（设备实测，TC=jbctl trustcache info）**：

| dyld | 结果 | H_full=sha256(CD..尾) | H_cdlen=sha256(CD[0:cdlen]) |
|---|---|---|---|
| dyld_plat.bin | **rc=0** | a9529bb2（不在 TC） | **2821ecab（在 TC）** |
| dyld_p2.bin | **rc=0** | 0fd89939（在 TC） | 9bf6c2a7（在 TC） |
| nm/deploy.bin | SIGNALED 9 | d74e1dcb（在 TC） | 10b958ba（**不在 TC**） |
| dyld_p3.bin | SIGNALED 9 | 411077e8（在 TC） | 639c6f84（**不在 TC**） |

- **决定性验证**：给 deploy.bin 只加 H_cdlen（10B958BA…）→ 立刻 `rc=0`（3×）。
- 判别式：`re-clears=2` ⇒ 准入通过；`re-clears=1` ⇒ 被杀。**确定性，非竞态**（8/8 稳定）。
- 正确工具：`misc`/设备上 `/var/mobile/nm/cdhash_slices.py`（它对：`sha256(b[o:o+cdlen])`）。
  临时脚本一律改用它，别再手写 `sha256(b[o:])`。

**重启后复原（交付物 A，已验证）**：`mount | grep mnt` 为空但 `/var/mnt/rootfs` 可用（普通目录，非挂载点，重启不丢）；
真正会丢的是 **jailbreak trustcache（内存）**。所以复原 = 部署活件 + 对 **dyld 及 HELLO 路径每个 Mach-O**（dyld / libSystem.B.dylib / libdyld.dylib / echo …按实际加载集）
用 `cdhash_slices.py` 算 cdhash 后 `jbctl trustcache add`。可用 `misc/restore_env.sh`（已用正确工具）。

**※ 勘误（覆盖本文件下方 2026-09-27 的旧说法）**：line 23「base 重签同名 → 与 base 字节完全相同 ⇒ 配方正确」成立；
但 line 62-64「`-Cadhoc` 只在内容改动时才致命」等旧解释作废——真实变量是 **cdhash 是否按 `CD[0:cdlen]` 算对并进 TC**。

## ★★★★★ 2026-09-27（晚）⭐⭐⭐⭐⭐ MILESTONE：**修改过的 dyld 准入规则破解** —— `ldid` 裸签（无 `-Cadhoc`）是唯一存活配方

### 🎉🎉 2026-09-28 里程碑：**536 映射 iOS 缓存成功（真因 = `files[].sf_slide` 非 16K 对齐）**
**真凶**：`shared_file_np` 是 **12B/条 `{sf_fd, sf_mappings_count, sf_slide}`**；iOS split-cache 主分片的 `sf_slide` = **0x539b0000（未对齐）** ⇒ 内核在文件循环之前/内就 EINVAL(22)。
**最小修复**：在 536 调用点前把每条 entry 的 `sf_slide`（+8）清零。
- 构建：`python3 build_dyld.py dyld_sf0.bin crossarch plataccept` 后再打 cave（`0x35690→0x9b578`：`mov x9,x1; mov x10,x0; loop{str wzr,[x9,#8]; add x9,#12; subs;b.ne}; mov x3,x26; b 0x35694`）。产物 `analysis/dyldwork/dyld_sf0.bin`。

**运行验收（3×）**：`DYLD_SHARED_CACHE_DIR=/iosdsc DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_SEGMENTS=1 chroot /var/mnt/rootfs /bin/echo HELLO`
- rc=0、输出 HELLO；**`Using mapping in dyld cache` × 91**、**`re-using existing shared cache` × 2、`cache not loaded` × 0**。
- `cknp2` 探针：**`check_np ret=0, base=0x180000000`** ⇒ **shared region 已被 iOS 缓存填上**。
- 无 env 基线仍正常（HELLO）。

**⚠️ 更正（重要）**：91 个 image 确实走“缓存映射”，但 **`/usr/lib/libSystem.B.dylib` 实际仍取磁盘 shim**（判据：`/bin/echo` 只需 `_err` → shim 有→过；`/bin/cat`/`/bin/sh` 需 `___error` → 报 `Symbol not found: ___error Expected in <B90391D8> /usr/lib/libSystem.B.dylib`，而 B90391D8 = **shim 的 UUID**）。
**真正的下一道墙 = libSystem 平台/兼容**：移走 shim → `wrong platform to load into process`；用 `platstub`（`loadableIntoProcess→1`）强载 iOS 库 → **SIGILL(132)**。⇒ “库全走 iOS 缓存”尚未完成，核心堵点是 **iOS libSystem × macOS 进程**。

**前置条件（缺一不可）**：① 重启后补 trustcache（`misc/restore_env.sh`）；② `cachereg_ios` 对 `/iosdsc/*` **全部 46 片**附加 CS blob；③ `set_blob_cov.py` 把 46 片 `csb_end_offset` 改成文件大小。

**下一步**：移除磁盘 shim 后，dyld 已能用缓存解析 91 库，但 `/usr/lib/libSystem.B.dylib` 会被平台检查拒（`wrong platform to load into process`）；用 `platstub`（`loadableIntoProcess→1`，诊断性）强行加载 iOS 库则 **SIGILL(132)** ⇒ 下一道墙 = **iOS 库在 macOS 进程里的平台/兼容兼容性**（需评估是否需真正的 macOS 缓存而非 iOS 缓存）。

### 🔜 2026-09-28 下一步计划（待重启测试）：改映射 **macOS 自己的缓存**
- 动机：macOS 缓存里的 libSystem **兼容 macOS 进程**，可绕过“iOS 库不兼容”的墙。
- 已知障碍：state doc 旧结论“**macOS 缓存 slide-info version=5，内核只支持 1..4**” —— ⚠️ **已证伪：本 macOS 缓存 `slideInfoVersion=0/slideInfoOffset=0`（无 slide-info）**。
- 对策：dyld 侧把每条 mapping 的 `sms_slide_size(+0x18)/sms_slide_start(+0x20)` 清零（不触发内核读 slide-info） + `files[].sf_slide` 清零。
- 产物：`analysis/dyldwork/dyld_noslide.bin`（= crossarch+plataccept + sf0 + mapping-slide 清零）。
- 实测：现 region 已被 iOS 缓存占；强制不复用后映射 macOS 缓存 **仍 EINVAL(22)**（因 region 非空）。
- **待办**：**重启取得干净 region** → 跑设备上 `/var/mobile/post_reboot_mac.sh`（自动：复原 TC → cachereg macOS 缓存 → blob 覆盖 → 部署 dyld_noslide → 验收 HELLO/using/notloaded + cat/ls/sh）。

### ⚠️ 2026-09-28 重启后环境退化（必须知道，否则会误判）
**设备发生过一次 panic 重启**（`ptd ... does not belong to iommu @pmap.c:15786` + `initproc exited`；现场 VirtualMachine.xpc 512% CPU/load 27+，与 dyld 实验无直接因果）。**重启后下列状态丢失，需重建**：
1. **jailbreak trustcache 清空** → 修改版 dyld 立即 `SIGNALED 9`（exec veto）。**修复：重新 `jbctl trustcache add` 后才能过准入**（本次实测：补 TC 后 9→6/0）。
2. **cachereg（对 macOS 缓存）需要重启**：否则 dyld 报 `code signature registration for shared cache failed`（缓存文件无 CS blob）。
3. **shim `libSystem.B.dylib` 变为不可用**：`Library not loaded: /usr/lib/libSystem.B.dylib`，`Reason: code signature invalid (errno=1) sliceOffset=0x00018000 ...`（该文件是 **fat 两切片**，`cafebabe 00000002`；`ldid -Hsha256 -S` 重签 + 补 TC 仍报 invalid）。⇒ **HELLO 目前不通**，需先把 shim 恢复到可被信任的形态（重启前它是可用的）。
4. **marker 组合测试（crossarch+plataccept+mkA+mkB+m2u+m2S）结果：`SIGNALED 11`，零 marker、零 dyld 输出** ⇒ 这些 marker 站点/机制确实有问题（与 subagent 判断一致），**不要再用它们做判据**；需另选探针方式（如 `cknp2entry/cknp2cave` 之前在旧环境里是可用的）。

### 🔧 2026-09-28 补记：签名/部署方法论（实测勘误，务必按此）

**实测矩阵（设备上，`run_nocskill ... chroot $R /bin/echo HELLO`）**：

| 被测物 | 签名方式 | 结果 |
|---|---|---|
| pristine | ldid -Hsha256 -S<ent> | **SIGSYS(12)**（过准入；pristine 无 crossarch → svc 触发 SIGSYS） |
| dyld_probe_noC（活基线） | 原样 | **rc=0** |
| dyld_es.bin | 原样 | **SIGILL(4)**（过准入，探针自身错） |
| dyld_plat = crossarch+plataccept | ldid -Hsha256 -S<ent> | **rc=0 ✅** |
| base 重签为**同名**dyld_probe_noC.bin | ldid -Hsha256 -S<ent> | **与 base 字节完全相同** ⇒ ldid 就是原工具、配方正确 |

**⚠️ 关键教训（之前误判“签名配方不对”）**：
- 先前多轮 `SIGNALED 9` 实为**状态污染**（连续多次 rm+cp 换 dyld / vnode CS 缓存 / cachereg 常驻），**不是**签名不被接受。
- **正确的签名配方（已验证可重新产出可用 dyld）**：
  1. `ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist <file>`（**不加** `-Cadhoc`、**不加** `-M`）
  2. `cdhash40 = sha256(CD blob)[:20]`（注意：是**整个 CD blob**（含 4B magic+4B len），取 sha256 前 40 hex）
  3. `jbctl trustcache add <cdhash40>`
  4. **`rm -f` 目标后 `cp`**（新 inode）+ `chmod 755`
  5. 若连续换 dyld 出现异常 veto：**先回滚到活基线一次，再部署新件**（消除残留）。
- `jbctl trustcache info` 输出是**大写 hex**，grep 必须 `-i`。
- **admission 不依赖 jailbreak trustcache**（活基线 cdhash 不在 TC 里也能跑）；TC 只是历史习惯。

### 🎉🏆 2026-09-28 【新里程碑】 **macOS 自有缓存也成功灌进 region**（536=0），随后命中已知 post-reuse SEGV
**实测（重启后的干净 region，脚本 `post_reboot_mac.sh`）**：
```
CACHE_DIR=/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld
rk=0? → 实为 rc=139(SIGSEGV)；但：
  Using mapping in dyld cache ×91\n  not loaded ×0
  Using mapping in dyld cache for /usr/lib/libSystem.B.dylib  → UUID D161E41A（非 shim B90391D8）
  re-using existing shared cache (/private/preboot/.../Caches/com.apple.dyld/dyld_shared_cache_arm64e)
  最后一行：Mapping the shared cache system wide → 随后 SIGSEGV
```
⇒ **536 对 macOS 缓存也成功（0）且已复用**；libSystem 来自缓存。**新的（已知）堆 = post-reuse SEGV**。

**源码关键事实（dyld 1286.10 `SharedCacheRuntime.cpp`）**：
- `files[i].sf_slide = (i==0) ? infoArray[0].maxSlide : 0;` ⇒ **我们的 zero-slide 修复与上游一致**（iOS maxSlide 非对齐才 EINVAL）。
- `console("Mapping the shared cache system wide")` 在构 files[]/mappings[] 之前 ⇒ 崩在构数组/536/其后。

### 🔬 2026-09-28 冒死取证：post-reuse 不是普通 SEGV，而是**内核 CS "Invalid Page" 击杀**
**实测（崩溃报告 `echo-2026-09-27-235846.ips`）**：
```
exception.type = EXC_BAD_ACCESS, signal = SIGKILL - CODESIGNING (subtype UNKNOWN_0x32)
termination.indicator = "Invalid Page" (namespace=CODESIGNING)
faulting 0x100bbe9f8 ∈ mapped file 0x100b48000-0x100be4000 (624K, r-x/r-x, SM=COW)
邻居：0x100ae4000-0x100aec000 (32K r--, gap 0x5c000) / 0x100be4000-0x100bec000 (32K rw-)
```
⇒ **不是 dyld 的普通野指针 SEGV，而是内核 page-CS 校验失败后的 SIGKILL**（CLAUDE.md 点名的同类阻塞：“CS-enforced 页 + 普通 mmap → CS kill”）。
- 对照实验：**不映射缓存（CACHE_DIR 指向不存在目录）时不会被杀**（echo rc=0）⇒ 击杀由“映射后从的库”引入。
- 注：崩溃地址在**普通 mmap 文件区**（非 shared region 0x180000000），且 dyld 日志显示最后加载的是 `/usr/lib/libMatch.1.dylib`（**iOS UUID 83CA476B**，无法在 chroot /usr/lib 找到）与 procursus 注入库 ⇒ 怀疑 **chroot 下 /usr/lib 被 iOS 的 bindfs `.fakelib` 命中** 或缓存 image 列表混入 iOS 路径。
- 探针局限：dyld 日志未打印 0x100b48xxx 段的 image 名；`.ips` 的 usedImages 为 0 ⇒ 需另建“地址→image”映射（如逐个 image mmap 跟踪）。

## 🧨🧨🧨 2026-09-29 01:1x【根因确定】chroot 进程【没有 shared region】⇒ 536 恒 EINVAL(22)
**证据（组合探针 + 项目自带 sprobe）**：
- 自建组合探针（hook 0x35690 dump 536 入参 + 调 check_np(294)，hook 0x76e00 dump 返回值）：`check_np` = **22**、files_count=3、mappings=16 ✓（macOS 缓存两片齐全）。
- 用带 `reuseExistingCache→ret0` 的 dyld（即**没有任何东西会销毁 region**）再测：`check_np` **仍 = 22** ⇒ **不是 reuse 弄丢的，是进程根本没有 region**。
- 项目自带 freestanding 探针 `/var/mnt/rootfs/tmp/sprobe`（无 dyld 依赖）直接 chroot 运行：输出第 5 字段 = **22** ⇒ 与 `misc/sprobe.c` errno 表一致（“22 = no shared region”）。
- ❗ 对照：之前的 iOS 缓存成功时 `cknp2` 曾测到 `check_np ret=0 base=0x180000000` ⇒ **那时区是存在的**。⇒ **“为什么同一个 chroot 进程有时有 region（以至于能映射 iOS 缓存）、现在却 22”是剩余唯一问题**。

**源码追踪（xnu-8792.81.2）**：
- `vm_map.c:13397` —— `vm_map_exec()` **无条件**调 `vm_shared_region_enter(new_map, task, is64bit, **fsroot**, cpu, cpu_subtype, ...)`；
- `vm_shared_region_enter()` → `vm_shared_region_lookup(fsroot,cpu,subtype,is64bit,pgshift,reslide,driverkit,rsr)`（**create if needed**）→ 若返回 NULL 则 `return KERN_FAILURE`，而调用方 `(void)` **忽略错误** ⇒ **进程就没有 region**；
- `vm_shared_region_create()` 对 64-bit 只在 `cputype!=CPU_TYPE_ARM64` 或 `sub_map==VM_MAP_NULL` 时才返回 NULL（arm64/arm64e 的 cputype 都是 0x0100000c ✓ 不在排除范围）。
- **实测排除“dyld 把空区删了”**：把 dyld 的 check_np stub（`0x76dcc`）改成 `mov w0,#12; ret`（假返回、绝不触发内核 `vm_shared_region_remove`）后，536 **仍失败** ⇒ 区不是被 dyld 删的。
⇒ **剩余唯一疑点**：exec 时 `vm_shared_region_lookup/create` 以 **fsroot(chroot 根 vnode)** 为 key 建区**未成功**（或建到了不同 key 上）。
**下一步（内核侧）**：用 KRW 读 `vm_shared_region_queue` / `vm_shared_region_count` 看是否有任何 region；再对 spawn 路径（`exec_mach_imgact → vm_map_exec` 传入的 fsroot 与 cpu/subtype/reslide）取样核对。

## 🌈🌈🌈 2026-09-29 凌晨【收敛】两种缓存都能映射了；配方与剩余堵点明确
### ✅ 已打通（重启后干净态，均 echo rc=0 HELLO）
| 缓存 | 正确 dyld | 关键条件 |
|---|---|---|
| **iOS** | `dyld_sf0`（crossarch+plataccept + 清 `sf_slide`） | `maxSlide=0x539b0000` **非法**→必须清零；cachereg_ios 46 片 |
| **macOS** | **`dyld_plat`（plain：crossarch+plataccept，不动 `sf_slide`）** | `maxSlide=0x20000000` **合法**→必须保留；cachereg(mac) |

**★ 发现 `maxSlide` 在缓存头偏移 `0xf0`**：iOS=`0x539b0000`（非16K对齐→内核 EINVAL），macOS=`0x20000000`（对齐）。
⇒ `files[0].sf_slide = maxSlide`；**清零只对 iOS 必要且对 macOS 有害**。

**★ `set_blob_cov.py`(csb_end_offset→filesize) 是 macOS 缓存 536 失败的原因**：恢复天然覆盖（只跑 cachereg）后 **macOS 两片 536 成功（notloaded=0）**。**以后不要对缓存做扩覆盖**。

### ⛔ 剩余堵点：dyld 不从缓存“绑定” libSystem- `cat/ls/sh` 仍 `rc=134`：`___error` / `libutil` 缺失。
- 日志同时出现 `<D161E41A…> /usr/lib/libSystem.B.dylib` 与 `<B90391D8…>(shim)`；**符号解析走了 shim**。
- **移走 shim** 后反而 `Library not loaded: /usr/lib/libSystem.B.dylib`（仅尝试磁盘路径）⇒ dyld 没有把缓存中的 libSystem 当作可用镜像。
- 下一步候选：①核实映射进去的到底是哪个缓存（读缓存头 uuid）；②`DYLD_SHARED_CACHE_DIR` 指向非标准目录是否导致“不作为系统缓存”；③libSystem 是否位于 `.01` 分片而子缓存未被纳入依赖查找。

### 🔍 2026-09-29 凌晨【最后定位】region 里装的其实是 **iOS 缓存**（不是 macOS）（⚠️ 见下方勘误）
**证据**：日志中被加载的 image 全是 **iOS 专属**（`/usr/lib/libMatch.1.dylib`、`AppleMobileFileIntegrity.framework`、`libmis.dylib`、`libsandbox.1.dylib`、`MobileSystemServices`）；
且 `re-using existing shared cache (/private/preboot/…/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e)` = **设备 iOS 缓存**的默认路径。
⇒ 因此 `ls` 报 `libutil.dylib (no such file, **no dyld cache**)`（iOS 缓存里没有 macOS 的 libutil）。

**明早解法（推荐顺序）**：
1. 让 chroot 在 dyld **默认缓存目录**处看到 **macOS 两片**（例：把 macOS `dyld_shared_cache_arm64e{,.01}` 放到或 mount 到 chroot 的 `/System/Library/Caches/com.apple.dyld/`），并**屏蔽 iOS 泄漏路径**（`/private/preboot/.../Caches/com.apple.dyld`），再跑 `post_reboot_final.sh` 的 CLI 验收；
2. 若不行，则核实 `CacheFinder` 的选择逻辑（分析源码 `DyldProcessConfig.cpp`），必要时用 `dyld_patch` 强制 cache dir。
**验收判据**：`cat/ls/sh` 的 `rc=0`，且 libSystem 只出现 `D161E41A`（不带 shim `B90391D8`）。

#### ⚠️ 勘误（2026-09-29 更晚）：上段“占用者=iOS 缓存”的证据不足
- `/usr/lib/libutil.dylib` 在 **iOS 缓存里也有**（grep 命中）⇒ 不能用它判定占用者；
- 日志里的 4 个“iOS 专属”镜像实际来自**越狱注入库闭包**（`procursus/ellekit/libinjector` 命中 11 处）；
- 标准路径 `/System/Library/dyld/dyld_shared_cache_arm64e` 在 rootfs 里是 **79B symlink → cryptex 的 macOS 缓存**（uuid `4c1223e5…`，maxSlide `0x20000000`）；`/private/tmp/dsc/` 还有一份完整 macOS 缓存副本。
⇒ **占用者更可能是 macOS 缓存**。真正的失败模式有两种：(1) 某些运行 `536` 失败→`notloaded=1`→`libutil (no such file, **no dyld cache**)`（dyld 当时根本没缓存）；(2) 缓存已映射时，`libSystem` 却被解析到**磁盘 shim**（`Expected in B90391D8`）——疑似**注入的 iOS 库先把磁盘 shim 拉成 libSystem**，后续 macOS 二进制的同名依赖命中了它。
⇒ 下步两选：**(a)** 在“`notloaded=0`”的那次里再试**移走 shim**（彻底让缓存提供 libSystem）；**(b)** 给 shim 补上缺失符号（务实解锁 CLI）。

### 🌫️ 2026-09-29 00:55+【当前状态】macOS 缓存 536 非确定性；iOS 缓存稳定
| 对象 | 结果（同一 boot 内反复测） |
|---|---|
| iOS 缓存（`/iosdsc`，46 片） | **稳定**：`notloaded=0`、`using=91`、echo `rc=0 HELLO` |
| macOS 缓存（2 片） | **不稳定**：`dyld_errno`(plat+cave) → `notloaded=0` 然后 **rc=139 崩**；`dyld_plat`/`dyld_sf0` → `notloaded=1`（`syscall to map cache into shared region failed`） |

**推论**：macOS 缓存首次映射成功后进程崩溃 139，留下**残留 region** ⇒ 后续 536 失败；直到 region 被释放/重启。
**`cat/ls/sh` 的 libSystem 三种去向**：①磁盘 shim → 缺 `___error`；②iOS 缓存 libSystem → `wrong platform to load into process`；③`platstub` 强载 iOS 库 → **SIGILL(132)**。
⇒ macOS CLI 要跑通，必须让**macOS 缓存可靠映射且不崩**（当前最大路障）。

### 🧪 新探针：`dyld_errno.bin`（在 `cerror` 之后 dump 真实 errno，保持原错误路径）
- 做法：hook `0x76e14`（536 stub 错误路径的 `mov sp,x29`）→ cave：`mrs x1,TPIDRRO_EL0; ldr w1,[x1]; write(2,&errno,4)` → 回放 `mov sp,x29` → `b 0x76e18`。
- 产物：`analysis/dyldwork/dyld_errno.bin`（已部署 `/var/mobile/`）。

### 🚨 重要操作规则（否则永远在测错对象）**只要在“目标缓存映射”之前跑过任何不带 `DYLD_SHARED_CACHE_DIR` 的 chroot 命令**（如 `restore_env.sh` 的内部验证、基线 `chroot .../echo`），
region 就会被 **设备 iOS 缓存**先占（首次映射持久）⇒ 后续全部在**复用 iOS 缓存**。
⇒ **正确脚本**：`analysis/dyldwork/post_reboot_cli.sh`（已部署 `/var/mobile/`，双端 `bash -n` OK，md5 `7571a720…`）
  要点：TC **手工补**（不跑 chroot）、**不做基线**、探针用不存在目录、**第一个真映射 = macOS 缓存**（plain dyld）、cachereg 天然覆盖。

### 🌟🌟 2026-09-28 深夜【颠覆性】重启后干净态梯度：**CS 击杀是“自伤” —— mapping-slide 清零补丁才是元凶**

**脚本**：`post_reboot_ladder.sh`（重启后自动跑完）。**结果**：
| 步骤 | rc | using | notloaded |
|---|---|---|---|
| 4 iOS 无blob | 124(挂) | 91 | 0 |
| 5 iOS blob 不扩覆盖 | 124(挂) | 91 | 0 |
| 6 iOS blob+扩覆盖 | **0 HELLO** | 91 | 0 |
| 7 macOS 无blob | **0 HELLO** | 91 | 0 |
| 8 macOS blob 不扩覆盖 | **0 HELLO** | 91 | 0 |
| 9 macOS blob+扩覆盖 | **0 HELLO** | 91 | 0 |

**随后手测（决定性）**：
- `dyld_nsl2`（**清零 mapping slide**）→ **rc=124 挂死/之前 rc=137 击杀**
- 换回 **`dyld_sf0`（仅清 `files[].sf_slide`）** → **echo ×3 均 rc=0 + HELLO**（iOS 与 macOS 缓存都行，`IOSOK`/`MACOK`）
⇒ **之前的“CS Invalid Page 击杀”是 `dyld_nsl2` 那个补丁自伤**（清 mapping slide 破坏了 rebase），**非本质阻塞**。**今后只用 `dyld_sf0`**。

**遗留（下次重启后再测）**：region 会被首次映射“占住”且**持久**，于是后跑的 macOS 缓存测试实际复用了先前的 iOS 缓存（证据：cat 报 `Expected in <B90391D8…>` = **shim UUID**；`check_np=0`）。
⇒ **正确测法**：重启后 **第一个**就映射 **macOS 缓存**（`DYLD_SHARED_CACHE_DIR=/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld` + `dyld_sf0`），再测 `cat/ls/sh`。
- 补充实测（免重启不可行）：`check_np base=0x180000000` = **iOS 缓存基址**（macOS 缓存映射在 `0x1CE430000`）⇒ 当前 region 装的确实是 iOS 缓存。
- **在映射中途 SIGKILL 无法释放 region**（试两次仍 `0/0x180000000`）⇒ **只能靠重启复位**。
- 用 `dyld_sf0` 向“iOS 已占”的 region 里映射 macOS 缓存 → **rc=139**（与 region 已有映射冲突相关）。

### 🧪 2026-09-28 IDA(Instance2=kernel) 取证：cs_validate_page / 无条件击杀 printf
- **`osfmk/vm/vm_fault.c:2863`**：`printf("CODE SIGNING: process %d[%s]: rejecting invalid page at address 0x%llx from offset 0x%llx in file \"%s%s%s\" ...")` —— **无条件打印（带文件名）**；本机**无法抓内核日志**（无真实 `log` 二进制；`log show` 空；无 dmesg/sysctl）⇒ 该线索暂时用不上。
- **`bsd/kern/ubc_subr.c:5226-5300`**（`cs_validate_page`）：页必须在某 blob 覆盖窗内（否则 `continue`），然后在 **CD 哈希表**查哈希；查不到 → `found_hash=FALSE` → `validated=FALSE` → 由 `vm_fault_enter` 决定击杀。
  ⇒ 我们对 dsc 的 `csb_end_offset` 扩到整文件后，超出原签名范围的页“被覆盖但无哈希”→ 仍 `validated=FALSE`。
- 实测补充：macOS 缓存 vnode **`v_flag=0x184a00` 已含 VSHARED_DYLD(0x200)**（`already set`）⇒ 设 VSHARED_DYLD 不解决问题。
- 崩溃模式稳定：fault 总在某个 **624K r-x mapped file** 的 `region_start+0x769F8`；且该区域**不在 dyld 的 segment 日志中** ⇒ 由**非 dyld 途径**映射（内核共享区/越狱注入器/plain mmap）。
- **稳定性复现（本轮）**：同一环境连跑 `cat→echo→cat`，**3/3 均 `rc=137` + `using=91` + `notloaded=0` + `re-using existing shared cache`** ⇒ 击杀是**确定性**的，之前偶发的 rc=0 不可靠。
- ❗ 结论：**536 与缓存复用均已成功**，唯一未通 = **消费缓存页时的内核 CS 击杀**。
- 崩溃报告 `ktriageinfo` 明确定性：**“VM - A memory corruption was found in executable text”**（=可执行文本页 CS 校验失败）。
- ❗ **lldb 取证在结构上受阻**：chroot 的 `libSystem.B.dylib` 是只有 10 个符号的 shim ⇒ `bash`/`lldb` 无法链接加载（`Killed: 9`/Symbol not found）。要跑 lldb 需真实缓存 libSystem，而它正是崩点 ⇒ **鸡生蛋**。
- 下一步建议（需你/明早）：① 用能读内核日志的手段拿到 `vm_fault.c:2863` 那条带**文件名**的 print（含 serial/kdp/sysdiagnose）；② 或从“**越狱注入的 iOS 库**”入手（本次扫到 `libbrotlienc` TEXT=0xa0000 接近 624K，但未命中；待查 624K r-x 的真正归属）；③ 或换策略避免对缓存/DSC 的 plain mmap。
- **击杀与 cs_blob 无关（实测）**：把 `cachereg` 全关（`pgrep` 无进程）后，iOS/macOS 缓存仍然 `using=91 / notloaded=0 / re-using` → **rc=137**。
- ❗ **关键机制（源码）**：`vm_shared_region_map_file()`（536 引擎，`vm_shared_region.c:1676-1680`）对每个被映射文件设 **`file_object->object_is_shared_cache = true`**
  ⇒ **经 536 映射的缓存页不会被 CS 逐页校验**。因此击杀页**必来自“非 536 途径”映射的文件**（与“fault 区域不在 dyld segment 日志/不在 region”一致）。
  ⇒ 嫌疑收敛：**越狱注入的 iOS dylib** 或某个磁盘库（其 ubc 对象被 CS 强制）。
- **只有“刚重启”才值得做的实验梯度**（脚本 `analysis/dyldwork/post_reboot_ladder.sh`，已部署到设备 `/var/mobile/`）：
  基线 → (iOS) **无blob** → **有blob不扩覆盖** → **扩覆盖** → (macOS) 同三步 → 收尾；每步记录 `rc / 536原始errno / notloaded / Using mapping`，stderr 存 `/var/mobile/L_*.err`。
  （目的：验证①无 blob 时 536 是否还能过；②**我们的 `csb_end_offset` 扩覆盖是否正是 CS 击杀诱因**；③vnode 标志/cs_blob 脏态是否贡献。）

**下半目标（post-reuse SEGV）取证工具与阻塞**：- 工具：`analysis/dyldwork/catch_segv.sh`（chroot lldb 拓 PC/far/backtrace）。
- 阻塞：chroot 里的 `bash`/`lldb` 未签名→TC → AMFI `Killed: 9`；**需先 `ldid -Hsha256 -S<ent>` + `cdhash_slices.py`+TC 后再跑 catch_segv**（下一轮）。
- 其他观察：重启后若映射进程崩溃（139），**region 会自动释放**（`check_np` 又回 12）⇒ **不用每次重启就能重试**。

### ✅ 本轮已达成：`crossarch+plataccept` 的修改版 dyld **通过 exec 准入并跑通 HELLO**（§2 iOS 缓存实验的前置全部就绪）

### 🎯 2026-09-28 环境已复原 + 536 实测结论（重跑 subagent 任务）
**A. 环境复原（已验证）**：重启后只丢 **jailbreak trustcache（内存）**；rootfs 是普通目录不丢。复原 = 部署活件 + 用 **`misc/cdhash_slices.py`**（对 **每个 slice** 算 `sha256(CD[0:cdlen])`）→ `jbctl trustcache add`。已用 `misc/restore_env.sh` 一次跑通。
- ❗ `run_nocskill` 旧件硬编码 kernel slide（0x158B4000），**重启后 KASLR 变化 ⇒ `[!] proc not found`**；已修于 `misc/run_nocskill.c`（改为扫描内核 Mach-O 头）。但**实测：TC 复原后 `chroot` 直连即可**（不需 run_nocskill）：`/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO` → **HELLO, rc=0（2/2）**。

**B. 536 实测（关键量化）**：
- 用 `build_dyld.py dyld_p536.bin crossarch plataccept retentry retcave`（在 536 之后写 x0 到 fd2）：stderr 首 8B = **`0xffffffffffffffff`** ⇒ **536 确被调用且返回 -1**。
- 用 `dyld_e5.bin`（crossarch plataccept e5centry e5ccave，挂在 536 stub 失败分支 0x76e04）取 raw errno：**`errno = 22 (EINVAL)`**。
- **重启 `cachereg_ios`** 对 `/iosdsc/dyld_shared_cache_arm64e{,.01}` 附加 CS blob（`REG ... fcntl=0 ... READY ok=1`）后，**errno 仍 = 22** ⇒ **EINVAL 不是 CS blob 门**。
- 仍无 `different platform` 文案 ⇒ **plataccept 生效**。
- **已 dump 536 入参（自建 cave @0x35690 写 x0..x3）**：**x0=0x2e(46 files)、x1=files、x2=0x3a(58 mappings)、x3=mappings** ⇒ 与 iOS 缓存 46 分片完全对得上，**参数集是齐的**。
- 据此反编译 `shared_region_map_and_slide_setup`：EINVAL(22) 站点共 8 处（0x84596c4 计数溢出/0x8459780 region==NULL/0x8459d74 fd==-1且>1映射/0x8459d04 未页对齐/0x8459d50 非VREG/0x8459ce0 无 memory object/0x8459cbc CS 覆盖不足/0x8459d40）。
- `0x8459780` 分支 → `sub_FFFFFE0008063590(task)`→内部 `sub_FFFFFE00080608E8(task)` 取 region，**返回 0 即 22**（候选元凶）。
- **下一步**：区分上述 8 个站点。推荐 **KRW（只读）** 或内核侧打点：在 `sub_FFFFFE0008459570` 的各 EINVAL 赋值处看哪个命中；或先验证“空 region 是否导致 0x8459780”。
- **已 dump files[]/mappings[]（cave @0x35690 写 x1/x3 各 0x60B）**：files 为 12B/条，`fd` 序列 = 4,5,6,7,8,0xA,0xB,0xC…（**全部有效小 fd，无 -1**），每条 `count=1`；mappings 48B/条。
  ⇒ 排除 `计数溢出(0x84596c4)`（46≤58）与 `fd==-1且>1映射(0x8459d74)`；`region==NULL(0x8459780)` 也大概不命中（check_np=12 ⇒ region 存在）。
  ⇒ **最可能剩下 `0x8459cbc`（CS blob 未覆盖 mapping 范围）或 `0x8459ce0`（无 memory object/非VREG）**。两者均与 vnode/ubc 相关，需内核侧运行时证据才能二选一。
- **已排除 ubc 门**：先用 iOS python mmap `iosdsc` 两个 dsc（促 vnode ubc_info）→ errno 仍 22。
- **✅ 已定位 22 = CS 覆盖门 `0x8459cbc`**：该门被 `initProt & 0x10` 守护（`*(v56+44)`）；在 536 前用 cave 对所有 mapping 做 `initProt |= 0x10`（并加 'X' marker 证明确实执行）→ **errno 由 22 变为 14(EFAULT)** ⇒ 门确实换了。
  ⇒ **根因：cachereg 附加到 dsc 的 CS blob 覆盖范围不包含各 mapping 的 [fileOffset, fileOffset+size]**（内核按 `cs_blob[5]/[6]/[7]` 算的覆盖窗口 vs mapping 偏移）。
- **下一步**：让 dsc 的 CS blob **覆盖整个文件**（内核要求 blob 窗口 ⊇ 每个 mapping 的 file 区间）；否则可研究 `initProt` 置位后为何变 EFAULT（可能是 COPY 位导致后续 mmap 语义变化）。
- **源码确证判据**（`analysis/xnu-xnu-8792.81.2/bsd/vm/vm_unix.c:2611-2642`）：CS 覆盖面检查 = `ubc_cs_is_range_codesigned(vp, sms_file_offset, sms_size)`；`ubc_subr.c` 里它要求 `csblob!=NULL && [csb_base_offset+csb_start_offset, csb_base_offset+csb_end_offset] ⊇ [start,start+size]`。
- **`struct cs_blob` 字段偏移（ubc_internal.h）**：`csb_flags`@0x20、`csb_base_offset`@0x28、`csb_start_offset`@0x30、**`csb_end_offset`@0x38**、`csb_mem_size`@0x40。
- **实测（blob_read.py）**：iosdsc main 的 `blob+0x38 = 0x58000`（仅覆盖 352KB）、.01 `= 0x6df4000` ⇒ 远小于文件 ⇒ CS 门必失败。
- **已做修复（`set_blob_cov.py`，KRW 写 `csb_end_offset=filesize`）**：对 `iosdsc/*` **全部 46 个分片**均改成功（main→0x5c000、.01→0x6e2c000）。
- ❗ **但 536 仍返 22**（cachereg 已对 46 分片全部附加 blob；ubc/blob 均非空）。⇒ **22 可能不只来自 CS 门**（或内核取的是 blob 链上另一个）。
- **下一步二选一**：(a) 内核侧证据——把 `cs_system_enforcement_enable` 置 0（`_cs_system_enforcement` @ IDA `0xfffffe0008373a28`）确认是否就是它（注：该全局是 SECURITY_READ_ONLY，KRW 写可能失败/风险）；(b) 继续用户态差分（如给 mappings 设 VM_PROT_ZF 已试→变 14(EFAULT)，说明 CS 段确实被跳过）。
- 🔎 **2026-09-28 进一步实测**：`_cs_system_enforcement`（kern_cs.c）在 RELEASE 内核里是**常量函数 `MOV W0,#1; RET`** ⇒ 无法靠全局关闭；唯一用户态旁路是 mapping 的 `VM_PROT_ZF` 位。
- 🔎 **iosdsc 结构**：**split cache**，46 片总 3.2G；每片 `mappingCount=1`、`fileOff=0`；主片映射 `[0x180000000,0x58000]`。
- 🔎 **覆盖修改已生效且持久**（main 0x5c000 / .01 0x6e2c000 / .02 0x1c000 = 各自文件大小；blob+0x38 重读确认）。
- ❗ **覆盖满足、CS blob 俱在，536 仍 22** ⇒ 22 另有出处（待查；下一候选：`0x8459d04 未页对齐` / `0x8459d50 非VREG` / 上游 wrapper 映射 / 某片未被 CS 覆盖到的 mapping）。

**C. 下一步（定位 EINVAL 的具体门）**：535 setup = `shared_region_map_and_slide_setup`（kernel IDA `0xfffffe0008459570`，slide=0x158B4000）。EINVAL 组候选：`mappings 溢出`/`region==NULL`/`fd==-1 未对齐`/`非VREG`/`no memory object`/`mapping 未 code-signed`。**建议**：用 Instance2（kernel）反编译该函数并逐个比对我们的 files/mappings 输入；或对 setup 内各 EINVAL 赋值处下 KRW/断点读数。
- ⚠️ 注意：之前 subagent 测过 “空 region(sr_first_mapping==-1) ⇒ 536=12；非空 ⇒ 22”，与本次 22 的对应关系需一并核清（可能同一 race 门）。
- 构建：`python3 build_dyld.py dyld_plat.bin crossarch plataccept`（产物仅 2 处 4B 差异：`0x76270`、`0x35c24`）。
- 部署：按上面配方，`/bin/echo HELLO` → `child exited rc=0`（多次复现）。
- **无 env 与带 `DYLD_SHARED_CACHE_DIR=/iosdsc` 均能跑**；`DYLD_SHARED_CACHE_DIR` 确被采纳（改成不存在的 `/nope123` → `/bin/true` rc=127，行为变化）。
- **env 确实到达 dyld**（`DYLD_PRINT_LIBRARIES=1` → 101 行 `dyld[...]` 输出）。
- `cachereg_ios` 常驻已启动并对 `/iosdsc/*` 附加 CS blob：日志 `REG ... fcntl=0 ... READY ok=1`。

### ⛔ 仍未通（下一步）：**536 映射 iOS 缓存仍失败**
- 带 `DYLD_SHARED_CACHE_DIR=/iosdsc` 时 dyld 打印：`dyld cache '(null)' not loaded: syscall to map cache into shared region failed`（path 为 null）。
- 未出现 `different platform` 文案 ⇒ **plataccept 疑似已生效**（未在 preflight 被拒）。
- `e5centry/e5ccave`(0x76e04) errno 探针**未触发** ⇒ 536 stub 的 error 分支没走到，需换探针位置或确认是否真发了 536。
- **下一步**：① 确认是否真发起 536（用 `mkBentry`/`M` 系列 marker 或 536 前 dump）；② 若发了 536，取原始 errno（kernel 门表见 `shared_region_map_and_slide_setup` `0x8459570`，EINVAL 组/EPERM 组）；③ 注意 iOS 缓存有 46 个分片，`preflightMainCacheFile` 会按 header 的 cacheFileCount 依次 open 子缓存（缺一则 cacheFileFound=false）。

**runtime-confirmed，3 次复测全过。**

### 结论（不要再推导）

修改 dyld `__TEXT` 任意字节（含死区 NOP）后，能否过 exec 准入**只取决于签名配方**，与改动内容无关：

| 签名方式 | superblob | CD flags | 结果 |
|---|---|---|---|
| `ldid -Hsha256 -S<ent.plist>`（**无 `-Cadhoc`**） | 4 项（CD+req+XMLent+DERent） | **0x0** | ✅ HELLO ×3 |
| `ldid -Hsha256 -Cadhoc -S<ent.plist>` | 4 项同上 | **0x2(adhoc)** | ❌ SIGKILL(exec veto,dyld 零输出） |
| `resign_dyld.py`（保 superblob 只重算页哈希） | 4 项原样保留 | 0x0 | ❌ SIGKILL |
| `ldid -Hsha256 -S<ent>` 于**未改内容**的备份 | 4 项 | 0x2（带-Cadhoc时也work) | ✅ HELLO（见备注） |

**备注**：`dyld_bk_resign`（未改内容 + `-Cadhoc` → flags=0x2）反而能跑——
说明 flags=0x2 只在"内容被改"时才致命。别问为什么，经验规则就是：
**改 dyld → 用 `ldid -Hsha256 -S<ent>`（绝不加 -Cadhoc）→ flags 必须 0x0。**

### 反例澄清（旧记录要修正）

- 之前写"`resign_dyld.py` 保 superblob + 重算哈希仍 SIGKILL"——属实，但
  现在知道正确配方是 ldid 裸签，不是保 blob。保 blob 路径不要再走。
- `jbctl trustcache add` 需要 **40 hex 的 sha256(cd)[:20]**——不是全 64 hex，
  也不是 cdhash.py 的全量输出。截前 40 字符。
- trustcache info 输出是**大写 hex**——grep 必须 `-i`（之前误判"备份不在 TC"是
  因为小写 grep 大写列表）。备份 cdhash `b219dae7…` 实于 TC entry 290。

### dyld 准入完整配方（一条命令链）

```bash
# on device, file at /var/mobile/dyld_X.bin:
/var/jb/usr/bin/ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist /var/mobile/dyld_X.bin
# cdhash = sha256(CD blob)[:20] (40 hex):
/var/jb/basebin/jbctl trustcache add <cdhash40>
rm -f /var/mnt/rootfs/usr/lib/dyld && cp /var/mobile/dyld_X.bin /var/mnt/rootfs/usr/lib/dyld && chmod 755 /var/mnt/rootfs/usr/lib/dyld
# test:
/var/mobile/run_nocskill /var/jb/usr/bin/env -i PATH=/usr/bin:/bin \
  /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO
```

### 本路径解锁的下一步（交接给隔壁 AI)

死区字节能改 = **可以打 `plataccept` 补丁**(dyld thin `0x35c24`
`B.NE → NOP`)，让 dyld 接受 iOS 缓存 platform → `DYLD_SHARED_CACHE_DIR=/iosdsc`
+ iOS 缓存拷贝灌进 chroot region 的路可以测了。详见
`HANDOVER-DYLD-ADMIT-2026-09-27.md`。

---

## ★★★★★ 2026-09-27（续·傍晚）⭐⭐⭐⭐⭐ MILESTONE：发现 iOS 缓存采纳路径 —— 可能根本不需要 536

**本轮改写整条技术路线的认知**（runtime-confirmed）：

1. **chroot 任务 exec 时被内核挂上的是 iOS 自己的 shared region**。
   `__shared_region_check_np`(syscall 294) 在 chroot 的 macOS dyld 进程里
   返回 **iOS 缓存基址 `0x1A4AE8000`**（与原生 iOS 进程同一 region）。
2. **`reuseExistingCache` 直接采纳 iOS 缓存**：magic `dyld_v1  arm64e`
   匹配 → `re-using existing shared cache` 打印 → **89 个 iOS dylib 全部
   从缓存成功加载**（libSystem、libnetwork、Security.framework… +
   jb 的 forkfix/libinjector 从磁盘）。证据：`/tmp/priv.log`（部署
   `dyld_noslide.bin` + `DYLD_PRINT_LIBRARIES=1`）。
3. **上游原项目（MacWSBootingGuide, macOS 13.4+iPadOS 16.x）从未打过
   共享缓存补丁** —— README 的 dyld 补丁清单只有 GradedArchs
   arm64e→arm64 一项 ⇒ **原厂设计就是采纳 iOS 缓存**，macOS-only 库
   （AppKit 等）从 rootfs 磁盘加载。536/keeper/cachereg 这条线为
   HELLO 世界不是必需；它只对"用 macOS 原生缓存内容"有必要（后期
   再评估 macOS 独有 dylib 是否必须走 macOS 缓存）。
4. **syscall 536 在 region 已被 iOS 缓存填充的进程上返回 EINVAL(22)**
   —— 与新发现自洽（不能覆盖已占 region）。536 只在"首交者"
   路径上有意义（keeper 设计保留）。

**当前唯一 blocker（Hello World 距离 = 这一个崩溃）**：
库全部载入后、到达 app 入口前 **SIGSEGV(139)**。
`dyld_emark.bin`（`crossarch`+`entrymark`@0x6b94 BLRAAZ→`bl 0x970`+
`entrycave`@0x970 write 'E'+x8→blraaz）实测 **'E' 未出现** ⇒
崩在 **dyld 内部 post-cache 代码**（initializers/notify/bind 阶段），
不是 app/libSystem 初始化器。下一步 = segvcap handler 移植到
emark 构建（`dyld_segvcap.bin` 的 cave 在 `0x9b578`，
`segvcap_cave.s` 现成）→ 抓崩溃 PC+FAR。

**复现命令（已验证）**：
```bash
# 纯 chroot 路径（无 libmachook 注入）
/var/mobile/run_nocskill /var/jb/usr/bin/env -i PATH=/usr/bin:/bin \
    /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO
# launchdchrootexec 路径（注入 libmachook_arm64.dylib）
/var/jb/usr/macOS/bin/launchdchrootexec 0 0 /var/mnt/rootfs /bin/echo HELLO
```

**新事实表**：
| 事实 | 值 |
|---|---|
| chroot 进程的 task region | iOS region，基址 `0x1A4AE8000`（exec 时挂载） |
| check_np(294) 在 macOS dyld 里 | 返回 iOS base + magic `dyld_v1  arm64e` ⇒ reuse 采纳 |
| reuse 采纳后行为 | iOS 库从缓存加载成功；崩于 post-cache（139，PC 未取） |
| `launchdchrootexec` | chroot+setenv(DYLD_INSERT_LIBRARIES=libmachook_$ARCH)+posix_spawn\|SETEXEC；`arch=arm64` 时注入 arm64 变体 |
| `run_nocskill MACWS_EXC=1` | **会让父进程被 AMFI SIGKILL**（task_set_exception_ports 门）——勿用 |
| `dyld_segvcap.bin` | 已有 SIGSEGV-handler dyld（start@0x540c→cave 0x9b578）；但其补丁组合走 536-map→烧循环，须移植 handler 到 emark 基线 |
| chroot 崩溃 | **不写 .ips**（CrashReporter 路径外） |
| rootfs 布局（README 实证） | OS cryptex→`$R/System/Volumes/Preboot/Cryptexes/OS`；`$R/System/Volumes/Data`→`../..` 链接；`$R/var/folders/zz`→`/var/folders/zz`；bind `$R/var/jb`→`/var/jb`；每 exe `ldid -S ent.plist -M`+逐 cdhash `jbctl trustcache add`+`loadtc` |
| 构建器新 patch key | `entrymark`/`entrycave`（@0x6b94/@0x970，见 build_dyld.py） |

**非确定性警示**：同一二进制连跑会在 {139 快崩, 45s 烧循环, 137}
之间漂移——与 region/时序状态相关；结论前必须连测 ≥3 次。

### 补充记录（散点但重要，勿再推导）

- **check_np 返回值的语义**：返回的是**当前任务 region 里已映射缓存的基址**——
  iOS 缓存映射时 = `0x1A4AE8000`；早前 macOS 缓存被某次 536 成功填入时
  = `0x180000000`。出现过的"僵尸态" = base 有值（`0x180000000`）但读
  magic 立刻 SEGV（region 对象在、页未映射进本任务）。iOS 采纳路径则
  页全映射好（verboseSharedCacheMappings 实打印过全部段）。
- **烧循环也在采纳路径上出现**：dyld_es（crossarch+entrymark+segvcap）
  两次运行都是 93% CPU 烧 ≥14s、零 fd2 输出——post-cache 失败不止
  SEGV 一种形态，也会以死循环出现。
- **run_nocskill 的 task port 在 ~2s 变 INVALID_DEST(kr=268435459)**：
  `vm_region_64`/`task_threads` 全挂——spawn 后 2s 端口就失效（
  env→chroot→echo 连续 execve 后端口语义问题），外部采样 PC 不可靠。
- **stderr pipe 填满假象**：marker 往 fd2 写约 ~190KB 后会阻塞（父进程
  pipe 没人读）→ 看起来像内核卡死。用文件重定向而非继承 pipe。
- **内核侧 536 errno 真值表**（Instance2 RE，`sub_8062254` 包装）：
  内部码 {0→成功，1→EFAULT(14)，2→EPERM(1)，3→ENOMEM(12)，>3→EINVAL(22)}；
  返回 -1 的是 `sub_8459570`（vnode/文件设置段）。任务门：`task+0x18`
  的 region-root vnode 须 == 进程根目录 vnode 或 rootvnode，否则 EPERM；
  chroot 内 `task+0x18`==chroot 根 ⇒ 可过（536 ret=0 曾在 chroot 实证）。
  `task+0x3e8` = 任务持有的 shared_region 对象；`sr+0x71`=in_progress，
  另有 stale 位。
- **`mov w1,wsp`/`mov x1,sp` 等一切以 SP 为源的操作数在 SP 未对齐时全部
  SIGILL**(0xe1030091/0xe1030111 崩溃签名）——既是坑也是探针。
- **AMFI 拒绝 macOS 磁盘 dylib**：加 trustcache 后仍 `errno=1`——
  libSystem 不可能走磁盘 shim，必须走缓存（坐实采纳路径必要性）。
- **libmachook 为 13.4 dyld 内部结构所写**——launchdchrootexec 注入它
  进 15.6.1 dyld 进程时其 ctor/hook 可能不兼容；纯 chroot（run_nocskill
  路径）可排除它，二分崩溃时先排掉注入变量。
- **forkfix+libinjector 来源**：不是 libmachook——是 Dopamine ElleKit
  经继承的 `DYLD_INSERT_LIBRARIES` 注入；`env -i` 可清。
- **`DYLD_SHARED_REGION=private` 未定论**：设过 env 但进程仍采纳共享
  region（可能 launchdchrootexec/env 传递问题或该选项不适用此场景）——
  值得重试验证（私有 mmap 路径绕开共享区概念）。
- **cave 间距规则**：相邻 marker cave 必须 ≥ cave 实际长度（mkD@0xb00
  与 mkE@0xb40 间距 0x40 < 0x4c cave 长 → 尾部互踩、输出污染）。
- **echo 的 Mach-O**:fat、platform=1(macOS);`ldid -S ent.plist -M`
  重签 + 每 slice cdhash 注册（`add_all_trustcache` 流程）。
- **re-clear csflags 模式**:run_nocskill 打 `re-clears=2` = 每次
  execve(chroot→echo）内核重设 0x300,watchdog 以 ~20µs 粒度重清。
- **iOS 缓存文件**:`/private/preboot/Cryptexes/OS/System/Library/Caches/
  com.apple.dyld/dyld_shared_cache_arm64e[.01-.44]+.symbols`（本机实测
  ls 输出）；chroot 内同名路径是 rootfs 里的 macOS 文件，勿混。
- **chroot 工具真实路径**:`/var/jb/usr/bin/chroot`（不是 /usr/sbin)。
- **run_nocskill 是 posix_spawn 包装，本身不 chroot**——不 chroot 直接
  喂 `$R/bin/echo` = iOS dyld 加载 macOS 二进制 → "wrong platform" 报错
  （报错来自 iOS dyld，不是我们 dyld)。

---

## 2026-09-27 — ⭐ 交接 Executive Summary（重启后先读这段）

### 🎉🎉🎉 MILESTONE 2026-09-27 晚：macOS `/bin/echo HELLO` 在 chroot 里首次跑通（独立复现）
- **实测（我亲自跑，非仅 subagent）**：`/bin/echo HELLO` ×5 → 全部打印 `HELLO` + `child exited rc=0`；`/usr/bin/true` ×2 → rc=0；`/bin/echo one two THREE` → `one two THREE`（参数透传 OK）。
- **达成路径（subagent 实现 + 我验证）**：**磁盘回退 + 两个自包含替身 dylib**（不需改 dyld、不需内核 patch）：
  - `/var/mnt/rootfs/usr/lib/libSystem.B.dylib`（shim，导出 echo 依赖的 10 个符号：`_err,_exit,_fflush,_getenv,_mbtowc,_putchar,_putwchar,_strlen,___stdoutp,___mb_cur_max`，并 LC_LOAD_DYLIB→libdyld）。
  - `/var/mnt/rootfs/usr/lib/system/libdyld.dylib`（shim，满足 dyld 硬门槛：install-name 精确匹配 + `__TPRO_CONST,__dyld_apis` + `__DATA_CONST,__helper` 的 **ptrauth 签名 `dyld4::LibSystemHelpers` vtable**，`version()>=7`）。
  - 两者均 `ldid -Hsha256 -S<ent>` + `trustcache add` + `rm+cp` 新 inode + `chmod 755`。
- **与 private 路径的关系**：`DYLD_SHARED_REGION=private` 能成功 mmap 两个子缓存（`mapped dyld cache file private to process (...main.../01...)`）但随后 **SIGKILL(9)**（DSC 页 CS-enforced exec 页被击杀）⇒ private 路死；转磁盘回退+替身才成。
- **诚实定性**：这是让 **echo/true** 跑通的**桥接方案**（非“完整缓存映射”的根治路）；要看任意 macOS 二进制需继续补齐 libSystem 桩（malloc/getenv/…）与依赖 dylib。但**对本 goal 已达标且可重复**。
- **产物**：`analysis/dyldwork/tmp/shim/{libSystem_shim.c,libdyld_shim.cpp,build_shim.sh,deploy_shim.sh,run_repeat.sh}`（gitignored）。

### ★★★ 2026-09-27 晚 M-HW6（非侵入探针锤实）：reuse 入口 `check_np=12`，root cause 彻底闭合
- 工具：自建 `dyld_reuse_dump.bin`（在 `reuseExistingCache`@0x351a8 入口装 cave@0x38d08：**保存 x0-x3/x30 → call check_np → dump 64B → 恢复 → 重放 PACIBSP → b 0x351ac**，**完全不改变原流程**）。
- 实测（`/tmp/rd.bin`）：**`check_np ret=0x0c(12)`、base=0**。⇒ **macOS dyld 的 reuse 必失败**（源码 `:1254`）。
- 结合 `getDyldCache` 仅被 `ProcessConfig::DyldCache` 构造一次（`loadDyldCache` 每进程一次）⇒ 日志 line1 “re-using”= iOS dyld（launcher），91 × “Using mapping in dyld cache”= `Loader::logSegmentsFromSharedCache` 的**文件级元数据**（实际未映射）；856-857 “Mapping system wide / not loaded” = macOS dyld 那唯一次 `loadDyldCache`。
- **圆整后的根因（最终版）**：**chroot 进程的 shared region 为空（root_dir 键控）→ macOS dyld reuse 失败 → 536 映 macOS 缓存失败（门/冲突）→ 无真实缓存 → 主执行的依赖（libSystem/libutil/libdyld…）无法解析 → abort。**
- **可行修法（已收敛到两条）**：**① 内核**：让 `vm_shared_region_lookup` 忽略 `sr_root_dir`（slide=0x158B4000）→ chroot 复用 iOS region（已映射的 iOS 缓存，magic 就是 `dyld_v1  arm64e`）→ reuse 成功。**③ 用户态**：提供缺失符号/库，或让 `DYLD_SHARED_REGION=private` 生效（需开 `security.allowEnvVarsSharedCache`）——均需另找插入点。

### ★★★ 2026-09-27 晚 M-HW5（完整失败链已锤实，含源码）：Hello-World 进不去 main 的机制
- **870 行完整 trace**（`analysis/dyldwork/ft.log`，本地）关键：
  1. 行 **1** `re-using existing shared cache (/private/preboot/.../dyld_shared_cache_arm64e)` + 行 2-13 dump **`0x1A4AE8000` 起的映射**（=`check_np` 的 base！）；
  2. **91** × `Using mapping in dyld cache for /usr/lib/system/...`；
  3. 行 **856** `Mapping the shared cache system wide` → 行 **857** `dyld cache '(null)' not loaded: syscall to map cache into shared region failed`；
  4. 然后 `/bin/echo` + **磁盘** `/usr/lib/libSystem.B.dylib`(34KB) → **`Symbol not found: _err` → abort**。
- **源码定论**（`analysis/dyld-dyld-1286.10/dyld/SharedCacheRuntime.cpp`）：
  - `loadDyldCache`(`:1476`)：`forcePrivate ? mapSplitCachePrivate : (reuseExistingCache(:1490) ? ok : mapSplitCacheSystemWide)`。
  - `mapSplitCacheSystemWide`(`:1367-1387`)：536 失败后**再试一次 reuse**（“另一个进程抢先”），仍失败才置 `errorMessage="syscall to map cache into shared region failed"`。
  - `reuseExistingCache`(`:1251`)：`check_np==0 && validMagic(magic=="dyld_v1  arm64e")`。
- **机制推断**：macOS dyld 的 `reuse`（:1490）**失败**（chroot 的 region 为空：root_dir 不同→空 region→check_np=12）⇒ 走 536（macOS 缓存）⇒ 失败（**与已在 `0x1A4AE8000` 的 iOS 缓存地址冲突 → KERN_NO_SPACE/EINVAL**）⇒ 再无 cache ⇒ echo 的 libSystem 退磁盘 ⇒ `_err` 缺失 ⇒ abort。
- **⇒ 真正可行的两大方向（用户决断）**：
  - **① 让 chroot 看见 iOS region**（内核 `vm_shared_region_lookup` 忽略 `sr_root_dir`；运行时地址可由 slide `0x158B4000` 求得）→ macOS dyld reuse **iOS 缓存**（其 magic 已是 `dyld_v1  arm64e`）→ 91 库全从缓存 → 可能直达 main。风险：跨 iOS 进程污染（需评估）。
  - **② dyld 侧“无 region 时体面回退”**：hmm——源码已证不可强改（会把 loadAddress 指空）。
  - **③ （治标、但直击目标）让 `_err` 可得**：给磁盘 `libSystem.B.dylib` 补/拦截 `_err` 符号（甚至一个导出 `_err` 的小 dylib）——因为 echo 只在错误路径用 `err()`，**提供 `_err` 就可能让它直接打印 HELLO**。注意：此为诊断/桥接，不是根因修复。

### ★★★ 2026-09-27 晚 M-HW4：536 原始 kernel errno = **22(EINVAL)**；populate 未成功
- 工具与脚本（已就绪，官方 key）：`build_dyld.py dyld_pope.bin crossarch hasexisting prereuse filescount1 dynoff accessor fcntl_nop cover_b slidentry slidecave e5centry e5ccave`。
- 实测：populate 的 536 返回 **-1**；`e5c` 探针（挂在 536 stub 失败处）拿到 **raw kernel errno = 22**。
- 对照门表（subagent 输出，`shared_region_map_and_slide_setup` IDA `0xfffffe0008459570`）——EINVAL(22) 组候选：`mapping not code-signed`(`0x8459CBC` `ubc_cs_is_range_codesigned`)、`no memory object`(`0x8459CE0`)、`fp_get_ftype/非VREG/映射溢出`。
- **下一步 = 内核 KRW 短路该门让 536 成功一次**（→ region populate → check_np 返真实 base → dyld reuse 成功 → `_err` 从缓存解析 → echo 应能进 main）。**前置：kernel slide（已定位内核代码段运行时在 `0xfffffe001d7e…`）。**

### ★★★ 2026-09-27 晚 M-HW3（subagent 源码+IDA 双证，推翻“没挂 region”假说）
- **exec 总是挂 shared region**（`kern_exec.c:1558 __mac_execve → vm_map_exec → vm_shared_region_enter`，**无条件**）；**唯一判别键 = `p_fd.fd_rdir`(root_dir)**（`vm_shared_region.c:386-398`）。**chroot 子进程因 root_dir 不同 → 拿到该 root_dir 下的【全新空 region】**。
- **check_np=12 语义**：任务**有** region 但**空**（`sr_first_mapping==-1` → start_address 返 KERN_INVALID_ADDRESS → ENOMEM(12)）；错误时 **copyout 未执行** → 用户态 base 保持 0（故 magic 全 0）。源码 `osfmk/vm/vm_unix.c:2046-2134`。
- **⇒ 正确修法（不是“让内核挂 region”）两选一**：
  - **① 用户态**：`DYLD_SHARED_REGION=private` → `mapSplitCachePrivate`(`SharedCacheRuntime.cpp:865/984`) 普通 `mmap(MAP_PRIVATE)`，**绕开 294/536**。**实测失败**：本 boot `security.allowEnvVarsSharedCache` 未开 → env 被 AMFI 剥掉；且加该 env 后变 **`SIGNALED 9`(SIGKILL)**（疑 `:1519` “指定 private 但找不到 cache 文件→halt”或 AMFI 剥 env）。
  - **② 内核 KRW**：短路 `shared_region_map_and_slide_setup`(IDA `0xfffffe0008459570`) 的某道门，让 **536 成功一次** populate region → 之后 check_np 返真实 base → reuse 走快路径。门的地址表见 subagent 输出（code-sign 门 `0x8459CBC` / root_dir 门 `0x8459764` / 等）。**需解决 kernel slide（旧问题）。**
- **③ dyld 强改 reuse=恒 true = 有害**（subagent 与源码均证实：会把 loadAddress 指向空 region，更早崩）——**不推荐**。
- **原仓库（MacWSBootingGuide）无 shared-region 结论**（目标 macOS 13.4，未遇此问题）；其 rootfs/签名设计见 README.md:15-30/123-129/182-205。

### ★★★ 2026-09-27 晚 M-HW2（根因链第一环已锤实）：chroot 里 macOS 进程 **没有 shared region** → check_np=12
- 工具：`build_dyld.py dyld_cknp2.bin crossarch cknp2entry cknp2cave`（**官方 key，非手改**）；在 `loadDyldCache:BL reuseExistingCache`(0x34298) 处先 call `check_np` 并 dump `{ret,base,magic}` 40B 到 fd2。
- **实测（`/tmp/c2.bin`）**：**`ret=0x0c(12)`，`base=0x0`，magic 全 0**。
- **源码定论**（`analysis/dyld-dyld-1286.10/dyld/SharedCacheRuntime.cpp:1251-1281`）：`reuseExistingCache` 首句 `if(__shared_region_check_np(&base)==0)`；**返回 12≠0 ⇒ 直接 `return false`** ⇒ `mapSplitCacheSystemWide`(536) ⇒ EINVAL(22) ⇒ “cache not loaded” ⇒ 磁盘 `libSystem.B.dylib`(34KB dsh 缺 `_err`) ⇒ `abort()` **SIGABRT(6)**。
- **⇒ 真正的根因**：**内核没有给 chroot 的 macOS（非 platform）进程挂上 shared region** ⇒ macOS dyld 永远看不到可 reuse 的缓存。
- **修正旧记录**：handover §1 fact#1 “chroot 进程 check_np 返 base=0x1A4AE8000” 与本次不符（本次=12/0）——需复测分清探针差异。
- **下一步（排序）**：① 查为何 exec 未挂 region（对照 `analysis/xnu-*/osfmk/vm/vm_shared_region.c` 的 `vm_shared_region_enter/attach` 条件 + `kern_exec.c`）；② 很可能与 “非 CS_PLATFORM_BINARY” 相关（早前已证 `CS_PLATFORM_BINARY` 是 exec 判别因子）⇒ 让二进制被认作 platform / 或内核侧挂 region；③ 或 patch dyld 让 “无 region” 时优雅回退（非首选）。

### ✅ 2026-09-27 晚 M-HW1：Hello-World 崩溃真因 = macOS dyld `reuseExistingCache` 失败 → 536 → 磁盘 libSystem 缺 `_err` → SIGABRT(6)
- **复现**（3/3 一致，非 SEGV）：`run_nocskill ... chroot $R /bin/echo HELLO` → `child SIGNALED 6`；dump(279B)：
  `dyld: dyld cache '(null)' not loaded: syscall to map cache into shared region failed` + `Symbol not found: _err ... Expected in /usr/lib/libSystem.B.dylib`。
- **完整链**（`DYLD_PRINT_*`，870 行）：
  1. 行1 `re-using existing shared cache (...)` = **iOS dyld**（env/chroot 这些 iOS 二进制）；
  2. ~90 × `Using mapping in dyld cache for /usr/lib/...`（macOS 库从缓存解析）；
  3. 行856 macOS dyld `Mapping the shared cache system wide`（=`mapSplitCacheSystemWide` @IDA **0x355ec**）→ 536 失败；
  4. 行857 `dyld cache '(null)' not loaded` → **丢弃缓存** → 退回磁盘 `libSystem.B.dylib`(34KB dsh) → **缺 `_err`** → `abort()` SIGABRT(6)。
- **RE 定位**：`loadDyldCache`@**0x34240**：`0x34298 BL reuseExistingCache` → **`0x3429c CBZ W0, →0x342b8`（reuse 返 0 则 `0x342d8 B mapSplitCacheSystemWide`）**。`reuseExistingCache`@**0x351a8**：`0x351d0 BL __shared_region_check_np`→`CBZ`；`0x351f0 platform_strcmp(base,"dyld_v1  arm64e")`→`0x351f4 CBZ`；**不匹配 → `0x351f8 MOV W0,#0` + 存诊断串 `"existing shared cache in memory is not compatible"`(@0x90b12) → ret0**；匹配 → `0x352a4 MOV W0,#1`。
- **⇒ 根因候选**：macOS dyld 的 reuse 判定（magic 不匹配 / check_np）失败 → 走 536，而 536 在本环境 EINVAL(22) → 缓存被丢弃。**下一步**：① 查为何 reuse 判失败（reuse 内的 check_np 值与 magic）；② 或 patch `0x3429c CBZ` 分支使 reuse 成功时不回退；③ 或给磁盘 `libSystem` 补 `_err`（治标）。

### ★★★★★★★★ 2026-09-27（Devin 续·深夜）⭐⭐⭐⭐ MILESTONE：macOS dyld 越过 shared-cache 阶段，进入库加载

**当前 blocker 已迁移**：不再是 syscall 536 / mapSplitCacheSystemWide，
而是 **`libSystem.B.dylib` 在 chroot 内加载失败**（code signature
invalid）。dyld 的库加载管线已经真实运行到逐路径重试 + fatal(SIGABRT)。

**m7 运行实证（dyld_m7.bin, CDHash 2ba08622…）**：
`run_nocskill chroot /var/mnt/rootfs /bin/echo HELLO` → 探针 dump 第一趟
全正常（x23=15=映射数、lr=dyld+0x35668、savedLR=dyld+0x2fe94=getDyldCache
返回址），随后 dyld 自身输出：

```
dyld[6174]: Library not loaded: /usr/lib/libSystem.B.dylib
  tried: '/usr/lib/libSystem.B.dylib' (code signature invalid in
    <4DB5C3A0-…> /usr/lib/libSystem.B.dylib, sliceOffset=0x0,
    codeBlobOffset=0x00040080, codeBlobSize=0x00040540)
  tried: '/System/Volumes/Preboot/Cryptexes/OS/usr/lib/libSystem.B.dylib' (errno=2)
  tried: '/usr/local/lib/libSystem.B.dylib' (code signature invalid …)
[+] child SIGNALED 6   ← dyld 正常 fatal 路径
```

**为什么这次通了**：共享区已被早前某次成功 536 填充
（`sr_uuid=4c1223e5…`，`slide=0xd7b0000`）。本进程里 536 返回非 0
（x3=mappings 指针被探针弄坏→EFAULT），但 `reuseExistingCache`
（0x351a8）走 `__shared_region_check_np` 发现已填充 region → 返回 1
→ dyld 继续。**即：reuse 路径本身工作正常**。

**m5/m6 寄存器实证（此前"用户态死循环"机制）**：

- m6 dump 在 pack 外循环头（0x3561c）：`CacheInfo[0]+0x180=8`、
  `CacheInfo[1]+0x180=7`、合计 15 = w23 ——**计数字段全部正常，
  pack 循环无辜**。
- m5 dump 在 0x35690 抓到 `x23=0x1024d8000`（另一趟 0x1044a0000）
  = **dyld 基址形态** → 该 dump 是**第二趟**经过 0x35690
  （第一趟 `MOV X23,X0`@0x35698 后 x23 被复用）。→ **post-536
  返回路径存在重入边**落回 ~0x35660-0x35668（append 块）；第二趟
  append 用 w23=基址低 32 位（0x24d8000≈38.8M）把映射记录写到
  x26+1.16GB ≈ 0x1b39271d0 ——**落进已映射共享区（0x180000000-…）
  不触发 fault**，之前观察到的"45s 用户态烧"即此类重入循环
  （每次重入 = append+536+post 一趟）。
- 重入边静态仍不可见（无 backedge 覆盖 0x3565c-0x35698）；候选机制：
  epilogue `0x35718 MOV SP,X8`（`[x19+0x18]` 恢复的 SP）/ RETAB 落到
  被踩的 x30。**但当 region 已填充时无关紧要——536 失败也走 reuse 成功。**

**教训（写探针的正确姿势）**：
- 有效：`write(2,regs,N)` 在 mapfn 深处（0x355fc+）安全；`x15`/`sp±0x50`
  做暂存；用 `b`（非 `bl`）跳 cave，尾部分支人工算 `target-(cave+len-4)`。
- 会崩：`__dyld_start`/`start()` 早期上下文里 svc write → SIGBUS。
- `_mkmark` 的 movz/movk 单字母标记有编码坑（`0x528a4a09`='P\x52'≠'PE'）。
- stp/madd 手编易错——**一律 python 现算**（见 build_dyld.py 底部注释）。

**新 blocker（下一步）**：`libSystem.B.dylib` 签名校验失败。
dyld4 报 `code signature invalid` —— 候选原因：(a) rootfs 里的
libSystem 是 thin slice/被重签坏（对照 dyld_shared_cache 内嵌的
libSystem 版本）；(b) dyld 的 `validateDyldCache`-style 自检要求 cdhash
匹配共享区映射的版本；(c) 正常路径本应**从共享区直接取**libSystem
（不需要落盘读），cache 未完全可用才 fallback 到磁盘文件 → 检查
`hasValidCache`/loadInfo 的标志位（`[X20,#0x19]` reuse 里写）是否置位。
**先用 IDA 找 `code signature invalid` 串的调用点，看校验条件。**

---

### ★★★★★★★ 2026-09-27（Devin 续）⭐⭐ 536 实证成功；卡点=映射后用户态循环（已解明机制）

**一句话**：syscall 536 `__shared_region_map_and_slide_2_np` **在 macOS
15.6.1 dyld 里能跑通且返回 0**——证据双保险：retcave 抓到 ~590 万次
`x0=0` 返回（47MB 零字节洪流）+ KRW 读 task map 显示共享区子映射已填
~30 个映射（`sr_uuid=4c1223e5…`=macOS cache，`slide=0xd7b0000`，
`first_map=0`，`in_prog=0`）。**之前所有"536 阻塞/EINVAL"方向全部作废**。

**当前卡点（未决）**：536 成功后 dyld 在 `mapSplitCacheSystemWide`
返回路径附近进入**用户态死循环**（fs_usage 静默=纯计算，看门狗 45s
才杀）。`spindump` 采样得到两个确凿 PC：

- pid 3394（遗留）：`dyld+0x38d28` = **NOP sled 内部**（0x38d08-0x38d3c
  padding，其后 0x38d40=`dyld_program_minos_at_least`）——PC 如何进
  padding 未解（无静态 xref；必为 BR/RET/computed 跳入）。
- pid 5430：`dyld+0xcb0` = **mkG cave 的 `svc #0x80` 内部**——write(2)
  阻塞在内核（stderr/pty 缓冲被前面 ~190K 次 'D' 标记写满）→ 证明流程
  真实走到了 0x35668（G 点，mappings 循环之后）。

**marker-breadcrumb 结论链**（写探针：B@0x35380/C@0x35434/D@0x355fc/
E@0x3561c/F@0x3562c/G@0x35668，全部 `write(2)+replay+b`）：

| 运行 | B | C | D | E | F | G | 结果 |
|---|---|---|---|---|---|---|---|
| mEFG2（含 mkD） | 1 | 1 | **~95K 次** | 0 | 0 | 0 | D 洪泛→pipe 满→G-cave svc 卡 |
| noD（去 mkD） | 1 | 1 | — | 0 | 0 | 0 | **47MB `00` 洪流**=retcave x0=0 |

- 'D' 在 0x355fc 重复 ~95K 次但**该点函数内无回边**（backedge 表：
  353a8/353e4/35538/3562c/3561c/356b4/35714）→ 要么 mapfn 被反复调
  用（但 B@入口只触发 1 次！）要么 `44 0a 00 00` 不是 D 标记——
  **未解之谜**。mkDlr 证实 x30=base+0x355d8 只是 chkstk BLRAA 残值。
- noD 的 47MB 零字节 = **每 8 字节一次 `x0=0` 的 retcave 写 ~590 万次**
  = **536 被调用了 ~590 万次且全部返回 0** → 确实存在一个
  `[…→0x35690 536→0x35698 ret→…→回绕]` 的巨型重试环，但环结构未
  定位（post-536 代码无到 0x35690 的回边；reuseExistingCache 内无递归）。

**新铁律（hard-coded，血换来的）**：

1. **`write(2)`/svc 探针在 dyld 早期上下文必死**：`__dyld_start`(0x47c0)
   和 `start` 入口(0x53dc) 的 write-cave → 秒 SIGBUS/SIGILL 零输出；
   同 cave 只 replay 不写 → 正常跑。write 只在 dyld 自身 init 完成后
   （mapfn 区域 0x35380+ 起）才安全。**早期打点别用 syscall 探针**。
2. **cave 间距必须 ≥ cave 长度**：mkE(0xb40) 与 mkF(0xb80) 曾重叠
   0x40 间距 < 0x4c 长度 → E 尾部被 F 覆盖。死区 `0x970-0xfff`
   （thin slice 全零已验证）+ `0x3b394`(60B) + `0x47290`(56B) 可用。
3. **`_le()` 输入必须是指令字序 hex**（如 `1400d32c`），不是文件序——
   曾把 `36d30014` 当字序喂进去 → 落地成 TBZ 乱指令 = v2/v3 怪异死法。
4. **每次重签后才部署**：`scp` 原地覆盖同 inode 会让 vnode 缓存旧 blob
   → 秒 SIGBUS；先 `rm` 再 `cp`。`ldid -Hsha256 -Cadhoc -S` →
   `jbctl trustcache add $(ldid -h|grep CDHash=|cut -c8-)`。
5. **spindump 可用且给真 PC/栈**：`/usr/sbin/spindump`（采样全系统，
   忽略 pid 参数）→ `/tmp/spindump*.txt` 里按 Process 名找段。kernel
   帧带 `*`。chroot 进程也采得到——**第一仪器，别再瞎猜 PC**。
6. `task_threads`/`mach_vm_region*` 经 task_for_pid 端口=INVALID_DEST
   （iOS vm_map_read_t 门）；KRW 直读 `task+0x590`=threads 队列，
   `task+0x28`(PAC)→map，`ro+0x8`(PAC)→task，`proc+0x18`=ro。

**函数地图（thin slice 基线 `analysis/dyld_15.6.1_arm64e_thin`，IDB=Instance1）**：

- `mapSplitCacheSystemWide` = `0x352bc-0x3576c`（不是 0x35380！）
  - `0x3537c BL preflightMainCacheFile`；`0x353e4-0x35430` subcache
    preflight 循环（每 CacheInfo 0x1C0，调 `preflightSubCacheFile`）
  - `0x35438 BL DynamicRegion::make`；`0x35510/0x355d4 BLRAA chkstk`
    （files[] 12B×w28 / mappings[] 0x30×w25 两个 alloca）
  - `0x35538-0x35564` files[] 打包循环（`files[i]={fd,slide(i==0?slide:0),
    count}`——**dyld 本来就只给 files[0] 写 slide，"subcache slide 未
    清零"假说已被 IDA 反汇编证伪**）
  - `0x355f8 CBZ W28→0x35660`；`0x3561c-0x3565c` mappings 打包
    （外 x8<x27，内 w14=CacheInfo[i]+0x180 映射数，0x30 拷贝）
  - `0x35664 BL DynamicRegion::size`；`0x35690 slidentry→slidecave2`；
    `0x35694 BL __shared_region_map_and_slide_2_np`；
    `0x35698 retentry→retcave`
  - `0x356a0 BL DynamicRegion::free`；`0x356b4-0x356cc` close(fd) 循环；
    `0x356d8 BL reuseExistingCache`→成功返 1，失败 w0=0+errstr
- `loadDyldCache`=`0x34240`：options+4==1→`mapSplitCachePrivate` 尾调；
  else `0x34298 BL reuseExistingCache`（**被 prereuse patch 改 movz 0**
  →强制 map 路径）→ `0x342d8 B mapfn` 尾调。
- `getDyldCache`=`0x2fe34`（BL loadDyldCache@0x2fe90）；caller=DyldCache
  ctor `0xbed4`。`reuseExistingCache`=`0x351a8`（`check_np`→uuid strcmp
  →`dynamicRegion()`(0x50dfc=accessor patch)→fileID 比对）。

**下一步二分**（恢复后继续）：①post-536 重试环结构未定——把
marker 放到 0x356dc/0x356f4/0x35710/0x35754 各分支点 + `free`/`close`/
`reuse` 各 callee 入口，看循环覆盖哪段；②0x38d28 NOP-sled PC 来源
未解（找 `off_9C038` GOT 被写坏 / BR x16 落点）；③47MB 零写本身也
可能是单次 `write(2,zerobuf,huge_count)`——需分辨。

### ★★★★★★ 2026-09-27（原 AI 续 2）⭐ 墙 B 已解——纯用户态 `run_nocskill`，零内核写

**决定性实验结果**（全在设备实测）：

| 用例 | 结果 |
|---|---|
| `hellocs`（sha256 签+TC）直接跑 | `r=33333326` rc=0 ✅ |
| `hellocs_bad`（**签名后翻 __text 字节**，cdhash 不变）直接跑 | **rc=137 SIGKILL-CODESIGNING**（Invalid Page）✅ 复现墙 B |
| `hellocs_bad` 经 `run_nocskill` | **`r=f041b330`（篡改指令真实执行）+ rc=0** ✅ 墙 B 攻破 |

**机制（repo:`misc/run_nocskill.c` + `run_nocskill.entitlements.plist`，设备 `/var/mobile/run_nocskill`）**：

```c
posix_spawn(pid, target, NULL, attr=POSIX_SPAWN_START_SUSPENDED, ...)  // exec 完、旗已置、用户代码未跑
proc = find_proc(pid)                      // pidhash: TBL@0x...1D22B4D0 / MSK@0x...1D22B4D8, chain proc+0xA0, pid@+0x60
ro   = kread64(proc + 0x18)                // p_proc_ro
kwrite32(ro + 0x1C, kread32(ro+0x1C) & ~0x300)   // p_csflags &= ~(CS_HARD|CS_KILL)
task_for_pid(mach_task_self(), pid, &tp);        // 需 task_for_pid-allow entitlement
task_resume(tp);                               // LEGACY release 也减 user_stop_count → NORMAL hold 亦可解
waitpid(pid, &st, 0);
```

**结构事实（xnu-8792.81.2 `bsd/sys/proc_ro.h`）**：`proc_ro{pr_proc@0, pr_task@8, p_uniqueid@0x10, p_idversion@0x18, p_csflags@0x1C}`——`p_csflags` 在 proc_ro 里，之前叫的"cs_blob"其实是 proc_ro。

**关键事实**：
- spawn 挂起 = `task_suspend_internal`（`place_task_hold NORMAL`）；`task_resume`（MIG impl）`release_task_hold LEGACY` 对非-PIDSUSPEND 模式**无条件减 `user_stop_count`** → 可解 NORMAL hold（源证 `osfmk/kern/task.c:3659-3760`）。
- `task_for_pid` root 也会被 MACF 拒；**`task_for_pid-allow`（或 `com.apple.system-task-ports`）entitlement 在 ldid-sha256 签的二进制上被 AMFI 兑现**（实测 kr=0）。
- `pid_suspend`/`pid_resume` 是 PIDSUSPEND hold——**解不了** spawn 的 NORMAL hold，别走那条路。
- `ptrace` PT_TRACE_ME/PT_ATTACH 在 iOS 全 EPERM。
- kcall 需要 `com.apple.security.exception.iokit-user-client-class`+`IOSurfaceRootUserClient` entitlement（python3.9 无 → kcall 不可用；launcher 已签上备用）。
- kernel `__TEXT` 写入会挂死（KTRR）——**别写内核 text**；本方案只写数据字段 `p_csflags`（非 PAC）。
- 真死代码洞 `0x38d08-0x38d3c`(56B)（探针可用）；`0x3576c-0x35afe` 是活代码（注入 blob），IDB 里 0x35754 字节被旧 patch 污染，真值看 `dyld.orig`。

**签名公式**：`ldid -Hsha256 -Srun_nocskill.entitlements.plist`（不加 `-M`）→ `jbctl trustcache add $(ldid -h x|grep -o 'CDHash=[0-9a-f]*'|cut -d= -f2)` → `chmod 755`。entitlements 必备 `task_for_pid-allow`+`get-task-allow`+`platform-application`+`no-sandbox`+iokit-user-client。

**剩唯一墙 = 墙 A（dynregion 0x1f8000000 随首 mapper 退出消失 → 后续 exec 的 `hasExistingDyldCache`/`reuseExistingCache` 在 `check_np(NULL)`/dynamicRegion() 处 SEGV 139）**。两个候选解：① 每 exec 自愈（把两个早 deref 点改返 0 强制走 map 路径，验证 536 在已填充 region 上可重提交）② keeper 常驻进程持有 region。

### ★★★★★ 2026-09-27（原 AI 续）双补丁点运行时地址已铁证 + slide 统一更正

**前提更正（推翻隔壁"slide 不匀"结论）**：`runtime_text = IDB_addr + 0x158B4000` 对 **kernel `__text` 和 `__TEXT_EXEC` kext 文本全部成立**。隔壁的 slide 矛盾是把 `proc+0x180`（数据/堆指针）混入推算所致。现在**任何内核 text 地址可直接换算**（本 boot）。

| 靶点 | IDB | 运行时（已逐字节验证） | 指令 | 补丁 |
|---|---|---|---|---|
| C1 | `0x92a69fc` | **`0xfffffe001eb5a9fc`** | `ORR W8,W8,#0x300` | `0xD503201F` NOP |
| C2a | `0x8373868` | **`0xfffffe001dc27868`** | `TBNZ W27,#9` | `0xD503201F` NOP |
| C2b | `0x8373874` | **`0xfffffe001dc27874`** | `TBZ W1,#8` | `0x1400000A` B +0x28 |

- **验证方法**：IDB 全库签名唯一性（`py_eval find_bytes`）+ 设备端 `kread32` 逐字节复核——C2 20/20 字、C1 11/11 字全匹配（含函数头 PACIBSP）。
- **签名**：C2=`b278010a 7100013f 9a8a011b 374800db 52800016 aa1b03e1 36400141`（IDB 唯一命中 0x837385c）；C1=`e80040b9 08051832 e80000b9`（IDB 唯一命中 0x92a69f8）。
- **扫描器**：`/var/mobile/{kscan_c1c2.py,kscan_c1.py,kverify_c1.py,kverify_c2.py}`（重启后地址会随 KASLR 变——**必须重扫重验再写**）。
- **语义**：C1 NOP 后 `_vnode_check_exec` 不再置 CS_HARD|CS_KILL → `cs_invalid_page` 的 KILL/HARD 分支全不触发 → 全局不再因 "Invalid Page" SIGKILL（诊断级、全局弱化，已获用户授权试写）。
- **未写**：KTRR 风险仍在——写 kernel text 可能 panic（用户已授权冒险）。

### ★★★ 2026-09-27 深夜 3×subagent 大包围：两墙定性 + 具体补丁地址（最高优先）

### ❗❗ 2026-09-27 晨 重大更正："echo 活/sleep 死" 是【测量假象】
- 【事实】（subagent 控变量实验）**"703–792 行 vs 2 行" 100% 由是否设 `DYLD_PRINT_*` 决定**：开了就喷~700行，关了就只有 launcher 2 行。同环境下 `echo ≡ sleep`（逐字节，仅 target 名不同）。**我那句“暖机 echo(703) 后 sleep(2行)”是错：echo 开了 PRINT、sleep 没开。**
- 【事实】`next_boot.sh` 的 `try_keeper` 原先**没开 PRINT → 误判 keeper 死亡**（已修：try_keeper 现带 PRINT）。
- 【事实】真实二档 = 签名：① 项目 ent+arm64+TC 的二进制（echo/true/ls/sleep/bash/cat）→ **全到缓存映射(703–792) → 再死在 dynregion(139)**；② Apple 原版 ent（如 `mobileassetd`）→ exec 就被 AMFI 杀(137)。**修②：`ldid -Hsha256 -S<项目ent>`（不加 `-M`！）-M 会合并保留 Apple ent → 仍死；去 -M 实测 782/0→792/1。**
- ⇒ 历史上“sleep 死”真身 = 当时它还是 arm64e（已被 arm64ify 修）。**⇒ 剩下唯一 CLI 墙 = dynregion(139)，连 mapper 自己也崩。**
- ❗ 实验失败记录：`dyld_nodyn.bin`（dynamicRegion→NULL + 中和 3 个 ExternallyViewableState）→ **反把 dyld 弄坏**（693 行、无 cachemap、死；且 ldid 报 “Are you sure that is a Mach-O?”）⇒ **NULL 不容忍，此路不通**。下一步改走：让 dynregion **真实存在**（keeper 或 m8 改文件映射），而不是强制 NULL。
**墙(b) `SIGKILL-CODESIGNING/Invalid Page`（rc137、“2 行静默死”）= 内核 AMFI 无条件置 CS_KILL**
- **★ 更正（2026-09-27）**：`kc_raw_16.3_T8112.bin` **文件名误名**——它含 `_apciecT8103` 等 T8103 特征，**就是本机内核**（设备=`iPad13,11`/`J523tAP`/`RELEASE_ARM64_T8103`，xnu-8792.82.2）⇒ **IDB 偏移有效**。
- **KRW 可用**：`kread32/64`、`proc_self`；`proc_self=0xfffffe1217…`（堆）。**`proc+0x180=0xfffffe002033cb10`、`proc+0x18=0xfffffe1133682080`（内核镜像区指针）可作 slide 锚点。**
- **❌ 仍缺：kernel 运行时 slide**。假阳性教训：`_memset_s+0x17000000` 看似序言，但 IDB `_memset_s[0]=0xd503237f` 而那里是 `0xa9017bfd` ⇒ **dry-run（`kpatch_c2.py`）挡住了误写**。用 `cs_invalid_page` 指纹在 `[0x19000000,0x1a200000)` step4K **未命中**。
- **下一步**：用 `proc+0x180` 的 IDB 字段语义（或读 IDB `struct proc`）一步定 slide → 再用 C2/C3 指纹校验 → 才写。**未确认前绝不写内核**。
- 【事实】`_vnode_check_exec`@`0xfffffe00092a69e8`，指令 **`0xfffffe00092a69fc  ORR W8,W8,#0x300`（CS_HARD|CS_KILL）在每次 exec 无条件置位** → 任一 taint/未验证可执行页 → `vm_fault_validate_cs`→`cs_invalid_page`(0xfffffe0008373778)→`threadsignal(SIGKILL)` → “Invalid Page”。
- 【事实】dyld **无自校验**（导入无 `csops`/`cs_*`）⇒ 墙(b) **不能靠改 dyld 代码修**（只能签名侧或内核侧）。
- **内核侧修法**：C1（最直接）`0xfffffe00092a69fc` 的 `ORR #0x300`→`NOP`（全局去 CS_KILL）；C2 `cs_invalid_page` 的 `0xfffffe0008373868 TBNZ`→NOP + `0xfffffe0008373874 TBZ` 恒跳软路径；C3 `cs_validate_hash`(0xfffffe00084035ac) no-hash 路径改 validated。风险：全局削弱签名。
- **用户态修法**：`ldid -Hsha256` 重签（默认 SHA1→SHA256）实测已使首交 782→792（越过 `__dyld_start` 击杀）。cave **无 CS 优势**（在 `__TEXT` 内、落在 codeLimit≈0x128210/297 页覆盖内）。

**墙(a) `SIGSEGV@0x1f8000000`（dynregion 消失）精确补丁**
- 【事实】崩在 `dynamicRegion()`@`0x50dfc` 第 3 条 `LDR X9,[X8]`（读 `0x1f8000000` magic）。10 调用点中 **4 组容忍 NULL**（`start`@0x6380、`evaluateFunctionVariantFlags`@0x95c0、`hasExistingDyldCache`@0x30178、`reuseExistingCache`@0x35254）；**3 个 `ExternallyViewableState` 不容忍**（`0x4a964/0x4b9a8/0x4c17c` 直接 `cachePath(dynamicRegion())`，`cachePath`@0x51358 也不判空）。
- **补丁 1**：`0x50dfc`→`mov x0,#0`(`00 00 80 D2`) + `0x50e00`→`ret`(`C0 03 5F D6`)；**需搭配补丁 2**：`0x4a95c/0x4b99c/0x4c170` 的 `CBZ`→`B`（无条件跳过，目标 `0x4a9ec/0x4ba54/0x4c264`）。更稳替代=keeper 保活。

**amfid（Instance3）** 【事实】`amfid_bin` 只注册 MIG base=1000；**内核 exec upcall 27001 的 server 不是 amfid_bin**（修正旧推断）；exec 放行 = ①ARM64/ALL ②执行位 ③cdhash∈trustcache → 内核直置 `CS_SIGNED`（`kern_exec.c:7430` 跳过 upcall）。

### 当前 TODO（下个对话先看这里）
1. ✅ exec 门/字节序/launcher 毒丸 — 已修。
2. ✅ 536=EINVAL 真因=缓存 slide-info v5；`cachereg`+掩0x20+slide=0 → ret536=0。
3. ✅ 候选 dyld（`dyld_noslide`/`dyld_noslide_reuse`）+ `post_reboot_noslide.sh`/`catch_segv.sh`。
4. ✅ reuse 设计（`dyld_noslide_reuse`）。
5. ✅ 冷启动首交：**536 映射成功 + dyld 从缓存加载 libSystem/libobjc 等**（`analysis/dyldwork/t1_final_trace.log`）。
6. ⏳ **免重启**：每测必重启很痛。**实验结论（2026-09-27）**：① “坏 fd 的 536”在映射前就失败→不触发 undo；② “m8.size=-1 的 536”（想先映 8 条再在第 9 条失败）在 **populated region 上被早期拒绝（22）** → 也无 undo。⇒ **536 无法复位已填 region**。可行替代：① 每 boot 一测（`/var/mobile/run_all_cold.sh`）；② KRW 直清 region（高风险，未做）。`vm_shared_region_undo_mappings`(`vm_shared_region.c:1295-1310`) 仅用于内核内部回滚。
   - ✅ 附带确认：populated 后子进程 `check_np` 返回 **`base=0x180000000`** = 缓存真映在此。（探针：`dyld_reset536.bin`/`dyld_reset536_v2.bin`）
10. ⏳ **【重启批处理计划】一次冷启动跑 `/var/mobile/run_all_cold.sh`，一次拿全：**
   - A `A_first`：空 region 首交全量 DYLD_PRINT(含 SEGMENTS) trace。
   - ✅ **免重启已全部做完（2026-09-27）**：全部变体重签为 **SHA256**；`run_all_cold.sh` 加固（8s 硬超时 + SEGMENTS）并 **dry-run 完整跑通（不挂死）**；当前 boot 上复用路径实测：trace 止于**缓存 8 段打印之后**（`re-using existing shared cache` 后）→ **SIGKILL(137)**（非 SEGV，handler 捕不到）。
   - B `base/noverb/noreuse/reusemin`：复用路径四个隔离变体（`dyld_v_*.bin`）。
   - C `C_segvcap`：`dyld_segvcap.bin`（SIGSEGV-handler cave，真 139 时 dump pc/far）。
   - D `sha1/sha256/ent256`：**验证 CS 击杀假说**（`dyld_segvcap.bin`=SHA1 vs `dyld_segvcap_s256.bin`=SHA256 vs `dyld_segvcap_ent.bin`=SHA256+entitlements，均含 segvcap）。若 SHA256 版不再被 CS 击杀 → 根因确定。
   - ⚠️ 2026-09-27 实测：**本 boot 上三签名表现一致**（均 2 行即挂住/早崩、无 macOS dyld 输出、无新 .ips）⇒ **哈希差异需干净冷启动才能观测**（本 boot 已退化：早前 92 行崩→现直接挂住）。
   - **✅ 2026-09-27 DRY-RUN（完整跑通 `run_all_cold.sh`，无挂死）：** 结果 **`D_sha256`/`D_ent256` = 792 行且到达 `Mapping the shared cache system wide` + reuse；`C_segvcap_sha1`/`B_*` = 782 行**。⇒ **SHA256 签名让 macOS dyld 越过了早期 `__dyld_start` 的 CS 击杀、走得更远**——方向坐实；后续 137 是**更后面另一处**。脚本已验证（本地=设备、依赖全齐、8s 硬超时、开 SEGMENTS）。
7. ⏳ **定位/修复复用路径 SEGV**（候选 `reuseExistingCache` 0x351a8 / `verboseSharedCacheMappings` 0x3598c；需 PC——chroot lldb 自崩、无 debugserver）。
   - **2026-09-27 发现**：**已填 region 的 boot** 上跑任何 dyld（含变体）都**停在 91 行 / ellekit `libinjector.dylib`**（launcher 的 iOS dyld 阶段，rc139/137）——**≠ 冷启动的崩溃**（后者才走到 macOS dyld 深处）。
   - ⇒ 复用路径隔离变体（`dyld_v_noverbose.bin`：`verboseSharedCacheMappings`→RETAB；`dyld_v_noreuse_tail.bin`：`reuseExistingCache` 0x3525c→0x352a4）**必须在冷启动首交后立即测**（用 `run_all_cold.sh` 的 B 段）。
   - 调用链：`loadDyldCache`(0x34240) → `reuseExistingCache`(0x34298) → 若返 0 → `mapSplitCacheSystemWide`(0x342d8)。上层：`SyscallDelegate::getDyldCache`(0x2fe34，reuse 成功后调 `DyldSharedCache::getUUID`+`kdebug_trace`) → `loadDyldCache`。
   - **2026-09-27 trace 细读（关键）**：`t1_final_trace.log` 792 行里，**1-782 行是 launcher 的 iOS dyld**；**783-792 行才是 macOS dyld**：`Mapping the shared cache system wide`(783) → `re-using existing shared cache ((null))`(784) → **`verboseSharedCacheMappings` 打印出缓存完整 8 段（785-792）**：`0x180000000->0x1E7F5BFFF __TEXT`、`__DATA_CONST`、`__DATA`、`__TPRO_CONST`、`__AUTH`、`__AUTH_CONST`、`0x1F9070000 __READ_ONLY`、`0x1FED4C000->0x22560BFFF __LINKEDIT`。⇒ **reuse 成功、缓存段布局与 prot 均正确**；**崩溃在 reuse/getUUID 返回之后**（dyld 的 post-cache 流程：cache-restart 或首个 dylib 启动）。
8. ⏳ 跳过 slide 的 **rebase 验证**（slide=0 下指针是否真正确）。
9. ⏳ 推进 **WindowServer/VNC**（AGX 桥接为后续大工程，见 AGENTS.md）。

### 测试环境（每次冷启动重复）
- SSH `root@192.168.64.1 -p 2222`（密码 cisco）；region 一旦填满 → **必须重启**。
- 冷启动恢复：trustcache add cache cdhashes + `chmod 755` 所有要用二进制 + `nohup /var/mobile/cachereg <cache> <cache>.01 &`。
- 一键：`sh /var/mobile/post_reboot_noslide.sh`（首交+reuse）；`sh /var/mobile/catch_segv.sh`（lldb 抓崩，目前 chroot lldb 自崩）。

### 2026-09-27 00:4x — ★★★★★ 里程碑：首交成功映射缓存（536=0），进程在下游 SEGV
- 冷启动空 region + `cachereg` + `dyld_noslide` → 跑 `/bin/echo`：**无 `syscall to map cache into shared region failed`**（= mapSplitCacheSystemWide 未报错），但**随后 `Segmentation fault: 11`（rc=139）**，无 .ips。
- 随后同 boot 用 `sf_ns`（掩码捕获探针）测得 **`ret536=22`**（region 已被首交填满）⇒ **首交的 536 确实返回了 0 并映射成功**（区域被填充）。
- ⇒ **536 映射这面墙已被跨过**；新的卡点是**下游 SEGV**（候选：跳过 slide 导致缓存 __DATA 的 rebase 指针未处理；或 dynregion deref；或注入的 libmachook ctor）。

**2026-09-27 00:4x 更新 — 冷启动 DYLD_PRINT trace（`analysis/dyldwork/t1_final_trace.log`，792 行）彻底改写了结论：**
- **macOS dyld 成功用了缓存！** 日志中大量 `Using mapping in dyld cache for /usr/lib/libSystem.B.dylib`、`/usr/lib/libobjc.A.dylib`、`libdispatch/libdyld/libc++/libcache/...`，还有 `Kernel mapped .../launchdchrootexec`、`__SHARED_CACHE (rw.)`。
- 末尾两行：**`Mapping the shared cache system wide`**（= `mapSplitCacheSystemWide` 0x35704 成功路径！）+ **`re-using existing shared cache ((null)):`**（= `reuseExistingCache` 0x35280 的打印，参数是 `DynamicRegion::osCryptexPath()`）→ **然后 SEGV(139)**。
- ⇒ **536 映射成功 + 缓存内 dylib 加载成功**；崩溃在 **macOS dyld 第二次 `reuseExistingCache` 的后续**（日志止于 0x35280 的 print；下一句即 `verboseSharedCacheMappings` 0x35290）。
- `reuseExistingCache`(0x351a8) 反汇编：`X19=base` → `strcmp(base,"dyld_v1  arm64e")` → 匹配则 `loadAddress=base; slide(); dynamicRegion(); getDyldCacheFileID(); osCryptexPath()` 打印；`(%s)=(null)` 是 `osCryptexPath` 返回 null（打印本身无害）。**下一步靶子：第二次 reuse 的 base/`verboseSharedCacheMappings` 为何 SEGV**（需冷启动 lldb `catch_segv.sh` 取 PC，或比对第二次 reuse 的 base 是否 null）。
- 下一步：① 用 lldb（`MACWS_SUSPEND_AT_EXEC=1` + `misc/lldb_*`）在冷启动首交时抓 SEGV 的 PC/far；② 判断是否 rebase（若 PC 落在缓存 __DATA 上）——若是，则需真正处理 v5 slide（内核 backport 或重生成 v4/4K 缓存），而非仅跳过。
- **源码分析（`dyld/SharedCacheRuntime.cpp:382-388`）**：dyld **仅在某 mapping 的 `slideInfoFileSize != 0` 时**才给它 `sms_{init,max}_prot |= (VM_PROT_SLIDE(0x20) | authProt)`。⇒ `0x20` 源于缓存的 `mappingWithSlide` 表（`dyld_cache_mapping_and_slide_info`）逐 mapping 的 `slideInfoFileSize`；掩 `0x20` 等价于让 dyld 不提交 slide。
- **rebase 推理（倾向“跳过 slide 不破坏指针”）**：slide-info 语义 = 内核给 slide 页的指针 += `slide`；`slide=0` 时增量 0，指针保持其 preferred VA，而缓存正好映射在 preferred `0x180000000` ⇒ 指针本就正确，理论上无需 rebase。故 SEGV 更可能是 **dynregion(m8) / 注入 libmachook ctor / 某条 mapping**，而非纯 rebase。**需 SEGV PC 定论**（冷启动首交跑带 DYLD_PRINT 的 trace）。
- **post-536 流程（IDA 反汇编）**：`0x356d8` 调 **post-syscall `reuseExistingCache`**（填 loadAddress）；成功 → `0x35704` 打印 **“mapped dyld cache file system wide”** → `ret 1`。**该函数内无 dynregion deref** ⇒ 首交 SEGV **不在 536 后立即处**，在**更下游**（cache 内 dylib 加载 / objc 初始化 / cache-finder）。静态分析无法定论，**必须冷启动跑 DYLD_PRINT trace**。
- ⚠️ 测试纪律：region 一旦映射就**持续到重启** → 每次端到端必须冷启动。本次首交已把本 boot 的区域填满。
- **★★★ 2026-09-27 01:0x 重大发现：崩溃实为 `SIGKILL - CODESIGNING`（CS 击杀），非普通 SEGV！**
  - crash 报告：`echo-2026-09-27-010008/010022/010105.ips` → `exception.type=EXC_BAD_ACCESS`、`signal=SIGKILL - CODESIGNING`、`subtype="UNKNOWN_0x32 at 0x<PC>"`（PC 三次：`0x100dd87c0`/`0x1027107c0`/`0x104b7c7c0`，随 ASLR 变）。
  - ⇒ 进程因**执行了未通过 CS 校验的代码页**被 AMFI/内核击杀（不是野指针 SEGV）。与文档旧结论“errno-40 / `VSHARED_DYLD` / 需项目 entitlements”属同一 **CS/AMFI 家族**。
  - ⚠️ **SIGKILL 无法被信号处理器捕获** ⇒ `dyld_segvcap`（SIGSEGV/SIGBUS handler）对 137 无效，只对真正的 139 有效。
  - **★★★★★ 决定性！报告全字段定位：击杀页 = 我们改过的 dyld 的 __TEXT。** 证据：`termination={code:2, namespace:"CODESIGNING", indicator:"Invalid Page"}`；`ktriageinfo="VM - A memory corruption was found in executable text"`；`vmRegionInfo` 的 PC `0x104b7c7c0` 落在 `0x104b78000-0x104c14000`【**0x9c000=624K** r-x】= thin dyld 的 __TEXT 大小；且 **PC-region = 0x47c0 = `__dyld_start`**。
  - ⇒ **根因假设（强）：修改 dyld 的 __TEXT 代码页后，CS 的页面哈希不匹配 → 内核在执行时判 "Invalid Page" → CODESIGNING 击杀**。与文档旧结论“MUST 带项目 entitlements 签名”同一方向（可能是 ldid 重签未正确覆盖页哈希 / 或需用 entitlements）。
  - 下一步（不需冷启动）：① 核查 ldid 重签是否重建了 CodeDirectory 的 page hashes（对比修改前后的 CD slot hashes / 用 `ldid -S<ent>` 重签）；② 优先**不改 __TEXT 代码页**的方案：只在**可执行 cave**（如未用 padding）写，或改用 **`DYLD_INSERT_LIBRARIES` 注入一个自写 dylib**（它自己合法签名）来装钩，避免改 dyld 代码页。
  - **追加实验（2026-09-27）：原始未修改 dyld.orig 也 rc=137**（其 cdhash 因 fat 头报 "wrong length" 未进 TC）；补丁版已 TC 但也 137。⇒ **CS「Invalid Page」击杀不只是“我改了代码页”——连未修改的 macOS dyld 也遭杀**。⇒ 根因升为：**chroot 里的 macOS dyld 的 `__TEXT` 过不了 iOS 内核的 CS 页校验（`__dyld_start` 处被杀）**，属**跨平台签名 × AMFI** 问题。下一步：内核 RE（Instance2/kc_raw 13339）定位执行时的 CS 页校验/击杀路径，看是否有 boot-arg/entitlement 可豁免。
  - **内核链已理清**：`vm_fault.c:2775` → `cs_invalid_page`（`kern_cs.c:248`）；**击杀条件 = `proc_getcsflags & CS_KILL`**（`kern_cs.c:274` → `threadsignal(SIGKILL, EXC_BAD_ACCESS)`）；页校验器 `cs_validate_page`（`ubc_subr.c:5422`）算页哈希 vs CS blob 期望哈希，不匹配 → `bad_hash`（Invalid Page）。
  - **★ CS blob 参数差异（无重启核查）**：原始 thin dyld → `hashType=2(SHA256)`；`ldid` 重签后 → **`hashType=1(SHA1)`**（pageSize=4096/nCode=297/flags=0x0 均同）。⇒ **ldid 把哈希降级为 SHA1**；若 iOS 内核对该 vnode 期望 SHA256 → `bad_hash` → Invalid Page。**修复候选：用匹配原版（SHA256/正确 CD 形态）的方式重签**（或用 entitlements 重签）。
  - 下一步：① 查被击杀的代码页归属（PC 落在哪个镜像/是否缓存内代码；.ips 无 usedImages，需冷启动复现时用 `MACWS_AGX_CRASH_DIAG` 或内核 RE：AMFI `mpo_file_check_mmap`/执行时的 CS 校验）；② 核对项目 entitlements 是否覆盖该 CS 检查（对比 `entitlements.plist` 与 macOS 缓存内的库需求）。
  - **★★★★★ 2026-09-27 冷启动决定结果（全变体 SHA256）：** `A_first`（**空 region 首交**）= **792 行 + `Mapping the shared cache system wide` + 完整 8 段** ⇒ **首交越过早期 CS 击杀、直接到缓存映射**。`B_noverb/B_noreuse/B_reusemin` = **rc139（真 SEGV）** 且到缓存映射；`B_base/D_sha256/D_ent256`=792；`C_segvcap_sha1`=782。
  - **❗ 合成变体 `dyld_segvcap_reusemin.bin`（segvcap + reuse_min）反而 137 早崩（130B）——说明 `sigaction` 本身扰动了时序** ⇒ 抓 139 需换策略（在不改时序处装 handler，或对其他能到 139 的补丁集加 handler）。
  - **★★★★★ 2026-09-27 拿到崩溃地址（无需 handler，SEGV 自落 .ips）：`true-2026-09-26-165443.ips` → `SIGSEGV` / `KERN_INVALID_ADDRESS at 0x00000001f8000000`。**
    - **`0x1f8000000` = 536 提交里的 m8（dyld dynamic region）**！结合旧结论（本文 §319：*`fd=-1` 的 dynregion 条目随 mapper 进程退出消失，文件映射存活*）⇒ **后续进程 deref `0x1f8000000` → SIGSEGV**。**这就是“缓存映射后立刻崩”的真因。**
    - **修复方向**：① **常驻 keeper 进程**持住 536 映射（使 dynregion 存活）；② **把 dynregion 改成文件映射**（持久）；③ 让 dyld 在 deref 前重建/重新 536 提交 dynregion。
    - ⇒ 同时解释为何 **A_first(首交)=792** 而 **后续=崩**：首交进程自映射（dynregion 对其存活），进程退出后 dynregion 消失。
    - **铁证（.ips 的 vmregioninfo）**：`0x1f8000000 is not in any region`；同表显 `180000000-1e7f5c000 …ed lib __TEXT`（缓存 __TEXT 在）⇒ **m0-m7（文件映射）存活、m8（fd=-1 dynregion）已消失**。诊断 100% 坐实。
    - **✅ 已备 keeper 方案（`/var/mobile/keeper_test.sh`，待冷启动）**：让**首个 536 提交者是一个长驻 macOS 进程**（`/bin/sleep 100000`，已确认存在），它自映射后不退出 → dynregion 存活 → 第 2 个进程（`true`）走 reuse 就不会 deref `0x1f8000000`。判据：keeper alive=yes + reuse 不再 SIGNAL 11@0x1f8000000。**注：keeper 测试必须冷启动**（首交）。
    - **✅ 免重启收尾已完成（2026-09-27）**：① 定论脚本 **`/var/mobile/next_boot.sh`**（一次冷启动：keeper(`/bin/sleep`) 首交 → 验证 alive/缓存映射 → 多进程 reuse（true/ls/echo）→ 区域状态）；② **常驻 keeper 守护**：`com.macwsguide.keeper.plist`（launchd，RunAtLoad+KeepAlive）+ `keeper_start.sh`（等 rootfs→注册 tc→启 cachereg→exec sleep）；③ 全部冷启动/keeper 脚本已过 **POSIX sh 语法**（修掉 `${@:3}` 等 bash 写法）。
    - **⚠️ 2026-09-27 冷启动 next_boot 结果**：`keeperalive=NO`（/bin/sleep 2 行即死）→ **根因 = launcher 给 /bin/sleep 选了 `arch=arm64e` + `libmachook.dylib`**（arm64e 路径坏）；而 true/ls/echo 选 arm64 能到缓存映射。**部署的 launcher（68880B, 9-26 22:45）与仓库源码逻辑（`include/macws_macho_arch.h` 已正确判 arm64）不符 ⇒ 设备上跑的是旧版 launcher；需重编+部署 launcher**。`R_true/R_ls/R_echo` 仍 `SIGSEGV @0x1f8000000`（缓存映射后）。
    - **✅ subagent 大包围（2026-09-27）关键结论 + 已执行修复：**
      - **【更正】`bash/cat` 早死是伪象**：同一 boot 内**后期一切二进制都退化成 2 行**（态退化）；`true/echo/ls` 与 `bash/cat` **无本质区别**。判据：`echo rc=0（非137）`、`sleep rc=137`。
      - **【已修】`/bin/sleep` 是 arm64e（sub=`0x80000002`）→ 已用 `misc/arm64ify_macho.py` 就地 relabel 为 arm64/ALL（sub=0）+ SHA256+adhoc 重签 + trustcache（已验 tc=1）+ chmod755**。这就是 keeper 失败的根因。（备份 `/var/mobile/sleep.orig.bak`）
      - **【确认】launcher arch 判定正确**（旧“部署版与源码不符”被证伪）；**arm64e 路径坏的真因 = `libmachook.dylib` 无 arm64e slice（实为 FAT 两条 arm64）≠ 设计（thin arm64e）** → 需重建（D-3）。影响低（arm64e 目标在 exec 门就 137）。
      - **【瓶颈=dyld 侧】**：`hasExistingDyldCache`→`dynamicRegion()` deref `0x1f8000000`（`dyld/DyldMain.cpp:1084-1095`）；libmachook ctor 晚于它，**排除**；CS/AMFI 是**已越过**的独立模式。
      - **【keeper 可行】**：doc 的“先有蛋”只针对“用 sleep 当第 2 个进程测”，不否决 keeper 思路——keeper 必须是 region 创建后的**第一个**进程（next_boot 顺序天然满足）。
      - **✅ 已备 keeper 二进制清单**（arm64/sub=0）：`bash,cat,echo,ls,pwd,printf,true,uname,update_dyld_shared_cache` + **`/usr/libexec/mobileassetd`**（唯一守护）；或 **arm64ify 后的 `/bin/sleep`**（已做）。
    - **⚠️ 2026-09-27 深夜核查（重要更正 + 不遗漏清单）：**
      - **❗ “true 活/sleep 死”是伪象**：同 boot 后段**连 echo/true 也变 2 行** ⇒ **是 boot 内“态退化”**（随时间劣化，与二进制无关）。**真伪判据**：同一时刻跑 sleep+echo 对比。
      - **“态退化”有健康窗口**：刚 boot 时 echo 能到缓存映射（703–792 行）；跑多轮 536/exec/SEGV 后**一切退化为 2 行**。（这与 §“region 会回空(12)/ret536=22”同源。）
      - **已逐项排除的真实因素**（均已对齐但仍退化→均非根因）：① arch（已 arm64ify，live 行 `arch=arm64` ✓）；② **minos**（`set_macos_version.py` 把 `/bin/sleep` 从 macOS **15.6.0→13.0.0** 已成功，仍 2 行）；③ **entitlements**（用 `ldid -Hsha256 -S<项目 ent> -M` 已把 true 的同一 plist 打给 sleep，同一份十六进制，仍 2 行）；④ trustcache（各二进制 tc 在 clean boot 后均为 0，非差异）；⑤ 热机（echo 预热 4 次后 sleep 仍死）。
      - **仍然成立的真实区分**：健康窗口内 `true/echo/ls` 能到 `Mapping the shared cache system wide`（cachemap=1）；退化后全 2 行。⇒ **真正靶子 = “为何 boot 内态会退化”（而非某二进制）**。
      - **工具产物**：`/var/mobile/{archchk.py,platformchk.py,cschk.py}`；`/bin/sleep` 已 arm64ify+重签（备份 `/var/mobile/sleep.orig.bak`）。未发现 sleep/bash/cat 的新 `.ips`（退化是静默 2 行/rc）。
- **subagent 大包围取证（2026-09-27）：**
  - 崩溃窗口 = `reuseExistingCache` **成功返回之后**（日志止于 8 段打印 `t1_final_trace.log:792`）；源码 `SharedCacheRuntime.cpp:1373-1378` 证成功路径**不**打印 `mapped dyld cache file system wide`（勿误判）。
  - **拿 PC 首选**：给 dyld `start()`(thin 0x53dc) 最前注入早期 SIGSEGV-handler cave（handler 从 ucontext 取 pc/far → 写 /tmp/dyldsegv.txt → _exit），复用 `build_dyld.py`/`cave_noslide.s` 工具链；libmachook 的 `MACWS_AGX_CRASH_DIAG`（`mac_hooks.m:7324-7502`）**装在 ctor，太晚**，抓不到 dyld 启动崩溃。
  - **ellekit 不是子进程崩溃源**：`launchdchrootexec/main.m:71` 的 `setenv(...,1)` **覆盖**掉 ellekit 注入项 ⇒ chroot 子进程只带 libmachook（`forkfix/libinjector` 仅在 banner 之前的 launcher iOS dyld）。
  - **post-cache 高风险 deref（优先级）**：`DyldDelegates.cpp:158-161`（getUUID+kdebug）、`dyldMain.cpp:1086-1095`（handleDyldInCache→dynamicRegion）、`dyldMain.cpp:1215-1219`（restartWithDyldInCache）、`DyldProcessConfig.cpp:1416-1431`（dylibsExpectedOnDisk + objc/swift 表位）。
  - 备用拿 PC：`catch_segv.sh`（+`MACWS_SUSPEND_AT_EXEC=1`）。注：日志若缺 `mapped dyld cache file system wide` 属正常。
  - **✅ 已造成工具 `dyld_segvcap.bin`**（2026-09-27）：在 dyld `start`(0x540c) 挂钩 → 跳 `__TEXT` 尾段可执行零填充 cave `0x9b578`（0xa88B 可用）→ 内部以裸 `svc` 装 SIGSEGV/SIGBUS handler，handler 把 **siginfo(0x20)+ucontext(0x140) 原样 write 到 stderr** 再 `_exit(139)`（免猜偏移，离线解 pc/far）。构建方式：`analysis/dyldwork/segvcap_cave.s`（clang 汇编体）+ 手算 adr/b 重定位（已验：hook→0x9b578, adr→handler@0x9b5c8, b→0x5410）。**待冷启动验证**（已入 `run_all_cold.sh` 的 `C_segvcap`）。

### （旧）三条真根因
1. **exec EACCES(13) = 部署文件缺 `+x`**（`kern_exec.c:6242`：非 authopaque 挂载 + 无执行位 → EACCES）。**修复：部署后 `chmod 755`**。→ exec 全通，macOS dyld 真运行。
2. **dyld 内探针 SIGILL(132) = 补丁 hex 字节序写反**（`build_dyld.py` cave 用了反汇编字序）。**修复：`_le()` 每 4 字节转真小端**。
3. **syscall 536=EINVAL(22) = 缓存 slide-info version=5，iOS 16.3 内核只支持 1–4**（`sub_8062CA8@0xfffffe0008063024`；源码 `vm_shared_region.c:2934` default→KERN_FAILURE→`vm_unix.c:2725`→EINVAL）。触发位 = `sms_max_prot & VM_PROT_SLIDE(0x20)`。

### 已验证的可行性（关键里程碑）
- **空 region 上，`cachereg`（挂 cache cs_blob）+ 掩掉 9 条 mapping 的 `0x20` + `files[0].sf_slide=0` → `ret536=0`（缓存真映射）**。
  - **两个条件缺一不可**：缺 `cachereg` → gate11（CS 覆盖）失败 → 22（本 session 首次重启测试就因脚本漏启 cachereg 而失败）；缺掩码 → v5 slide pass → 22。
  - 实测：2026-09-27 00:3x boot（已启 cachereg）用掩码探针 `sf_ns` → **try1 ret536=0**，try2/3=22（region 已填）。
  - 说明：`check_np` 从 iOS 侧读 `ret=0 base=0` 不可信（无法区分空/满）；以**子进程内** `check_np`（✅ 12=空）为准。
- 已把该绕法做成两个真 dyld 补丁（见下），字节已经 IDA/clang 校验。

### 候选 dyld（已部署 `/var/mobile/`，重启后用）
| 文件 | 内容 | 用途 |
|---|---|---|
| `dyld_noslide.bin` | cleanB 全补丁 + 在 `0x35690` 注入 cave（`files[0].slide=0` + 掩 9 条 mapping 的 `0x20`） | 强制 map 路径 |
| `dyld_noslide_reuse.bin` | 同上但**不 patch `hasexisting/prereuse`** | 首交 map + 其余进程 reuse |
（cave 源码 `analysis/dyldwork/cave_mask20b.s`；构建见本文件对应段）

### 当前状态 / 恢复步骤
- **当前 region 已被填满**（本 boot 任何 536 都 22/139）→ **只能在冷启动空 region 上测**。已写 `analysis/dyldwork/post_reboot_noslide.sh` 并部署 `/var/mobile/`。
- **重启 + Dopamine 重越狱后**：`sh /var/mobile/post_reboot_noslide.sh`。成功判据：第1步 `echo NOSLIDE-OK` 且**无** `syscall to map cache into shared region failed`。
- 设备恢复清单（重启后必做）：trustcache 重加（`jbctl trustcache add <cdhash>`）+ `chmod 755` 所有要用二进制 + `nohup /var/mobile/cachereg <cache> <cache>.01 &`。

### 工具/坑速查（血泪）
- `analysis/dyldwork/exec_probe.py`：chroot+posix_spawn 真返码（launcher 的 `perror` 是残留 errno，不可信）。
- 探针 cave 字节必须 clang 汇编后取真字节（手写/字序必错 → SIGILL 132）。
- 描述符级探针：pre-svc 重入 svc 后写 stderr 会在 attach 窗口被杀（137）；用“pre-svc 改参 + `b` 回原 svc + post-svc 在 `0x76e00` 写 ret”双 cave 才稳。
- KRW（`/var/jb/basebin/libjailbreak.dylib` kread/kwrite）：臂64e 数据指针 PAC 是 47-bit VA（`0xffff800000000000|(v&0x7FFFFFFFFFFF)`）；**裸写 vnode/mount 等 PAC 指针会 ptrauth panic**。
- 部署 dyld 用 `cp` 从 `/var/mobile` 源（scp 直写会丢元数据）+ `chmod 755`；解释器(dyld) 必须 **CD flags=0x0**（`ldid -Cadhoc` 会使其被内核拒收）。

## 2026-09-26 23:0x — ★★★★ exec EACCES 真根因 = 缺可执行位；exec 门彻底打通 ★★★★

**整场 session 卡住的“exec EACCES(13)”根因是文件 mode 缺 +x，与 CS/trustcache 无关。**

- **决定性证据（源码 `analysis/xnu-xnu-8792.81.2/bsd/kern/kern_exec.c:6242`）**：
  ```c
  if (!vfs_authopaque(vnode_mount(vp)) &&
      ((vap->va_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) == 0))
      return EACCES;   // 挂载点非 authopaque 且文件无执行位 → EACCES
  ```
  `/private/var`（rootfs 所在）**不是 authopaque**。部署脚本的 `rm -f; cp` 把目标建成 `rw-r--r--`（0644）→ kernel 直接 EACCES，**静默、无 AMFI 日志**。
- **实测对照（本回合）**：`ls -la $R/usr/bin/true` = `-rw-r--r--`；`ls -la $R/tmp/cachereg_test` = `-rwxr-xr-x`。**只有缺 +x 的失败**。
- **修复**：部署后 `chmod 755 <file>`。chmod 后 `true`/`ls`/`echo`/`printf` exec **全部成功**，macOS dyld 真正运行：
  ```
  dyld '<null>' not loaded: syscall to map cache into shared region failed
  => 应用层回到原任务核心：syscall 536
  ```
- **含义**：`exec admission`（arm64ify / -Hsha256 / trustcache / platform / load 命令）以前的所有冲突结论，很多是被这个 +x 门污染的。**现在 exec 已稳定可用**（本回合多次复现 rc=0 落地到 536 层）。

**次要点（仍成立且有用）**：
- 解释器(dyld) 需 **非 ad-hoc（CD flags=0x0）**：`ldid -Cadhoc`（0x2）会让内核拒收解释器（`ldid -S` 裸签=0x0 可过）。部署 dyld 用 `cp`（从 /var/mobile 源）比 scp 直写更稳。
- launcher(launchdchrootexec) 曾在 `0x46FC` 被写坏成非法指令 `0x001c0012`（posix_spawn 错误路径）→ 任意 posix_spawn 失败即 SIGILL(132)，掩盖真实码；已修复（正确 12B：`00000090 00042a91 25000094` = `adrp x0,#0; add x0,x0,#0xa81; bl _perror`）。且 `perror("posix_spawn")` 打的是残留 errno（posix_spawn 不写 errno）——真实码用 `analysis/dyldwork/exec_probe.py`（ctypes 直调，rc=真实 errno）。
- 工具固定新增：`analysis/dyldwork/exec_probe.py`（chroot+posix_spawn 真返码，`python3 -u exec_probe.py <rootfs> 0x0 <target...>`）。
- **下一步**：syscall 536 真实 errno（errprobe dyld 目前经 launcher 得 rc=132，待用 stderr-write 探针或重新核位取得——见文件末尾 22:x 段相关构建键 `fwentry2`/`fwcave2`）。

## 2026-09-26 23:5x — ★★ 536 真 errno = EINVAL(22)；region 实为空；探针 SIGILL 根因 = 字节序 ★★

（详见 `docs/porting/syscall536-errno-and-probe-sigill.md`；本段补最新实测）

**A) 探针 SIGILL(132) 根因 = 字节序。** `build_dyld.py` 的 `P` 表把**多指令 cave** 的 hex 按“反汇编器字序”（如 `d10043ff`）直接 `bytes.fromhex` → 落盘成小端逆序 → 非法指令。**已修**：新增 `_le()` 把每 4 字节字转为真小端（单指令条目本就正确，仅 cave/errprobe 错）。修正后 cave `0x38d08` = 合法微小端 `ff0301d1 e01300f9 …`。

**B) 536 真实返回 = 22 (EINVAL)**（可靠探针：拦截 `__shared_region_map_and_slide_2_np` 的 post-svc `B.CC`(0x76e00) → cave 写 raw x0 到 stderr）。

**C) ★ region 实测为空（非“已填充”）。** 同一 cave 额外调 `shared_region_check_np`(294)：得到 `checknp_ret=12`（= 存在但空）+ `ret536=22`。⇒ **推翻 `syscall536-errno-and-probe-sigill.md` 的 #1「region 已填充」假说**，也推翻旧文档的“40=SESandbox”。

**D) 已挂 cs_blob（cachereg 运行中、`fcntl=0`）仍为 22。** ⇒ 536 的 22 来自 **setup 门**（不是引擎的 region-populated）：内核 `sub_8459570` 中 EINVAL=22 的来源为：门6 `v_type!=VREG`、门10 `ubc==0`(vnode+120)、**门11 per-mapping `ubc_cs_blob_get` 覆盖不足**（`sub_8459570` 末尾的 `v66[6]+v67 > v64 || v66[7]+v67 < v65` 分支）或 engine 返回。

**E) 工具**：`analysis/dyldwork/_bx3.py`（读 checknp+536ret 的探针构建器，带与 clang 字节的自校验）；`cave_checknp.s`。设备现役 dyld = `/var/mobile/stub_checknp.bin`。

**下一步（重要）**：定位 536 到底是 setup 哪一门。建议：① 在 `_bx3` cave 里额外写 `files_count`/`mappings_count`/首条 mapping 的 `file_offset+size`，与 vnode blob 的覆盖字段（KRW 读）对算，看是否命中门11；② 重建“只交 mapping0”的 16:00 式探针，确认单条 mapping 能否返 0（分离“首条 vs 后续/dynregion”）。

**F) 实测提交参数（`_bx4.py` / `stub_argdump.bin`，拦截 536 pre-svc 0x76df8 dump x0..x3）= `files_count=2, mappings_count=9`。** 即：**2 个 file 条目（主缓存 + 尾部 `fd=-1` dynregion 伪条目）+ 9 条 mapping**（主缓存 8 条 + dynregion 1 条）；**`.01` 子缓存未提交**（=无 4GB 溢出）。
  - 更正：`filescount1`(0x3538c) 按本文 17:50 段是 **mapSplit flag，不是 files_count**；真正决定 files 数的是 dyld 内部逻辑。
  - 因此 536=EINVAL 的候选锁窄到：**门6 v_type**（主缓存是普通文件，应过）、**门10 ubc==0**、**门11 per-mapping CS 覆盖**、或 **门4 dynregion fd=-1 伪条目**、或 **engine**。
  - 现役诊断 dyld = `/var/mobile/stub_argdump.bin`（dump 参数）；重建器 = `analysis/dyldwork/_bx4.py` / `cave_argdump.s`（clang 汇编 + 重算 branch）。

**G) 实测 9 条 mapping 字段（`_bx5.py` / `stub_mapdump.bin`，cave 把 x3 指向的 9×48B 写 stderr）**：
```
m0 addr=0x180000000 size=0x67f5c000 foff=0x0          ip=5
m1 addr=0x1e7f5c000 size=0x1e90000  foff=0x67f5c000   ip=97
m2 addr=0x1ebdec000 size=0x239c000  foff=0x69dec000   ip=99
m3 addr=0x1ee188000 size=0x24000    foff=0x6c188000   ip=99
m4 addr=0x1ee1ac000 size=0x1200000  foff=0x6c1ac000   ip=35
m5 addr=0x1ef3ac000 size=0x7cc4000  foff=0x6d3ac000   ip=33
m6 addr=0x1f9070000 size=0x5cdc000  foff=0x75070000   ip=1
m7 addr=0x1fed4c000 size=0x268c0000 foff=0x7ad4c000   ip=1
m8 addr=0x1f8000000 size=0x4000     foff=0x10285c000  ip=1  (dynregion 伪条目, foff=用户指针)
```
- m0..m7 = 主缓存 8 条，**全在 4GB 界内**（end=0x22560c000）；文件偏移覆盖 `[0, 0xa160c000]`（恰等于缓存 CS blob 的 `cso`）。m8 = 重定位后的 dynregion（`dynoff` 生效）。
- **强制参数实验（`cave_force2.s`：pre-svc 改 x0/x2 后自己发 svc 再写 stderr）遇到 137**（在 attach 窗口写→被杀），无法可靠取 ret；但**暗示「减量提交可能真的映射成功」**（attach in-flight→137），待用更稳手法确认。
- 下一步：⑧ 用 stubprobe 手法（从 0x76e00 入口，避开 pre-svc 重入）在 **dynregion 被移除**的变体上取 ret；⑨ 或直接 KRW 读缓存 vnode 的 cs_blob 覆盖字段（`v66[5..7]`）与 m0..m7 对算，坐实/排除门11。

**H) ★★ 双 cave 强制参数实验（已跑通）：错误码分层。** 手法：`cave_seting2`（0x76df8 → 改 x0/x2 → `b 0x76dfc` 跑原 svc）+ `cave_capture`（0x76e00 → 写 x0 → exit），修复了 pre-svc 重入导致的 137，ret 稳定可读。结果：
| 强制参数 | ret536 |
|---|---|
| fc=1, mc=9（丢 dynregion，但沿用自然 mappings_count=9） | **22** |
| fc=1, mc=1（仅 m0） | **5** |
| fc=1, mc=8（主缓存全部 8 条） | **5** |
| 自然 fc=2, mc=9（含 dynregion） | **22** |
- ⇒ **两种失败分层**：**dynregion（fd=-1 伪条目）导致 22**；**主缓存自身（即使单条 m0）导致 5（KERN_FAILURE）**。自然提交先撞 dynregion 的 22。
- 这与旧文档“空 region 时 536 返 0”不符——当今缓存/vnode 状态下，即使良构/单条也返 5，说明 **5 来自主缓存映射的引擎层失败（候选：`ubc_cs_blob_get` 覆盖 / ubc / vm_map_enter）**，非 region 占用。
- 构建器：`analysis/dyldwork/` 下 `cave_seting2.s` + `cave_capture.s`；禁用 dyld = `/var/mobile/sf_mc1.bin` 等。
- 下一步：⑩ 用 **KRW 读缓存 vnode 的 cs_blob 覆盖字段**（`v66[5..7]`）与 m0 的 `[0,0x67f5c000]` 对算，判定 5 是否来自门11；⑪ 或在内核 IDB 反编译引擎 `sub_8061EF0`/包装 `sub_8459134` 定位 5 的返回点。

**I) 更正 H)：强制实验的 5 是「计数不匹配」伪码，不是真实失败。** 内核 wrapper `sub_8459134` 的 `5` 只来自：① `files_count>0x100`；② `mappings_count>0x800`；③ **slide 计算循环 line 191（files[].sf_mappings_count 与 mappings_count 不一致）**。我改了 x2（mappings_count）却没同步 files[] 声明计数 ⇒ 计数不匹配 ⇒ 5。**自然提交（fc=2,mc=9，8+1 自洽）→ 22 才是真实失败**（来自 setup `sub_8459570` 或其调用的 engine `sub_8061EF0`；wrapper 仅当 engine 返 >3 才转 22）。故正确靶子仍是 setup 门6/10/11 或 engine——需 KRW 直接读写 vnode/blob 来判定。

**J) KRW 实测：setup 门 6/10/11 全部通过。** `blob_read.py` 走 fd→vnode→(vnode+0x78)=ubc→(ubc+0x50)=cs_blob 读到：`v_name='dyld_shared_cache_arm64e'`；`ubc` 非空；`blob` 非空；覆盖字段 `blob+0x38=0xa160c000`、`blob+0x40=0x50c000`（= 缓存 CD 的 cso/css）。门6 `v_type`(word@vnode+0x70=1 VREG)✓、门10 `ubc!=0`✓、门11 覆盖 `[0,0xa160c000] ⊇ m0..m7`✓。⇒ **22 来自 engine `sub_8061EF0`（返回 KERN_FAILURE>3 被 wrapper 转 22）**，不是 setup 门。

**K) 提交参数：`files[] = [{fd=3,count=8,slide=0x20000000}, {fd=-1,count=1,slide=0}]`，`mappings_count=9`**（`_bx4`+`cave_argdump2`）。强制 `files[0].slide=0` **无效**（仍 22）⇒ 不是 slide。

**L) 当前结论**：536 的 22 是 **engine 层 map 失败**（region 空、setup 全绿、非 slide）。下一步靶子在 engine `sub_8061EF0`/`sub_80623D4` 的 KERN_FAILURE 返回点（需内核单步或对参数再 dump）；先例：文档 16:00 “单条 mapping → 536=0” 待重现（需自洽且 slide 字段合适的提交）。

**M) ★★★★★ 536=EINVAL 的真因锁定：缓存 slide-info version=5 不被 iOS 16.3 内核支持。**
- 链路：engine `sub_8061EF0`(自身无硬编码>3) → 其 slide-pass callee **`sub_8062CA8`** @ **`0xfffffe0008063024`**：`if ((unsigned)(slide_info->version - 1) > 3) → KERN_FAILURE(5)`（源码 `osfmk/vm/vm_shared_region.c:2934` switch(version) 的 default；非 1/2/3/4 全落此）→ 经 `bsd/vm/vm_unix.c:2725` `case KERN_FAILURE: EINVAL` → **22**。
- **物证**：`xxd -s 0x7ad4c000 -l 32 /Users/ciscohe/Desktop/macPad/analysis/dyld-cache-15.6.1/dyld_shared_cache_arm64e` = `0500 0000 0040 0000 e708 0000 …` → **version=5、page_size=0x4000(16K)**。5 条 slid mapping 的 `sms_slide_start` 指向处均如此。
- **触发条件**：`sms_max_prot & VM_PROT_SLIDE(0x20)`（engine 用 `(prot>>5)&1` 计数 → 与 slide *数值* 无关）⇒ 解释了「强 `files[0].slide=0` 仍 22」；**m0 无 0x20 → 不进 slide pass → 历史单条 mapping 能 536=0**。
- **次生障碍**：即使把 version 改 4 也仍失败——`page_size=16384 ≠ 内核 `PAGE_SIZE_FOR_SR_SLIDE=4096`（`vm_shared_region.h:98`）。**双重格式不兼容**。
- **修复方向**（未实现）：① 从新版 xnu(11215+) backport `case 5:` + `vm_shared_region_slide_page_v5` 并把 `PAGE_SIZE_FOR_SR_SLIDE` 放宽为 16K（需内核补丁）；② 用户态重生成缓存 slide-info 为 v4/4K（工作量大，但不碰内核）；③ 应急验证：提交前清掉映射的 `VM_PROT_SLIDE(0x20)` + slide=0 以跳过 slide pass（但 __DATA 的 rebase 指针可能留错，仅确认分支）；④ 换用 slide-info 为 v4/4K 的旧缓存。
- 辅助脚本：`/Users/ciscohe/Desktop/macPad/analysis/dyldwork/parse_dsc_slideinfo.py`（subagent 新增，解析 slide-info 头）。

**N) ★★★★★ 验证成功：清掉 `VM_PROT_SLIDE(0x20)` → 536 返 0，缓存真正映射。**
- 探针（`cave_noslide.s`：pre-svc 把 9 条 mapping 的 `max_prot/init_prot` 都 `bic #0x20`，并 `files[0].sf_slide=0`）→ **try1 ret536=0（成功！）、try2 ret536=22**（region 已被 try1 填满 → 再交 EINVAL）。
- ⇒ **slide-info version 5 的墙被证实且找到**用户态可行**绕法**：不让 dyld 提交 `VM_PROT_SLIDE`，内核就不跑 v5 slide pass。
- ⚠️ **两个后续问题**：① **region 一旦被填就持续到重启**（try1 后 try2=22）⇒ 需「冷启动首交成功 + 其余进程 reuse」或常驻 keeper；② 跳过 slide 后 __DATA 的 rebase 指针可能留错（待验 dyld 自处理能力；若 slide=0 使 rebase 为恒等则 OK）。
- **正确的用户态修复**：在 dyld 构建 mappings 处不带 `0x20`（而不是运行时 cave）。下一步：IDA Instance1 定位 dyld 给 mapping 加 `VM_PROT_SLIDE` 的点（submission construction 区 0x35600-0x356c0），做成常量补丁。",

**O) 候选 dyld 构建配方 + 双 cave 测试法（可复现）**
- **hook 点**：dyld thin `0x35690`（原 `MOV X3,X26`，紧接 0x35694 `BL __shared_region_map_and_slide_2_np`）。此时 `x1=files ptr, x25=mappings_count, x26=mappings ptr`。
- **cave（`cave_mask20b.s`，clang 汇编取真字节）**：
  ```
  str wzr,[x1,#8]        // files[0].sf_slide = 0
  mov x9,x26; mov x10,x25
  loop: ldr w11,[x9,#40]; bic w11,w11,#0x20; str w11,[x9,#40]
        ldr w11,[x9,#44]; bic w11,w11,#0x20; str w11,[x9,#44]
        add x9,x9,#48; subs x10,x10,#1; b.ne loop
  mov x3,x26; b 0x35694
  ```
  cave @ `0x38d08`（`__text` NOP 填充死区）；`0x35690 <- b 0x38d08`（`9e0d0014`）。
- **两个变体**：`dyld_noslide.bin` = DEFAULT 全补丁 + cave；`dyld_noslide_reuse.bin` = DEFAULT 去掉 `hasexisting/prereuse` + cave。构建用内联 python（导入 `build_dyld` 取 P 表 + `_le` + 重算 branch）。
- **双 cave 取 ret 测试法**：`0x76df8 -> cave_seting`（改 x0/x2 后 `b 0x76dfc` 跑原 svc）+ `0x76e00 -> cave_capture`（写 x0 到 stderr + exit）。这样避开 pre-svc 重入 svc 导致的 137。
- **单次成功已由 `cave_noslide.s` 实证**（pre-svc 掩 0x20+slide=0 → ret536=0）；但端到端（dyld 继续加载 libSystem）只能在**空 region（冷启动）**验证。

**P) 复用(reuse) 设计（dyld IDA 反编译）**
- **`hasExistingDyldCache`(0x30140)**：`check_np` → `W0!=0` 返回 0（**空 region check_np=12 → 返回 0 → 走 map**）；`W0==0`（已填）→ `dynamicRegion()` → `getDyldCacheFileID` → **返回 1（已有缓存 → reuse）**。
- **`reuseExistingCache`(0x351a8)**：`check_np` → `W0!=0` 返回 0；否则 `strcmp(base,"dyld_v1  arm64e")` → 匹配则 **reuse**；不匹配→“existing shared cache … is not …”→0。
- ⇒ **`dyld_noslide_reuse`（不 patch `hasexisting/prereuse`）是多进程正解**：首进程（空 region）check_np=12 → map；后续进程（region 已填**我们的**缓存）→ magic 匹配 → reuse。cleanB 的 `hasexisting/prereuse→0` 反而破坏 reuse。
- ⇒ 印证旧文档的两个假设都可去：① `hasexisting` 的 dynamicRegion deref 崩溃只发生在 region 被**其他**缓存填满时；region 填的是我们的缓存时 deref 正常。② 旧文档“pre-reuse 会误收 iOS 缓存”也仅在 region 真有 iOS 缓存时成立。

## 2026-09-26 18:2x — ★★ EXEC 137 ROOT-CAUSE FOUND + BLOB PURPOSE DECODED + CLEAN DYLD ★★

**A) The whole device can no longer exec ANY macOS binary (all → SIGKILL/137).**
This is the immediate blocker for every dyld experiment. Evidence (runtime, this session):
- `true`, `/bin/ls`, and EVERY project probe (`minexit_arm64[ e]`, `srprobe`,
  `maptest`, `dsctest[2]`, `hw`, `misc/sprobe`) → `rc=137`, zero output, no `.ips`.
- SAME result via: plain procursus `/var/jb/usr/bin/chroot`, `launchdchrootexec`,
  and the project's OWN launchd job `com.macwsguide.smoketest`
  (`LastExitStatus=9`, `smoke.out` empty). 60/60 retries = 137 → NOT intermittent.
- Apple-pristine `dyld.orig` → 137; trusted `dyld_pm`/`cleanB` → 137.

**One exceptional GOOD run observed**: right after `killall -9 amfid` +
restoring a *trusted* `/usr/lib/dyld`, `true` ran (rc=134) and macOS dyld printed:
```
dyld: dyld cache '(null)' not loaded: syscall to map cache into shared region failed
dyld: Library not loaded: /usr/lib/libSystem.B.dylib (code signature invalid …)
```
This proves: (1) exec CAN work; (2) once it does, we are back at the expected
**syscall 536 fails** layer (dyld falls back to disk → CS-invalid dylib → abort 134).
It could not be reproduced afterwards (60/60 = 137).

**TRAP discovered**: deploying Apple-pristine `dyld.orig` as `/usr/lib/dyld`
WITHOUT adding its cdhash to trustcache makes EVERY macOS exec SIGKILL (137),
because the kernel kills the process when the dyld it must load is untrusted.
Always trust a deployed dyld before running.

**Ruled out as causes** (all tried this session, none fixed it):
- trustcache membership (hashes present + `jbctl add` + `macws_boot_trust.py --readd`);
- per-binary re-sign with project entitlements (`platform-application` present);
- amfid dead vs alive; pre-reading the dyld to populate the vnode CS blob;
- **autosignd** (the per-exec trustcache daemon): started it (`restart_autosignd.sh
  --force`, socket `/var/mnt/rootfs/tmp/autosignd.sock` live, pid running) + re-added
  autosignd/true/ls/dyld cdhashes → still 137;
- full `postinst.sh` re-run (re-signed 1156 images, rebuilt platform tags);
- `nvram boot-args` empty; no `amfi_*`/`cs_*` sysctl exposed.
Kernel strings present: `amfi_enforce_launch_constraints`,
`amfi_allow_3p_launch_constraints`, `"AMFI: Launch Constraint Violation …"`
(@ `0xfffffe000735197c`), but NO AMFI log line is emitted on our kills.
**Conclusion: kernel userspace-irreparable exec/CS state (panic-era pmap/shared-region
corruption suspected — the `pmap_trim_internal` panic *was* on an exec teardown of `true`).
A kernel reboot (+Dopamine re-jailbreak) is required to continue.**

**B) The `dyld_pi` injected blob's TRUE purpose is decoded.**
`preflightCacheFile` (thin `0x35a98`) calls `fcntl(fd, 97 /*F_ADDFILESIGS_RETURN*/,
fs)` at `0x35d68` to self-attach the cache's cs_blob, then fails in-chroot
(EPERM from `mac_vnode_check_signature` → AMFI). The blob existed ONLY to bypass
that. Since host-side `cachereg` already attaches the blob, dyld's redundant
attach is skippable with TWO tiny patches instead of a `0x3576c-0x35afe` rewrite:
| off | orig | new | meaning |
|---|---|---|---|
| `0x35d70` | `00 01 00 54` (B.EQ) | `1f 20 03 d5` (nop) | ignore `fcntl==-1` |
| `0x35d80` | `e2 00 00 54` (B.CS) | `07 00 00 14` (b 0x35d9c) | skip coverage check |
This CLEAN build keeps the ORIGINAL preflight AND re-enables `.01` subcaches
(the blob had forced main-cache-only). Builder: `analysis/dyldwork/build_dyld.py`.

**C) Clean dyld builds produced this session** (`analysis/dyldwork/`, signed, thin arm64e):
- `dyld_cleanB.signed` (cdhash sha256 `fe9947cd…`): crossarch + hasexisting(0x30140→0)
  + prereuse(0x34298→0) + filescount1 + dynoff(0x35fc8→0x78000000) + accessor(0x50dfc)
  + fcntl_nop(0x35d70) + cover_b(0x35d80). NO blob.
- `dyld_cleanB_err.signed` (cdhash `48218610…`): cleanB + errno probe at `0x35698`
  = `and w0,w0,#0xff; mov x16,#1; svc #0x80` (`12001c00 d2800030 d4001001`) →
  exits with `syscall536_ret & 0xff`. Ready to run the moment exec is restored.
- `dyld_cleanB_glue.signed` (cdhash `f1bbb79b…`): cleanB + §9.3 glue-call probe.
  **REAL dead cave found (IDA, zero xrefs)**: `__text` NOP padding `0x38d08-0x38d3b`
  (52B; real function `dyld_program_minos_at_least` starts `0x38d40`).
  `0x6b94 blraaz x8` → `b 0x38d08` (`5dc80014`); cave = `write(1,{x8,x9},16); exit(0)`.
  This replaces the old *contaminated* glueprobe (which sat inside the live blob at
  `0x3588c` and produced the bogus "103").
Every patch encoding is assembler-verified; all offsets byte-match pristine.

**Next after reboot**: redo §2 restore, then run `dyld_cleanB_err` → read the
536 errno (expect 0 if always-map self-heals dynregion, else EINVAL/EPERM).

### 2026-09-26 19:0x — exec 137 深挖（重启后仍复现）+ 内核 RE 判定链

**重启 + 重越狱 + 完整 postinst 后，exec 137 依旧。** 又排除：
- Mach-O `platform` macOS(1)→iOS(2)：仍 137；
- 非 chroot（root=`/`）、任意路径、`misc/sprobe`（最小 freestanding）、
  裸签（无 entitlements）、`macws_boot_trust.py --readd`（经 libjailbreak 真注册 3 个 hash）：**全部仍 137**。
- 重启后项目 macws 作业原本不在（未 bootstrap）；`postinst.sh` 已重载，无效。

**内核 RE（Instance2，符号齐全）定位到的判定链：**
- AMFI MACF 钩子 `_vnode_check_signature` @ `0xfffffe00092a45e4`
  （`AMFIIsCodeDirectoryInTrustCache`→`pmap_lookup_in_static_trust_cache`；
  `codeDirectoryHashIsInLoadedTrustCache`→`pmap_lookup_in_loaded_trust_caches`）。
- `_vnode_check_signature` 关键分支：
  - L359：cdhash 不在（static ∪ loaded）trustcache → 走 `StaticPlatformPolicy::check_signature`（callout amfid）。
  - L1124：`if ((cs_flags & CS_PLATFORM_BINARY/*0x4000000*/) == 0)` 才检查
    `devModeStatusResolved()`；否则 fatal "only allows platform binaries until
    developer mode status has been resolved"。
  - 设备实测 `developer_mode_status=1`、`developer_mode_resolved=1` → 此分支不触发。
- 所以：**“cdhash 在（static∪loaded）trustcache → category 'trust-cache' → 放行”**
  这条链在 RE 上应该通过，但 exec 仍被杀 → 触发点在更下游（疑似 exec 页映射/CS_KILL，
  或一个尚未定位的更早检查）。

**待用户输入**：当初设备上“让 macOS 二进制可 exec”的完整/重启后必做步骤。

### 2026-09-26 19:3x — ★★★ 重大突破：exec 137 根因 = arm64ify！★★★

**根因锁定（subagent 通读仓库文档 + `misc/arm64ify_macho.py` 注释双证）：**
> “the iPadOS kernel will only exec the chroot's macOS binaries as **ARM64/ALL**”

iPadOS 内核**只接受 `cpusubtype=ARM64/ALL`(0) 的 macOS 主可执行体**；macOS 15.x 系统二进制
只有 `x86_64+arm64e`（无 arm64 切片）→ 直接被 exec 门 SIGKILL(137)。

**实测（本回合）**：用 `misc/arm64ify_macho.py` 把 `dyld`/`true`/`ls`/`bash`/... 就地 relabel 成
ARM64/ALL（代码字节不动）+ `ldid` 重签 + trustcache 后：
- **137 消失！** `true`/`ls`/`bash`/`echo`/`printf`/`pwd`/`uname` 不再被杀。
- `echo`（未 arm64ify，仍 arm64e）仍 137 → 完全印证“arm64e → 杀”。
- `misc/sprobe`（arm64e）→ 137；证实。

**越过 137 后暴露下一道门：`EACCES`(13)。** 用设备端 ctypes 探针
`analysis/dyldwork/ceprobe.py`（`posix_spawn` 直调，拿**真实返回码**）查到：

```
/usr/bin/true  -> rc=13   (EACCES)    ← 所有 arm64 macOS 二进制均是 13
/var/mnt/rootfs/tmp/creg  (iOS 二进制) -> 能跑  ← rootfs 卷可 exec
```

**⚠ 重要侦错陷阱（subagent 发现）**：`launchdchrootexec` **丢弃 `posix_spawn` 返回值、
无条件 `perror("posix_spawn")`**，而 `posix_spawn` 不写 errno——所以它打印的
”No such file or directory” 只是**残留 errno（多为 chdir 的 ENOENT）**，**不是真实失败码**。
以后测真正的 exec 错误一律用 `analysis/dyldwork/ceprobe.py`。

**EACCES 已排除（本回合逐一实测）**：
- rootfs 卷 `noexec` → 排（/private/var 无 noexec；iOS 二进制放 rootfs 内 chroot 能跑）
- entitlements：`no-sandbox`/`no-container`/`get-task-allow` 均在；去 `platform-application` 无效
- Mach-O `platform` macOS→iOS → 仍 13
- dyld arch = arm64 vs arm64e → 都 13
- CS flags `CS_HARD|CS_KILL|CS_RUNTIME` → 仍 13
- `rm`+`cp` 换新 inode / 两遍 ldid 签名 / `-Hsha256` → 仍 13

**下一道门待查**：EACCES 的触发点（疑似内核 exec 的 CS/sandbox 判定；
也可能与“首次 exec 前必须先把 loader 闭包入 trustcache”那条纪律有关，
见 `layout/DEBIAN/postinst:291-304`）。工具已就绪：`ceprobe.py` + 设备端 `arm64ify_macho.py`。

### 2026-09-26 19:4x — EACCES(13) 内核触发点已定位（Instance2 RE）

**exec 拒绝在核心里只有一处返回 13**，在 exec 核心 helper `sub_FFFFFE000839B748`
（被 `posix_spawn`/`__mac_execve` 调用）：

```c
v35 = sub_FFFFFE000839BF5C();      // → sub_FFFFFE0007FE51E8
if (v35 != 5) {
    if (!v35) { …ubc_cs_blob_get/csblob_find_blob_bytes(CSMAGIC_BLOBWRAPPER=0xFADE0B01) 检查… → LABEL_35(成功) }
    else { os_reason_create(OS_REASON_EXEC,9); v17 = 13; }   // ← EACCES
} else { os_reason_create(OS_REASON_EXEC,8); v17 = 13; }
```

`sub_FFFFFE0007FE51E8` = **向用户态策略服务发 MIG `msgh_id=27001`**，等回复 `27101`
里的结果字节 `v6`：**`v6 != 0` 就返回非零 → EACCES**。即：**exec 会同步 RPC 一个
用户态策略服务；它返回非 0 就判 EACCES**。

**旁证**：同一拒绝在 `imgp` flags bit 0x10 置位时改走 `terminate_with_reason` → SIGKILL；
所以 **EACCES 与 137 是同一内核 exec 拒绝的两种上报形态**。AMFI 钩子
`_vnode_check_exec @0xfffffe00092a69e8` 无条件置 `CS_HARD|CS_KILL(0x300)`。

**实测（本回合）**：amfid 一直在线（launchd 守护重生，杀不掉），EACCES 不随 amfid 状态变化；
`oslog` 在失败 exec 时**无任何 AMFI/exec 日志**（静默拒绝）。

**下一步**：确认 MIG-27001 到底是哪个用户态服务，以及**它为什么对 arm64ified 的
macOS 动态二进制返回非 0**（候选：CMS/签名形态、platform 身份、或“首次 exec 前 loader 闭包”。），
可用 Instance2 回溯 27001 子系统的注册/处理函数。

### 2026-09-26 19:5x — MIG-27001 是“按发起进程”判定（Instance2 反汇编）

exec helper 调用 MIG 前的参数构造（`0xfffffe000839bb90`）：
```
ADRP X26, _kernproc ; LDR X8,[X26,_kernproc] ; CMP X8,X19
B.EQ LBBAA8          ; 若 x19==kernproc → W1=0
LDR  W1, [X19,#0x60] ; 否则 W1 = 调用进程 (*x19) 的 field 0x60
... BL sub_FFFFFE000839BF5C   ; W1 作为 MIG 请求体发给 27001
```

⇒ **这个用户态策略服务是根据“发起 exec 的进程”的身份/策略来判定放不放行的**
（很可能是 launch-constraint / responsible-process 类检查）。
所以 EACCES 可能与**调用者（launcher / 测试脚本）的上下文、launch type、
responsible process** 有关，而不只是目标二进制本身。
（注：`launchdchrootexec` 只在 `getppid()==1 && XPC_SERVICE_NAME` 时才设
`CS_LAUNCH_TYPE_SYSTEM_SERVICE`；从 SSH 直接跑则是普通 launch type。）

### 2026-09-26 20:0x — 用 XNU/dyld 官方源码把 EACCES 闭环彻底解开

**源码位置（原 AI 已落盘，不再靠 /tmp）：**
`analysis/xnu-xnu-8792.81.2/`（内核 8792.82.2 的最近公开 tag）与
`analysis/dyld-dyld-1286.10/`（与设备 dyld 精确同版本：`dyldMain.cpp`）。

**EACCES 确切来源 = `kern_exec.c` 的 `process_signature`：**
- `kern_exec.c:7430`：`if (imgp->ip_csflags & CS_SIGNED) { error=0; goto done; }` ——**有 `CS_SIGNED` 就跳过后续 upcall**。
- `kern_exec.c:7459`：否则调 `find_code_signature(port, new_pid)` = **MIG 27001**
  （`osfmk/mach/task_access.defs:55-57`：subsystem 27000 的第 2 个例程，回复 27101），
  参数 `new_pid = proc_getpid(p)` ——**所以 `p->0x60` 就是 `p_pid`**（kernproc 返回 0）。
- 返回 `KERN_FAILURE(5)` → `os_reason(EXEC,8)`；其他非 0 → `os_reason(EXEC,9)`；**均 EACCES**。
- spawn 场景（`imgp` flags bit 0x10 = IMGPF_SPAWN）同一拒绝改走 `psignal_vfork_with_reason(SIGKILL)`
  （`kern_exec.c:7558-7573`）——**这就是 137 与 13 同源的确切位置**。
- `kern_exec.c:7506`：`CS_SIGNED` 是 **upcall 成功后**才 `proc_csflags_set(p, CS_SIGNED|CS_VALID)`。
- upcall 成功后仅接受“最朴素 ad-hoc”（`kern_exec.c:7490-7507`）：
  `(csb_flags & CS_ALLOWED_MACHO)==CS_ADHOC` 且 **无 CMS blob、非 platform、无 entitlements**。

**27001 服务 = 用户态 taskgated/amfid**（XNU 只有 .defs + 客户端桩 + `task_access_port`；
server 实现属闭源 AMFI 生态；`amfid` 一直在设备上在线）。

**当前状态**：设备上 arm64ified+重签+`jbctl trustcache add` 后**仍 EACCES**
（试过 CD flags=0x2/CS_ADHOC、CS_HARD|KILL|RUNTIME、platform 字节 1/2/5、identifier 重签、
loader 闭包入 tc、launchd 作业 launch type——均 13）。
⇒ **推断：AMFI 没把我们的 cdhash 当作有效信任 → 不置 `CS_SIGNED` → 走 upcall 被拒**。
下一步：验证“jbctl/libjailbreak 加的 root trustcache”是否就是 AMFI 看的 loaded trust cache
（对比 `mac_vnode_check_signature`/`pmap_lookup_in_loaded_trust_caches` 与 `CS_TRUST_CACHE_AMFID`）；
或用 `csops(CS_OPS_STATUS)` 在运行进程上直接读 `CS_SIGNED` 位确认。

### 2026-09-26 20:1x — csops 实证：**能跑的都是 CS_PLATFORM_BINARY**

用 `analysis/dyldwork/csops_probe.py`（`csops(CS_OPS_STATUS/CDHASH)`）读**运行中进程**的真实 cs_flags：
```
cachereg : flags=0x26803b0d  SIGNED=Y PLATFORM=Y HARD=Y KILL=Y
python3  : flags=0x26803b09  SIGNED=Y PLATFORM=Y
jbctl    : CD flags=0x2(adhoc)  entitlements 含 platform-application
launchdchrootexec: CD flags=0x0  (能跑)
/usr/bin/true    : CD flags=0x0  (EACCES)
```

⇒ **`CS_PLATFORM_BINARY(0x04000000)` 是 exec 准入的判别因子**：
能跑的进程都带它；我们的 macOS 二进制不带 → 走 taskgated upcall → EACCES。

**XNU 源码佐证**：`csb_platform_binary` = `!!(csb_flags & CS_PLATFORM_BINARY)`
（`ubc_subr.c:4313-4321`）；而 `csb_flags` 由 **AMFI 的 `mac_vnode_check_signature`** 决定
（`ubc_subr.c:4264`）；AMFI 内部在验证通过后 `*a5 = v43 | 0x20000000`（置 CS_SIGNED）
并对带 entitlements 的 `CS_PLATFORM_BINARY` 调 `OSEntitlements::markAsCSPlatform`。

**综合结论（本 session 定论）**：
- **137 根因**：非 `ARM64/ALL`（用 `arm64ify_macho.py` relabel 解决）。
- **EACCES 根因**：内核 `process_signature` 的 taskgated/amfid upcall（MIG 27001）拒绝——
  因为目标二进制**未被 AMFI 认定为有效签名（无 `CS_SIGNED`/非 `CS_PLATFORM_BINARY`）**。
- 纯用户态使二进制成为 platform 的可行途径：**把它以“platform”形态入 trustcache / 或带
  platform-application 同时满足 AMFI 验证**（具体形态待验证；CD platform 字节 1/2/5 无效）。

### 2026-09-26 20:2x — ★★★ 重大突破：exec 准入打通，macOS dyld 已能真正运行 ★★★

**EACCES(13) 的解法（纯用户态，无需 patch 内核/amfid）——精确复刻能跑的 `cachereg` 的签名形态：**

```
python3 misc/arm64ify_macho.py <file>                     # 1) ARM64/ALL（否则 137）
ldid -Hsha256 -Cadhoc -S<entitlements.plist> <file>       # 2) sha256 cdhash + CS_ADHOC
jbctl trustcache add <sha256 cdhash>                      # 3) 入越狱 trustcache
rm <dest>; cp <file> <dest>                               # 4) fresh inode
```

**关键：`-Hsha256` 是必需的。** trustcache 条目带 `hash_type` 字段（`osfmk/kern/trustcache.h` 的
`trust_cache_entry1{cdhash,hash_type,flags}`；`CS_TRUST_CACHE_AMFID=0x1`）；**只有 sha256 的 cdhash
（20B sha256(CD)）才与条目类型匹配 → AMFI 认 → 置 `CS_SIGNED` → 跳过 taskgated upcall**。
之前的 `-Cadhoc`（hashType=1 sha1）都仍 EACCES，就是因为 hash_type 对不上。

**实测结果（部署脚本 `analysis/dyldwork/remote_apply_form.sh` / `remote_deploy_dyld.sh`）：**
```
true / echo  -> 不再 137/13，macOS dyld 真正执行：
  dyld: dyld cache '(null)' not loaded: syscall to map cache into shared region failed
  dyld: Library not loaded: /usr/lib/libutil.dylib ...
ls / bash    -> 同样dyld运行、fallback 到磁盘
```

⇒ **工作重心已从“exec 门”回到原任务核心：syscall 536 映射缓存**
（与 HANDOVER §4 的门禁链、与 `dyld_cleanB_err` 探针完全对应）。

**可复现的部署命令**（逐文件，需对 dyld + 要跑的二进制都做；设备端脚本见 `analysis/dyldwork/`）：
```sh
A64=/var/mobile/arm64ify_macho.py; ENT=/var/jb/usr/macOS/bin/entitlements.plist
LD=/var/jb/usr/bin/ldid; JB=/var/jb/usr/bin/jbctl
python3 $A64 <file>; cp <file> /tmp/w.bin
$LD -Hsha256 -Cadhoc -S"$ENT" /tmp/w.bin
for a in arm64 arm64e; do $JB trustcache add $($LD -arch $a -h /tmp/w.bin|grep CDHash=|cut -c8-); done
rm <dest>; cp /tmp/w.bin <dest>
```

**顺带的其它确认**：设备 `/usr/libexec/amfid` 是**原版**（未 patch，不在越狱 tc）；
exec 准入靠的确实是 `jbctl trustcache`（凭 sha256 cdhash 让 AMFI 直给 `CS_SIGNED`）。

### 2026-09-26 20:4x — 回到 syscall 536：源码级根因分析（subagent + 逐个源文件）

部署带补丁的 `dyld_cleanB` 后，macOS dyld 执行、但 536 失败、退磁盘 fallback：
```
dyld: dyld cache '(null)' not loaded: syscall to map cache into shared region failed
dyld: Library not loaded: /usr/lib/libSystem.B.dylib
  ... '/usr/lib/libSystem.B.dylib' (code signature invalid ... errno=1)
```

**536 的内核侧全路径（`analysis/xnu-xnu-8792.81.2/bsd/vm/vm_unix.c` + `osfmk/vm/vm_shared_region.c`）：**
- 包装层 `shared_region_map_and_slide_2_np` `vm_unix.c:2842-2964`（files_count==0 → **成功**；mappings>2048 → EINVAL）。
- `_shared_region_map_and_slide` `vm_unix.c:2664-2748`：errno 映射 L2712-2730
  （INVALID_ADDRESS→**EFAULT(14)**、PROTECTION_FAILURE→**EPERM(1)**、NO_SPACE→ENOMEM、其余→**EINVAL(22)**）。
- `shared_region_map_and_slide_setup` `vm_unix.c:2189-2652`：卷不符→**EPERM**（L2264/2514）；
  非 VREG→EINVAL；uid!=0→EPERM；**gate11 `!ubc_cs_is_range_codesigned`→EINVAL（L2620-2642）**。
- 引擎层 `vm_shared_region.c`：**`sr_first_mapping != -1`（region 已填充）→ KERN_FAILURE→EINVAL（L1464-1470）**；
  FIXED 越界→KERN_INVALID_ADDRESS→**EFAULT(14)**（`vm_map.c:2714-2718`）。
- 区域尺寸 `shared_region.h:90-91`：ARM64 base `0x180000000`、size **4GB**。

**gate11 `ubc_cs_is_range_codesigned`（`ubc_subr.c:5545-5589`）**：要求单个 blob 整段覆盖
`[file_offset, file_offset+size]`。`cachereg`（host `fcntl(fd,F_ADDFILESIGS=61)`，`kern_descrip.c:3957-3990`）
挂的 blob 满足：主缓存 `csb_base_offset=0/csb_start_offset=0/csb_end_offset=0xa160c000` ⊇ 8 条 mapping。
**但必须常驻**（vnode 回收→blob 释放 `ubc_subr.c:4906-4943`），**且 `.01` 子缓存需各自 blob**。

**dynregion（fd=-1）（`vm_unix.c:2286-2320` + `vm_shared_region.c:1534-1669`）**：
`mappings_count==1`、`sms_address/sms_size` 页对齐、`sms_file_offset` 是**用户态指针**（`copyin` L1600）；
内容来自 mapper 私有内存 → **per-mapper，mapper 退出即消失**（`vm_shared_region.c:538-625`）。

**dyld 失败分支**：`SharedCacheRuntime.cpp:1367`(syscall) / **1379-1387**(失败) / 1384-1385（该错误串）；
打印在 `DyldProcessConfig.cpp:1516`。fallback：`reuseExistingCache`(L1490)→`mapSplitCacheSystemWide`(L1495)
都失败 → 无缓存 → 逐文件从磁盘加载 → libSystem 仅存于缓存 → 报 `Library not loaded`。

**⇒ 当前 536 失败的最可能原因（源级，排序）：**
1. **region 已填充** → `vm_shared_region.c:1464` → **EINVAL(22)**；
   （`check_np` syscall 294 实测 ret=0 → 区域确实已存在）。`dyld_cleanB` 强走 map 路径（`0x30140/0x34298`）→ 撞此。
2. gate11 blob 失效/ `.01` 缺 blob → EINVAL。
3. FIXED 越界（`.01` 尾）→ EFAULT。
4. 卷不符/uid → EPERM。

**修复路线（源级可行，未实现）：**
- **常驻 keeper**（=mapper 本体长驻）持 region 引用不销毁 → 其余进程走 `reuseExistingCache`（勿再强走 map）；
- **冷启动后首交**（region 空，绕开 L1464）；
- cachereg 双缓存（主+.01）blob 常驻；
- `.01` 越 `0x280000000` 的尾映射改私有 `mmap`（`mapSplitCachePrivate`）。

**errno 读取未完成（未决）**：在 0x35698/0x356d8/0x35714 插 syscall 探针（写 8B 到 fd2）→
要么被内核当“attach 半途”杀（137），要么未产出字节；改用真死洞 `0x38d08` 的 cave 也未产出。
下一步用 **kernel RE（Instance2）** 直接跟 `vm_unix.c:2620/1464` 对应的内核函数，或冷启动后首交验证。

**errno 为何取不到（已证的结构性原因）**：chroot 里**任何动态 macOS 二进制都要缓存里的 `libdyld`**
（`dyld: libdyld.dylib not found`），静态二进制却撞**静态门→137**（已实测：静态 arm64 探针 rc=137；
sprobe rc=134=dyld 报错）。→ **userland 探针路已断**，只能靠内核 RE 或冷启动首交。
把 sprobe 的 `LC_LOAD_DYLIB(libSystem)` 改成 `LC_LOAD_WEAK_DYLIB` 又暴露注入的 `libmachook` 需 `libobjc`（同样在缓存里）。
即前位 AI 的 sprobe “能跑”是陈旧结论（`TOOLS-AND-PORTING.md:585` 实际只得到 rc=134）。

**当前设备状态**：cachereg(pid 751) 已同时持有主缓存+`.01`（gate11 blob 已满足）；dyld = `dyld_cleanB`（已用新形态部署）。
**一键冷启动验证脚本**：`analysis/dyldwork/coldboot_firstsubmit.sh`（重启+重越狱后跑：以 cleanB 作**首个 536 提交者** → 若 region 空则可能返 0 并真映射，再看 `sw_vers`）。

### 2026-09-26 20:5x — ★ 又通一道门：**LC_BUILD_VERSION platform 必须 = macOS(1)**

用 `DYLD_PRINT_CACHES=1` 发现**新错误**：`shared cache file is for a different platform`。
根因（源码 `dyld/SharedCacheRuntime.cpp:143-154` `validPlatform()`）：`cache->header.platform != options.platform` 即拒；
而 `options.platform = process.platform`（`DyldProcessConfig.cpp:1365`）= **主二进制的 LC platform**。
- 实查：`cache` 头 `platform` @**0xD8** = **1 (macOS)**；而设备上的 `true`/`ls`/`echo`/`bash` 的
  `LC_BUILD_VERSION platform` = **2 (iOS)**（之前不知何处被设成 iOS）→ **被 dyld 拒**。
- **修复**：跑 `misc/set_macos_version.py`（iOS→macOS）对这四个二进制重设 → **platform=1** → 重签+trustcache。
- **效果**：错误从 “different platform” **变回** `syscall to map cache into shared region failed`（即 536），
  **且 exec 仍通**（不再 137/13）★ **exec 准入与 platform=macOS 兼容，无需 iOS tag**。

**⇒ 现在只剩 536 一道门**（见下节的内核门表：最可能门 B `ubc_cs_blob_get` CS 覆盖 / 门 C uid&volume）。

### 2026-09-26 21:0x — ★ 536 的**真正墙**已钉死：Sandbox 要 `VSHAREDCACHE`

前位 AI 已在 `docs/porting/kernel-syscall536-finding.md` 实测（exit-probe 6/6）：**536 返回 40（EMSGSIZE）**，
来源唯一：**Sandbox `mpo_file_check_mmap` = `hook_file_check_mmap` @ `0xa659664`**（`mac_policy_ops`+0x120）：
```
TBZ W3,#2,.ret0                 // (prot & 4)==0 → 返回 0
vnode_isdyldsharedcache(vp)      // =(vp->v_flag>>9)&1  i.e. VSHAREDCACHE
CBZ W0,.evaluate                // 非 shared-cache → 评估
.evaluate: return cred_sb_evaluate(cred, 16 /*file-map-executable*/, …)
```
- 门外：AMFI `_file_check_mmap` @ `0x92a1a90` 只返 {0,1}；AppleImage4 无该 hook；无任何 40 字面量。
- 调用点：源 `vm_unix.c:2372` `mac_file_check_mmap(cred, fg, **VM_PROT_ALL=7**, MAP_FILE|MAP_PRIVATE|MAP_FIXED, 0, …)` —— flags/prot 是**常量**。
- **我校验**：`true` 已带 `com.apple.private.security.no-sandbox` + `platform-application`，**仍返 40**。
- **我校验**：内核里 `vnode_isdyldsharedcache` 的调用者仅 Sandbox hook + **APFS**（`_apfs_vnop_inactive`/`_apfs_vnop_pagein`）
  → 即 `VSHAREDCACHE` 由 **APFS 驱动在内核态**置位；全仓无既有绕法。

**结论**：外来 macOS 缓存挂在 rootfs DMG 上，vnode 永不带 `VSHAREDCACHE` → sandbox 必拒 → **536 永返 40**。
这是**纯用户态不可绕**的墙（除非：① 有内核写原语去设该 vnode 标志/ nop 该 hook；② 让 APFS 把该缓存当作已识别的 dyld 缓存）。
（对照：原作者 iOS 16.5 项目能跑——16.3 与 16.5 在此 hook 上的差异值得下一步比对。）

### 2026-09-26 21:1x — ★★ 墙可解：项目其实有完整**内核写 + kcall**

复核 `/var/jb/basebin/libjailbreak.dylib`（Dopamine）的导出符号（`nm -gU`，共 397 个）：
```
__kwritebuf_phys        # 内核写（physical）
__physwritebuf_virt     # 内核写（virt→phys）
__kreadbuf_phys / __physreadbuf_virt   # 内核读
_kalloc / _kalloc_with_options         # 内核 malloc
_arm64_kcall / _is_kcall_available / _arm64_kcall_prepare_state  # kcall
__boomerang_get_physrw / _jbclient_root_get_physrw               # phys r/w
```
⇒ **"只有 kread"是旧结论；实际有 kwrite + kcall。** 因此 sandbox 墙（`VSHAREDCACHE`）有两条可行解：
1. **设 vnode 标志（推荐，数据写、不碰内核 text/KTRR）**：用 kread 从 mount/vnode 链找到
   该缓存的 vnode，把 `v_flags@vp+0x54` 的 **bit9（VSHAREDCACHE）置 1** → sandbox hook 短路返回 0。
2. **patсh sandbox hook**（`hook_file_check_mmap` @ `0xa659664` → `mov w0,#0; ret`）——需写内核 text，A12+ 有 KTRR，风险高，不推荐。
（注：这不是“lazy bypass”——`VSHAREDCACHE` 本就是内核给 dyld 缓存 vnode 打的标记，此处只是把它补上；待验证。）

### 2026-09-26 21:2x — ★★ KRW 打通内核 walk；**纠正：目标 vnode 已带 VSHARED_DYLD**

**用设备端 python3 + ctypes 驱动 Dopamine KRW（免编译）已全线打通：**
- `kread64/kread32/kwrite64/kwrite32` 均可用（`proc_self` 返回真内核指针）。
- 打通结构链：`proc+0xF8 → fd_ofiles → [fd] → +0x10 → fileglob → +0x38 → vnode`。
- **两个坑已解**：① arm64e **PAC 是 47-bit VA**，剥位=`0xffff800000000000|(v&0x7FFFFFFFFFFF)`（非 48bit）；
  ② python 里 `open(...).fileno()` 会因文件对象被 GC 而**立即 close(2)**，必须保持对象存活。
- 工具：`analysis/dyldwork/set_vshared.py`（定位任意 fd 对应文件的 vnode）+ `kwalk*.py`。

**实测目标 vnode**（`v_name = 'dyld_shared_cache_arm64e'`，`ubc` 存在）：
```
vnode+0x50/0x54: v_flag = 0x84a00   → bit9(VSHARED_DYLD) = 1  ★已置位★
```
⇒ **sandbox `hook_file_check_mmap` 的 `!VSHAREDCACHE` 分支不会走** —— 即 **doc 里实测的 errno 40（sandbox）
对当前这个 vnode 不成立**（很可能 40 来自另一条/另一 vnode 的路径）。由于该 vnode 已置位、536 仍失败，
**真正的失败点回到门 B（`ubc_cs_blob_get` CS 覆盖）或门 C（uid/volume）**。

**下一步**：用 KRW ① 读 `ubc_cs_blob_get` 看 blob 对 8 条 mapping 的覆盖；② 读 vnode 的 v_mount 与 root 卷/`/private/preboot/Cryptexes` 比对；
③ 读 shared_region 的 `sr_first_mapping`（门 A “已填充”）。三选一钉死后对症修复。

### 2026-09-26 21:3x — ⛔ 内核裸写 v_mount → PAC panic（已回滚/重启）；门 C 坐实

尝试用 KRW 直接改缓存 vnode 的 `v_mount`（@vp+0xD8）→ 写完后内核报：
```
panic(cpu 5): Break 0xC472 instruction exception from kernel.
  Ptrauth failure with DA key, at pc 0xfffffe002376651c, lr 0xfffffe00205c1c
  x0 = 0xfffffe13055e6c38   ← = 缓存 vnode(0xfffffe13055e6b60) + 0xD8 (=v_mount)
```
**根因**：`vnode->v_mount` 是 **PAC 签名指针（DA key）**；`kwrite64` 写入了**未签名裸指针** → 后续任何访问该字段的路径 → 指针认证失败 → panic → iPad 重启。
**确认**：panic 现场正好落在 `vp+0xD8` → 证明该偏移就是 `v_mount`（门 C 判定成立）。
**教训**（已写入 memory）：改内核结构**指针字段**必须重新 PAC 签名；只有**非指针字段**（如 `v_flag`）可安全写入。

**重启后**：iPad 已重新越狱、SSH 恢复；重建 trustcache + 重部 dyld 后，`true/echo` → 137、`ls/bash` 无输出，**缓存仍未映射**。

**当前状态与结论**：
- 两道 exec 门 + platform 门 ✅；macOS dyld 能运行。
- **536 仍未过**，且与 `kernel-syscall536-finding.md` 的“外来缓存结构性不可满足”一致：
  挂在 rootfs DMG 上的 macOS 缓存，其 mount≠root卷/`/private/preboot/Cryptexes`（门 C，EPERM），
  且改 v_mount 属 PAC 指针写 → panic。**纯用户态在该设备上无解**。
- **待你决定的方向**：A) 把缓存改挂到与 root卷/`/private/preboot/Cryptexes` 匹配的位置（零内核风险，但需改 rootfs 布局）；
  B) 找到内核自带的“已签名 v_mount”来源做受控替换（需先取证 ptrauth 判别子是否地址相关）；
  C) 明确授权内核补丁路线（KTRR 风险，A12+ 设备不保证可行）。

### 2026-09-26 21:4x — ★ 原版 macOS 13 做法考古（subagent）+ 方向修正

**用户提示“看原版 macOS 13 怎么实现” → subagent 精读仓库文档 + 上游安装法，结论：**
1. **原版把 OS cryptex（含 `dyld_shared_cache_arm64e` + `.01`）解包进 chroot rootdir 卷
   （= 数据卷 `/var/mnt/rootfs` 树）内的 `System/Volumes/Preboot/Cryptexes/OS/…`**
   （上游 MacWSBootingGuide 安装步骤25-27）。这**同时命中门8“同一 mount”**（rootfs=数据卷）
   与门9 `scdir_enforce` 的父目录名。
2. **原版 macOS 13 缓存 ≈1.6G < 4GB** shared region → **天然放得下，无需裁剪/混合映射**。
3. **原版对 cachereg / F_ADDFILESIGS / VSHAREDCACHE / sandbox 零处理** —— 只把缓存 CDHash 入 trustcache
   （`layout/usr/macOS/bin/postinst.sh:1010-1034`，13.4 分支两枚 cdhash）。⇒ 这些墙是**把方案搬到 15.6.1 才撞上的新墙**。
4. **原版文档从未提及 syscall 536 或“缓存必须放 X 卷”**；该卷规则是 15.6.1 移植会话用 IDA 反编译 `sub_8459570` 才发现的。

**【重要修正】** 我先前推的“把缓存改挂到 `/private/preboot`”是**误读**（已撤销）：门 C 要的是 **rootdir 卷（数据卷）**，
而当前缓存就在数据卷上，**门 C 应已满足**。真正剩下的更可能是 **门 B（CS 覆盖）** 或 **sandbox**——
重启后已重跑 `cachereg`（输出 `fcntl=0` 成功，blob `cso=0xa160c000` 已挂），但 `true`/`sw_vers` 仍 rc=1、无输出（exec 软状态待重建）。

**下一步（未定）**：重启后重跑测试前，需先重建 exec 软状态（trustcache/arm64ify）并确认 `true` 能过 exec；
若 536 仍失败，则重点回到**门 B 的 blob 覆盖**与 **sandbox 的 `file-map-executable`**（后者可查 vnode 的 `VSHARED_DYLD` 实际生效性）。


## 2026-09-26 17:50 — ★ POST-REBOOT RESTORE + BLOB-ARTIFACT CORRECTION + HANDOVER ★

**NEW comprehensive handover doc**: `docs/porting/HANDOVER-15.6.1-2026-09-26.md`
— self-contained reproduction guide for a fresh agent. State below assumes it.

**Persistent artifacts moved**: kernel now at
`analysis/kc_raw_16.3_T8112.bin` (80052224 B, imagebase `0xfffffe0007004000`) +
`analysis/kernelcache_16.3_T8112.img4`. `/tmp` wipes no longer lose them.

**Post-reboot restore procedure (verified working)**:
- trustcache tool = `/var/jb/basebin/jbctl` (`trustcache info|add <cdhash>`;
  the old hvfs shim + `.trustcache` file paths are gone).
- cachereg holder redeployed at `/var/mobile/cachereg`; one stale instance
  (PID 1355) was auto-restarted post-Dopamine holding the preboot paths.
- 5 hashes confirmed IN: proof2 `b2b3a8b8…`, dyld_pm `10bc320f…`, main cache
  `2b9cccd5…`, .01 `8c7ba7e5…`, cachereg `f176402d…`.

**dyld_pi full diff vs pristine** (byte-level, all sites):
`0x76270` movx0,0;ret (crossarch) | `0x30140` movz w0,#0;ret
(hasExistingDyldCache→0) | `0x34298` movz w0,#0 (reuse→0) |
`0x3538c` movz w28,#1 (NOT files_count — it's a mapSplit flag) |
`0x35fc8` movz x9,#0x7800,lsl#16 + preserved `add` @0x35fd8 (dynregion
submit → 0x1f8000000) | `0x50dfc` movz x8,#0x7800,lsl#16 (accessor) |
**`0x3576c-0x35afe` = injected blob replacing `preflightMainCacheFile`**
(open+fctl(F_ADDFILESIGS)+header-parse+CacheInfo fill, dumps
`/tmp/MTOUT5.txt`; called from `0x3537c`).

**⚠ CORRECTION — the "103" runs were a measurement artifact**: dyld_pm =
dyld_pi + glueprobe2 @ `0x3588c` + `b` @0x6b94, but `0x3588c` is INSIDE the
live injected blob (not dead code). So `true`/`ls` exit 103 = blob hit the
probe mid-flight; dumped regs (x8=1, x9=0x228, x10=0x50c000…) are blob
internal state, NOT the glue-call site. Whether normal flow reaches
`0x6b94 blraaz x8` is still UNVERIFIED. Next probe must use a real dead
region (e.g. __text tail padding) and only patch `blraaz→b`.

**NEW MYSTERY — proof2 137**: locally-built arm64e test exe (ctor→
`/tmp/ctor_ran`, main→`/tmp/main_ran`+stdout+ret7) gets SIGKILL even with
its cdhash live in trustcache, while system binaries (`true`,`ls`) run dyld
fine. Suspect AMFI launch constraint on non-Apple CodeDirectory shape /
platform. `true`→103, `ls`→103, `proof2`→137, all hashes trusted.

## 2026-09-26 17:00 — ★★ DYNREGION EPHEMERALITY FULLY DECODED (current blocker) ★★

**Observed pattern**: after any successful map run → first child exec works
(T1=0), every subsequent exec SEGVs at `0x1f8000000`
(`KERN_INVALID_ADDRESS … not in any region`) even though the file-backed
cache mappings are still present in the shared region (crash dumps show
`180000000-1e7f5c000 __TEXT SM=COW` alive). Conclusion (runtime-confirmed):
**the fd=-1 "dynamic" mapping is torn down when the mapping process exits;
file-backed entries persist.** On real macOS this doesn't matter because the
boot-time mapper stays alive / the region is populated once.

**Exact kill site on subsequent execs** (IDA-confirmed, Instance1 dyld IDB):
`start` → `SyscallDelegate::hasExistingDyldCache` @0x30140 (called at
start+0x5a8c) → `shared_region_check_np` returns base →
`DyldSharedCache::dynamicRegion(base)` (0x50dfc, patched → 0x1f8000000) →
`DynamicRegion::getDyldCacheFileID` derefs → SEGV. This fires **before**
`loadDyldCache`, so even a forced map path can't save a populated region —
the crash is in the earliest "is there a cache?" probe.

Call graph (all IDA-verified):
```
start 0x53dc → hasExistingDyldCache 0x30140 → dynamicRegion() → DEREF (boom #1)
loadDyldCache 0x34240 → reuseExistingCache 0x351a8 → same deref (boom #2)
                  → mapSplitCacheSystemWide 0x352bc → syscall 536
                  → reuseExistingCache again at 0x356d8 (post-syscall verify)
```

**Probe harness that finally works** (deploy + measure without files):
exit-code probes need `movz w0,#N; movz x16,#1; svc #0x80` — **x16 is the
syscall selector, `svc #0x80` is just a marker**; an `svc` without x16=1
invokes a random syscall (we measured 140=SIGSYS from garbage x16, and once
"exit(85)" because `movz w0,#0x55` encodes `a8 0a 80 52` not `#0x51`).
movz imm16 occupies insn bits [20:5]: exit(N) = `movz w0,#N` byte0 =
(N&7)<<5, byte1 = N>>3.

Deployed base for all probes = `/var/mobile/dyld_pb` (patches: P1 crossarch,
W28=1 @0x3538c, dynreloc `movz x9,#0x7800,lsl#16` @0x35fc8 + preserved add
@0x35fd8, `movz x8,#0x7800,lsl#16` @0x50dfc, **plus two leftover patches
found in it**: `movz w0,#0` @0x34298 = reuse-call→0 (forces map path
always) and `b 0x3576c` @0x35698 = skip post-syscall tail).

Probe results with correct encoding:
- exit(0x51)@0x351a8 + exit(0x52)@0x352bc + exit(0x53)@0x35698 → all runs 82
  (map path always taken; reuse never entered because of the 0x34298 patch).
- exit(syscall_ret&0xff)@0x35698 (`uxtb w0,w0`): T1=**0** = 536 succeeded on
  empty region; T2-T4=139 = die BEFORE the post-syscall site → inside the
  earlier `hasExistingDyldCache` deref on the still-populated region.

**Remaining question**: does re-submitting 536 on an already-populated
region re-create the dynregion entry? To measure: patch out the early
derefs (`hasExistingDyldCache` 0x30140 → return 0; `reuseExistingCache`
0x351a8 → return 0) so the map path always runs, then read errno at
0x35698. If 0 → "always-map" is the fix (each exec self-heals dynregion).
If EINVAL/EBUSY → need a persistent keeper OR kernel-behavior workaround.

Alternative theory (NOT yet verified): entries tagged dynamic may be
per-mapper and die with the mapper — if so, a permanently resident chroot
"cache keeper" that performs the one-time map would keep dynregion alive
for all later processes. Untestable via `sleep` because a fresh exec on a
populated region dies at `hasExistingDyldCache` first — the keeper must be
the *first* process after the region is created (chicken-and-egg unless
we wipe/reset the region or the keeper itself is the only mapper and
everything else reuses).

## 2026-09-26 (post-breakthrough) — ★★ STRUCTURAL WALL FOUND: 15.6.1 CACHE > iOS 4GB REGION ★★

**The macOS 15.6.1 cache cannot fully fit in the iOS 16.3 shared region.**
This is a kernel-constant limitation, NOT a validation failure.

- iOS 16.3 (xnu-8792.81.2 `osfmk/mach/shared_region.h`):
  `SHARED_REGION_BASE_ARM64=0x180000000`, `SHARED_REGION_SIZE_ARM64=0x100000000` (4 GB)
- macOS 15.x (xnu-11215.81.4 same header — fetched from apple-oss-distributions):
  `SHARED_REGION_SIZE_ARM64=0x180000000` (**6 GB**). Apple grew the region.
- 15.6.1 cache virtual span: `0x180000000 → 0x2ac75c000` + dynregion 0x4000
  ≈ **4.69 GB → overflows the iOS region by ~0.70 GB**.
- `sr_map` submap max_offset is baked to sr_size at creation
  (`vm_shared_region.c:775` `vm_map_create_options(pmap_nested, 0, size)`);
  pmap nesting region is also 4 GB. Out-of-bounds `vm_map_enter` →
  KERN_* → errno (EFAULT/ENOMEM/EINVAL), whole submission rolled back.
  Region size is a kernel-immediate constant — cannot change without
  kernel patch (none available under Dopamine).

### Exact overflow map (from cache headers, verified)

Main file — 8 mappings, ALL fit:
`0x180000000…0x22560c000` (end), fileoff 0…0x7ad4c000. CS cso=0xa160c000 css=0x50c000.

.01 — 7 mappings, **partially overflows**:
```
m0 0x22560c000+0x54808000 → 0x279e14000   ✓ fits
m1 0x279e14000+0x21c4000  → 0x27bfd8000   ✓ fits
m2 0x27dfd8000+0x38b4000  → 0x28188c000   ✗ starts in-bounds, ends OUT
m3 0x28188c000…0x28261c000                ✗ all out
m4 0x28261c000…0x286bf0000                ✗ out
m5 0x288bf0000…0x288dcc000                ✗ out
m6 0x288dcc000…0x2ac75c000                ✗ out
```
.01 CS cso=0x83150000 css=0x41c000.
dynregion (fd=-1 anon, size DynamicRegion::size(), dynMax=0x4000):
submitted VA = `regionBase+header[0x1F0]` = `0x2ac75c000` → out.

Free gap inside region after .01-m1: `[0x27bfd8000, 0x27dfd8000)` ≈ 32 MB.
Chosen dynregion relocation VA: **0x27c000000** (region offset
0xfc000000; single-insn `movz x8,#0xfc00,lsl#16` / VA `movz #0x27c00,lsl#16`).

### dyld patch sites for hybrid plan (thin-slice offsets, byte-verified)

| off | orig | new | why |
|-----|------|-----|-----|
| 0x76270 | `01 10 00 d4 c0 03 5f d6` | `e0 03 1f aa c0 03 5f d6` | P1 crossarch noop |
| 0x351f0 | strcmp-result test | force "not equal" | P5 reject iOS cache → real map path |
| 0x35fd8 | `add x9,x9,x11` (`09 01 0b 8b`) | `movz x9,#0x27c00,lsl#16` (`09 80 ef d2`) | dynregion VA → 0x27c000000 (CacheInfo+0x1B0; consumed at 0x35660 `ldr x24,[x19,#0x11d0]`) |
| 0x50dfc | `ldr x8,[x0,#0x1f0]` (`08 f8 40 f9`) | `movz x8,#0xfc00,lsl#16` (`08 80 bf d2`) | `dynamicRegion()` returns base+0xfc000000 = 0x27c000000 — consistent with relocated map |

Submission construction (0x35660-0x35694): `stp x24,x0,[x8]` = sms_address/sms_size;
`str x22,[x8,#0x10]` = sms_file_offset = dyld-side copyin source ptr;
prot word at +0x28 = 1.

### Trimmed-set plan (in progress)

Phase A (now): files_count=1 patch (0x3538c `ldr w28,[x19,#0x1a8]`→`mov w28,#1`
= `3c 00 80 52`) → submit main 8 + dynregion only → `true` should exec on
the REAL macOS cache (verify via `_dyld_get_shared_cache_uuid`).

Phase B: custom .01 — copy file, patch header `mappingCount` 7→2 (keeps
m0,m1), **re-sign whole file** (ldid/self CD), cdhash → jbctl trustcache,
F_ADDFILESIGS → vnode blob. dyld then submits 2+1 files naturally.

Phase C: .01 tail (m2..m6 = fileoff 0x569cc000..EOF → VAs
0x27dfd8000..0x2ac75c000) private-mmap'd PROT_* per initprot by a pre-main
hook (libmachook ctor / dyld patch) — file pages validated vs the SAME
custom CD + trustcache. VAs land OUTSIDE the shared region in normal user
space — image enumeration then works transparently.

Phase D: WindowServer deps — audit which images fall >0x280000000.

### Known risk from earlier session (retained)

`pmap_trim_internal` panic on `true` exit — likely triggered while an
invalid shared-region mapping existed. Now that only in-bounds, CS-valid
mappings are submitted, this specific panic path should not recur; still,
avoid fd=-1 mappings whose copyin source is invalid (EFAULT→ INVALID_ADDRESS,
not the panic path). If panic recurs, suspect map-engine undo path.

-## 2026-09-26 21:5x — ★★★ exec 137 真正闭环 + 与原版 macOS13 方法对比 ★★★

### 1) exec 137 的真因（本轮新定）：**launcher 注入的 dylib 未受信**

沿调用链后发现：
```
launchdchrootexec 以 posix_spawn(SETEXEC) 启动 child，并注入
  DYLD_INSERT_LIBRARIES=/usr/local/lib/libmachook_arm64.dylib
```
而该 dylib 当时 **不在 trustcache**（cdhash `21f69e8c…`，`flags=0x0`）。
⇒ 子进程 exec 因“**注入的库未受信**”被内核 SIGKILL（137）。

**修复（“用 trustcache 绕 gate”）——对注入 dylib 同样做四步：**
```
arm64ify_macho.py <libmachook_arm64.dylib>
ldid -Hsha256 -Cadhoc -S<entitlements.plist> <libmachook_arm64.dylib>
jbctl trustcache add <sha256 cdhash>
（同时把 libmachook.dylib 的 arm64/arm64e 两个 cdhash 也 add）
```
**效果：`true` 连续 3/3 `rc=0`（不再 137）**。⇒ **exec 准入彻底打通**（主二进制 + 注入库都要）：
主二进制用 `arm64ify + -Hsha256 -Cadhoc + trustcache`，**注入的 libmachook 也要同样处理**。`echo` 也 rc=0。

### 2) 重要排雷：日志归属
`DYLD_PRINT_*` 下看到的 “re-using existing shared cache (/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld/…)”
与整段 782 行缓存映射，**首映射是 `…/dopamine…/launchdchrootexec` ⇒ 那是 iOS 侧 launcher 的 dyld**，
**不是 chroot 内 child 的 macOS dyld**（launcher 未透传 `DYLD_PRINT_*` 给 child，`ceprobe2` 直连 child 时为 0 字节）。
⇒ 判断 child 行为时**不要误把 launcher 日志当成 child 的**。

### 3) 原版 macOS 13 vs 我们 15.6.1（回答：“13 的方法能否用到 15.6？”）
**能部分延用（缓存放置），但 15.6.1 多了三类新墙：**
| 维度 | 原版 macOS13（iPad13,1） | 我们 15.6.1（iPad13,11） |
|---|---|---|
| 缓存放置 | OS cryptex 解包进 rootfs 的 `System/Volumes/Preboot/Cryptexes/OS/…` | **同样已做好，路径解析正确** |
| 体积/4GB | ~1.6G，天然放入 | 2.7G(+.01 2.2G)，**越界→需裁剪/dynregion** |
| exec 准入 | 未撞上 | 需 arm64ify+trustcache+LC platform+**注入库受信**（刚破） |
| CS/sandbox 门 | 未撞上 | `VSHARED_DYLD`/`file-map-executable` 等（15.6.1 特有） |
⇒ **原版方法必要但不充分**；15.6.1 还需解决 4GB 越界 + 更严的 exec/CS 门。

### 4) 本轮其它既定结论（已有专节）
- exec 137 根因 = 非 ARM64/ALL；EACCES = trustcache hash_type（需 sha256）；
- dyld “different platform” = LC_BUILD_VERSION 需 macOS(1)；
- 536 内核门 A/B/C + sandbox `VSHARED_DYLD`；KRW 打通内核 walk；PAC 裸写会 panic。

**下一步**：exec 已稳定（rc=0），重点回到 **child 的 macOS dyld 是否真映射 macOS 缓存**（需把 child 的 dyld 输出单独取到：因其 env 未被 launcher 透传，需改用能透传 env 的启动方式，如自建最小 launcher）。

### 2026-09-26 22:0x — 状态盘点 + 两处更正

1. **launcher `POSIX_SPAWN_SETEXEC` 事实**（`launchdchrootexec/main.m:119/141`）：
   SETEXEC 成功→launcher 进程被替换成 child；其返回码 = **child 的退出码**。
   ⇒ `true` 的 `rc=0` 是**真跑了且 exit 0**（true 本静默）；`echo` 无输出才是“main 未跑”的信号。
2. **文档归属规则已证实**：`DYLD_PRINT_*` 输出中 **`[launchdchrootexec] target=` 横幅之前 = launcher(iOS)dyld，之后 = child(macOS)dyld**。
   实测 child 段**为空** → child 的 macOS dyld 未输出任何内容。
3. **exec 137 = “逐二进制且会抖”**：对 `true/echo/ls/bash/printf` 逐个 `arm64ify+-Hsha256 -Cadhoc+trustcache` 后**时而 rc=0、时而 137**，
   与 HANDOVER §10 “137 不是单一原因…每二进制单独归因”一致。**推断：运行 `postinst.sh` 等批量重签会改写 cdhash，令 trustcache 瞬时不一致 → 137**。

**当前卡点（与 HANDOVER §9 对应）**：
- child 的 macOS dyld → **silent-0**（§9/§10 记为“admission 过了但 main 没跑”的第三类症状）——即 **dyld 未真正加载 libSystem/macOS 缓存**；
- 未决仍为：§9.2 dynregion 持久化、§9.3 glue-call 取证、§9.4 `.01` 尾部混合映射。
**前置（必须先做）**：让设备 exec 进入**稳定态**
（重跑 `postinst.sh` 收官 → 对我需要的每个二进制重做 `arm64ify+-Hsha256 -Cadhoc+trustcache` → 连测 `echo HI` 稳定打印）再做后续打点；否则测量不可复现。

### 2026-09-26 22:3x — ★ 新工具 `dearm64e`：arm64ify 后必须消掉 arm64e 专属分支指令

**发现**：`arm64ify`（只改 cputype/subtype）后，二进制代码仍是 **arm64e**，其中 **arm64e 专属的
指针认证分支/调用指令在 arm64 路径上非法 → SIGILL(132)**：
- `brab* = 0xd61f0800|Rn`、`braa* = 0xd61f0c00|Rn`（branch）
- `blrab* = 0xd63f0800|Rn`、`blraa* = 0xd63f0c00|Rn`（call，含 doc 点名的 `0x6b94 blraaz x8`）
- `retab = 0xd65f0bff`（return）
（注：`pacibsp=0xd503237f`/`autibsp` 是 HINT，在 arm64 上无碍，不必改。）

**工具**（`analysis/dyldwork/build_dyld.py` 里的 `_dearm64e()`，作为构建键 `dearm64e`）：
把所有以上族 → `br Xn`/`blr Xn`/`ret`（保留 Rn）。构建例：
`python3 build_dyld.py dyld_cleanB_ae2 crossarch hasexisting prereuse filescount1 dynoff accessor fcntl_nop cover_b dearm64e`
该 thin dyld 上共转 167 处；验证后 `brab/braa/blrab/blraa/retab` 全为 0。

**但**：即使 dyld 内全清零，`true` 仍 **rc=132(SIGILL)**（`echo` 也无输出）→ **SIGILL 还有第二个源，不在 dyld**
（候选：被 exec 的主二进制自身的 arm64e 残留、注入的 libmachook、或更深层）。当前设备 exec 状态不可复现（137/0/silent-0/132 轮番），
且 132 未落 crash 报告（最新 `true-*.ips` 仅为旧的 16:54 SIGSEGV）。

**下一步（需干净 boot）**：重启+重越狱后，对**主二进制与 libmachook** 也跑 `dearm64e`（同理存在 arm64e 指针分支），
再测 `echo`；若不消，用设备 `find_crash` 取 132 的 crash 报告（PC/指令）定位第二个源。


## 2026-09-26 16:00 — ★★★ BREAKTHROUGH: CACHE MAPPED SUCCESSFULLY ★★★

**syscall 536 returned 0 — the macOS 15.6.1 cache IS mappable on iPadOS 16.3.**

Runtime proof (chroot child, dyld-cave probe):
```
check_np before = 12 (region exists, empty)
F_ADDFILESIGS   = 0  (blob already on vnode — attached host-side earlier)
submit mapping0 = 0  ← SUCCESS
check_np after  = 0, base = 0x180000000 ← region POPULATED w/ macOS cache
```

### The complete gate chain of shared_region_map_and_slide_2_np
(xnu-8792.81.2 `bsd/vm/vm_unix.c:2189 shared_region_map_and_slide_setup`,
source downloaded to `/tmp/dyldwork/xnu-xnu-8792.81.2/` — RE-VERIFY vs
binary when kernel is back in IDA; tag is one patchlevel off 8792.82.2)

In order, errors:
1. `files_count==0` → EINVAL; `>MAX` → E2BIG
2. `shared_region==NULL` → EINVAL (ours exists — check_np=12)
3. `region->sr_root_dir != proc->fd_rdir` (chroot root) → EPERM
4. fd==-1 pseudo-entry: >1 mapping or unaligned addr/size → EINVAL
5. fd→vnode: not file/!FREAD/!VREG → EINVAL/EPERM
6. `mac_file_check_mmap` → passthrough errno (sandbox: 40 = EMSGSIZE
   deny for non-boot-cache vnodes; bypassed via `no-sandbox` entitlement)
7. `va_uid != 0` → EPERM (file must be root-owned — ours is)
8. `v_mount` must equal root-vol mount OR preboot-cryptex mount
   (`vnode_lookup("/private/preboot/Cryptexes")` — FAILS inside chroot!)
   → EPERM. *This is why cryptex-vol files get EPERM in chroot but data-vol
   files pass — the bindfs'd preboot dir is still a disk1s6 vnode.*
9. `scdir_enforce` (if on): vnode_parent must be expected scdir → EPERM
10. `ubc_getobject` NULL → EINVAL
11. **`ubc_cs_is_range_codesigned(vp, file_offset, size)` → EINVAL** ←
    our errno-22 wall. Needs a cs_blob on the vnode covering each
    non-ZF mapping range. Blob was ABSENT because our cache file (cp'd
    into rootfs) lost its APFS fs-signature.

### Errno observations (both confirmed)
```
CHROOT child (empty region):       iOS host (populated region):
  macOS-cache@datavol → EINVAL 22    macOS-cache@datavol → EPERM 1
  macOS-cache@preboot → EPERM  1     macOS-cache@preboot → EINVAL 22
  iOS-cache@preboot   → EPERM  1     iOS-cache@preboot   → EINVAL 22
  dyld@datavol(fake)  → EFAULT 14    dyld(fake)         → EINVAL 22
```
Pattern = gate-8 volume check vs later EINVAL ordering. Populated-region
submissions all EINVAL (occupied reject in map engine).

### ★ THE FIX: `fcntl(fd, F_ADDFILESIGS=61, &fs)`
Reads superblob at `fs.fs_file_start+fs.fs_blob_start` → `ubc_cs_blob_add`
→ attaches cs_blob to the vnode. For the 15.6.1 cache:
`fs_file_start=0, fs_blob_start=codeSignatureOffset (hdr+0x28=0xa160c000),
fs_blob_size=codeSignatureSize (hdr+0x30=0x50c000)`.

**KEY: must be called from an iOS-PLATFORM process (host side).**
Inside chroot it returns EPERM — `ubc_cs_blob_add` → `mac_vnode_check_
signature(vp,…,proc_platform)` → AMFI rejects (probably because caller is
PLATFORM_MACOS, or sandbox file-check of a macOS process). Host-side call
on the SAME file succeeded (probe7 on all files → 0). Blobs stick to the
VNODE → chroot children then pass gate 11.

### Tooling that now works (all verified this session)
- **dyld cave probe**: patch `B` at thin-dyld 0x35698 → 0x3576c overwrites
  `preflightMainCacheFile` (dead once probe exits). Asm via clang
  `-nostdlib -Wl,-e,__start -Wl,-static`, extract `__text`.
  GOTCHA: GNU as drops everything after `;` on a line — ONE INSTR PER LINE.
- **iOS-native probe binary**: build `arm64e` Mach-O exec, then
  `vtool -set-build-version 2 16.0 16.0 -replace` (platform=ios) —
  plain clang output gets "wrong platform" from iOS dyld; static Mach-O
  exec gets SIGKILL'd (137) even signed+trustcached — must be DYNAMIC.
- `mount_bindfs` exists: `/var/jb/usr/local/bin/mount_bindfs` (binary,
  mounts READ-ONLY per mountdevfs comment).
- `open=5 write=4 close=6 pread=153 fcntl=92 mmap=197 check_np=294
  map_and_slide_2=536` (svc #0x80, errno in x0; success→0/value).

### mmap PROT_EXEC + pagein on cache file (probe4)
mmap PROT_EXEC **succeeded** (0x10459c000), pagein returned byte 'd' —
kernel validated exec pages without kill, but that path does NOT attach a
cs_blob to the vnode (or its coverage doesn't satisfy). Still EINVAL after.
So F_ADDFILESIGS is the correct attach mechanism, NOT mmap.

### Next steps (NOT yet done)
1. Host-side helper/daemon: open cache files + F_ADDFILESIGS before launch
   (must run at each boot/vnode-recycle — blob lives on vnode only while
   vnode cached). Simplest: keep an fd open in a resident helper.
2. Retest REAL dyld submission (full files[] incl .01 + DynamicRegion
   fd=-1 pseudo-entry at 0x1fa000000 — relocated inside 4GB region).
   The probe only submitted mapping[0]; full submit may hit new gates
   (subcache count, slide, align checks per mapping, scdir check).
3. `scdir_enforce` sysctl — if on, file parent dir must be expected path
   (/System/Library/dyld in chroot may or may not satisfy it — worked in
   probe since errno was 22 not 1... or scdir off; verify).
4. Then dyld proceeds to actual lib loading — watch for next failures.
5. Investigate whether blob attach survives across `true` runs (vnode
   recycling) — if flaky, pin fd or add to postinst/launch helper.

## 2026-09-26 15:09 — KERNEL PANIC + RECOVERY (newest, read first)

**The iPad panicked during our experiments** — log
`/private/var/mobile/Library/Logs/CrashReporter/panic-full-2026-09-26-150854.000.ips`:

```
panic(cpu 5 caller 0xfffffe0025bc6938): pmap_trim_internal:
grand addr wraps around, grand=0xfffffdf12d917450,
subord=0xfffffdf1aee850e0, vstart=0xffffffffffffffff, size=0x1
@pmap.c:11173
Panicked task: pid 46438: true   (i.e. our launchdchrootexec /usr/bin/true run)
```

Interpretation: repeated malformed `shared_region_map_and_slide_2_np`
submissions (and possibly the earlier `check_np(NULL)` detach) leave the
task/shared-region pmap state inconsistent; the NEXT exec's pmap_trim on
teardown computes a wrap-around range and panics. **vstart=-1 is the
signature.** Consequences:

- AVOID `shared_region_check_np(NULL)` (detach) entirely — it both kills
  the caller (137) and probably corrupts state for later execs.
- AVOID large batches of malformed syscall-536 submissions; space
  experiments out, prefer clean-boot measurement.
- After ANY suspicious exit pattern, re-check kernel logs.

**Post-reboot device state** (panic also rolled back unflushed APFS writes):
- `/var/mnt/rootfs` content intact; probe binaries in `usr/local/bin` survive.
- **`/usr/lib/dyld` DELETED by the rollback** — backups survive:
  `dyld.orig` (2289328B fat, pristine), `dyld.func` (fat, 5-patch), etc.
  Deployed dyld must be RE-COPIED from dyld.orig + re-patched + re-signed.
- `/tmp` wiped on BOTH device and Mac: `kc_raw.bin`, probe asm, mt_dylib,
  dyld_* variants all gone. Pristine thin slice lives at
  `macPad/analysis/dyld_15.6.1_arm64e_thin` (1240752B). Kernel re-extract
  procedure documented below still valid (IMG4→bvx2→LZVN).
- Jailbreak re-applied by user (Dopamine); SSH ok.

**EINVAL=22 investigation — remaining candidate sites** (from
`sub_8459570` decompile; task->shared_region ruled out, it exists but is
EMPTY → check_np returns 12):
1. `vnode+0x70 != VREG` — unlikely (regular files).
2. `vnode+0x78` UBC info → cs_blobs chain empty → 22. **Top suspect.**
   Cache file has never been CS-validated by UBC (never mmap'd/exec'd as
   signed object on this kernel) → vnode may have NO cs_blob.
3. Per-mapping `ubc_cs_blob_get(vnode,-1,-1,file_offset)` must cover
   [file_off, file_off+size] → 22 if any non-slide mapping uncovered.
4. `fd=-1` DynamicRegion pseudo-entry malformed (count/align) → 22.
5. NEW: `/private/preboot/Cryptexes` literal in kernel — possible
   "vnode must be on cryptex volume" check. Our files sit on the data
   volume under a *fake* cryptex PATH. If so: copying cache onto the REAL
   preboot volume (or bind-mount) may be the bypass.

**Decisive next experiment (designed, not yet run)**: patch dyld's
failure-path dead code (~0x35720+) with a custom asm probe that submits
syscall 536 for (a) `/usr/lib/dyld` itself — guaranteed CS blob since it
is executing — and (b) the macOS cache file; write both errnos to a file.
Distinguishes "file has no UBC CS blob" vs "geometry/pseudo-entry" causes.
Probe asm source lost with /tmp; re-derive from this spec.

## 2026-09-26 — PARADIGM-SHIFTING FINDINGS (read before anything else)

1. **`re-using existing shared cache` observed at runtime.** With
   `DYLD_PRINT_SEGMENTS=1` (env DOES propagate through launchdchrootexec —
   it setenvs before POSIX_SPAWN_SETEXEC so the child inherits everything)
   a `true` run printed `dyld[pid]: re-using existing shared cache
   (/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld/
   dyld_shared_cache_arm64e)` + full segment dump, then RC=0.
   **CAUTION — attribution ambiguity**: launchdchrootexec is itself an iOS
   process, so iOS dyld prints the same "re-using" line for the LAUNCHER's
   own startup (Dopamine also injects forkfix/libinjector into it — visible
   in output). Everything BEFORE the `[launchdchrootexec] target=` banner is
   the launcher's iOS dyld; everything AFTER is the child (macOS dyld).
   Must capture output after the banner to attribute. Either way, the kernel
   pre-maps the iOS boot cache into every exec'd process — macOS dyld may be
   reusing IT rather than mapping ours.
2. **errno-40 source CONFIRMED by neighbor-AI kernel RE**
   (`docs/porting/kernel-syscall536-finding.md`): Sandbox
   `mpo_file_check_mmap` @ 0xfffffe000a659664 → `cred_sb_evaluate(op=16,
   file-map-executable)` → deny errno 40 for foreign cache vnodes lacking
   VSHARED_DYLD flag. AMFI hook can only return {0,1}. AppleImage4 no hook.
3. **Deployed dyld MUST carry project entitlements** (has
   `com.apple.private.security.no-sandbox` + 233 others): bare ldid-signed
   dyld gets platform sandbox at exec → file-map-executable denied → 40.
   Entitled dyld + `true` gave RC=0 ×5 (still ambiguous vs disk fallback).
4. **trustcache grep must be case-insensitive** — `jbctl trustcache info`
   prints UPPERCASE hex; `grep` without `-i` falsely reports missing.
   `jbctl trustcache add` silently no-ops sometimes — always verify.
5. **15.6.1 cache CodeDirectory cdhashes** (superblob 0xfade0cc0 @
   header+0x28 codeSignatureOffset, CD slot 0):
   main: sha256 `2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e` / sha1 `05afea7d…`;
   .01: sha256 `8c7ba7e588b0edd43f7334e2de11688cd4732192` / sha1 `f1d3342b…`.
   Both sha256 values added to device trustcache (verified).
   Cache files live at `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/
   System/Library/dyld/` (symlinked from `/System/Library/dyld/`).
6. **`_dyld_get_shared_cache_uuid` probe** — must declare manually:
   `extern const unsigned char *_dyld_get_shared_cache_uuid(void)
   __attribute__((weak_import));` compiled OK, deployed as dsctest, first
   run 137 (cdhash race), then RC=0 with ZERO output — main seemingly never
   ran (should print UUID + exit 42/43). Unexplained.
7. **Static arm64e test binary (`hw`, raw svc, no dyld) → 137** despite
   cdhash trusted. A no-dyld exec still gets killed — suggests an
   exec-time/launch-constraint gate on arm64e Mach-Os independent of dyld.
8. **137 causes catalog**: (a) cdhash not in trustcache; (b) stale inode
   (cp-overwrite keeps old CS vnode); (c) AMFI launch-constraint (see
   launchdchrootexec main.m comment — `Launch Constraint Violation` kills
   when spawn type mismatches); (d) dyld abort_with_payload = SIGKILL;
   (e) exiting while shared-region attach mid-flight (probe <0x35698).
9. **Host reboot wiped `/tmp`** — kernel `/tmp/kc_raw.bin` + IDB +
   `/tmp/xnu8792` gone. Kernel RE doc `kernel-syscall536-finding.md` has all
   addresses. If kernel needed again: re-extract → tell user → they load in
   IDA Instance2. Device kernelcache source: `/private/preboot/CFD92CED…/
   System/Library/Caches/com.apple.kernelcaches/kernelcache` (IMG4, 21.8MB,
   bvx2/LZVN payload, decompress to ~76MB arm64e Mach-O).
10. **Merge with upstream done** (`91ff1b6`), `control` conflict resolved to
    `Depends: python3, ldid`. Repo `~/Desktop/macPad`, upstream DCMMC/macPad,
    origin zenkernelsam/macPad, `main` ahead of origin by 5. Untracked
    `analysis/` dir exists.
11. **Device binaries of record**: `/var/mnt/rootfs/usr/lib/dyld` currently =
    entitled fat build (cdhashes 82d3f27a/46282ecc — trusted) but patch
    offsets 0x352bc/0x35698 show PRISTINE bytes → it's an entitled
    UNPATCHED dyld. `dyld.func.keep` = earlier functional build backup.
12. **`dsctest`/`hw`/`true` all currently 137 or silent-0** — device state
    unstable; when `true` gave RC=0 the cache-hash adds + entitled dyld were
    in place. Reproduce DYLD_PRINT run to see WHERE it dies now.

## Device access (also in AGENTS.md)

- SSH: `sshpass -p cisco ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no -p 2222 root@192.168.64.1`
  - Password: `cisco` (`alpine` does NOT work). The `-o` flags matter: without
    them ssh-agent keys get offered first → "Too many authentication
    failures" / "UNIX authentication refused" intermittently.
  - If unreachable, IP may have changed; scan for open :2222.
- Staging dir on device (upload everything here, overwritable):
  `/var/mobile/Containers/Shared/AppGroup/1B2AD29A-2C34-4770-86EC-E11CD02312FF/File Provider Storage/macPad_iOS`
- Rootfs mounted at `/var/mnt/rootfs`; installed tools at `/var/jb/usr/macOS`.
- Pristine dyld extract on device: `/tmp/usr/lib/dyld` (from rootfs tar).
- Repo paths `/var/jb/var/mobile/MacWSBootingGuide`, `/var/jb/var/mobile/theos`
  are GONE (device re-jailbroken).

## dyld binary layout — CRITICAL

- Source dyld = fat (arm64@0x4000 + arm64e@0x100000) from rootfs tar,
  2289328 bytes. Analysis file = `analysis/dyld_15.6.1_arm64e_thin`
  (1240752 B = arm64e slice).
- **`ldid -S` REPACKS the fat**: after signing, arm64e slice moves to
  `0xfc000`. NEVER hardcode the delta. ALWAYS read the fat header after
  signing:
  `d[8+i*20+8:8+i*20+12]` big-endian = slice offset; arm64e = cputype
  0x100000c subtype 0x80000002.
- Order of operations: restore pristine → patch at CURRENT delta →
  `ldid -S` → `jbctl trustcache add <sha256 cdhash>` (BOTH slices, UPPERCASE
  hex — lowercase add is accepted but verify with `jbctl trustcache info`) →
  **`rm` dest file THEN `cp`** (cp over same inode = stale CS → silent
  SIGKILL 137. New inode required).
- NOTE: `for h in $(ldid -h ...)` loops can silently produce empty vars →
  verify `jbctl trustcache info | grep <hash>` actually lists it.

## Syscall ABI (verified vs xnu-8792.81.2 + IDA)

- `shared_region_check_np` = **294** (NOT 464 — sprobe.c originally had it
  wrong; iOS master:445). `check_np(&base)` → 0+base if region mapped;
  errno otherwise. `check_np(NULL)` = **detach/unmap the task's shared
  region** (vm_shared_region_remove + set NULL).
- `shared_region_map_and_slide_2_np` = **536**
  `(u32 files_count, shared_file_np files[], u32 mappings_count,
   shared_file_mapping_slide_np mappings[])`:
  ```c
  struct shared_file_np { int sf_fd; u32 sf_mappings_count; u32 sf_slide; }; // 12B
  struct shared_file_mapping_slide_np {   // 48B
      u64 sms_address, sms_size, sms_file_offset;
      u64 sms_slide_size, sms_slide_start;
      int sms_max_prot, sms_init_prot;    // +VM_PROT_SLIDE(0x20) etc in max_prot
  };
  ```
- Kernel applies `slide_amount` (random % files[0].sf_slide) to EVERY
  sms_address → dyld submits UNSLID header addresses; slide=0 → as-is.
- Kernel-side checks (vm_unix.c shared_region_map_and_slide_setup): file on
  root/preboot volume, CS coverage, vnode owner/root-dir, alignment,
  KERN_NO_SPACE for out-of-region. Errnos: EPERM/EINVAL/EFAULT/ENOMEM.

## dyld flow (thin offsets; from full-analysis + fresh IDA reads)

`loadDyldCache` 0x34240 → `mapSplitCacheSystemWide` 0x352bc:
- 0x34268 `B.NE` guards private-vs-systemwide; 0x34298 `BL reuseExistingCache`
  = PRE-reuse (accepts iOS cache by magic alone!) — PATCH `MOV W0,#0` to force
  syscall path. Post-syscall reuse call at 0x356d8 is SEPARATE (keep it —
  it fills results->loadAddress via check_np+strcmp).
- 0x3538c `LDR W28,[X19,#0x1A8]` = numFiles (2). `MOV W28,#1` = main only.
- files[] entries = per-subcache {fd,count,slide} + trailing
  `{sf_fd=-1, count=1, slide=0}` = **DynamicRegion pseudo-mapping**:
  sms_address = header.sharedRegionStart(0xe0)+dynamicDataOffset(0x1f0)
  = 0x2ac75c000 (OUT OF 4GB iOS REGION — always KERN_NO_SPACE);
  sms_size = DynamicRegion::size(); **sms_file_offset = userspace ptr to
  DynamicRegion buffer** (kernel copies content from it); prots=0x100000001
  (R/R); slide fields 0.
  - Computed at 0x35fc8-0x35fd8 in preflightCacheFile tail:
    `LDR X9,[hdr+0x1f0]; LDR X10,[hdr+0x1f8](size?); LDR X11,[hdr+0xe0];
    ADD X9,X9,X11; STP X9,X10,[record+0x1B0]`. Read later at 0x35660.
  - `DyldSharedCache::dynamicRegion()` accessor at 0x50dfc:
    `LDR X8,[X0,#0x1F0]` then `this+X8` → must be relocated CONSISTENTLY
    with the submission patch (both to same offset). Returning NULL →
    fileId stays 0 → ctor `halt`. Don't NULL it.
- Error path: 0x356dc `CBZ W23,0x356f4` (syscall ok) / `TBNZ W0,#0→0x35710`
  (reuse ok) / else `LDR X8,[X20,#0x10]` errorMessage — **if non-NULL the
  native error path at 0x35754 is SKIPPED** (returns 0 at 0x356ec). My
  earlier tramp at 0x35754 only fires when errorMessage==NULL.
- Success print "mapped dyld cache file system wide" gate: `0x35700 B.NE`
  (options+6 != 1). "re-using existing shared cache (%s)" gate: `0x35270
  B.NE`. "mapped cache does not contain dynamic cache info" (0x35298) is
  UNGATED — fires when dynamicRegion()==NULL but still returns 1.
- `reuseExistingCache` 0x351a8: `check_np(&p)` → `strcmp(p,"dyld_v1  arm64e")`
  → slide → dynamicRegion() → getDyldCacheFileID → ret 1. Magic-only check =
  why iOS cryptex cache gets reused.
- errno global = `0xa9b10` (`_errno`, written by `cerror_nocancel` @0x2d64:
  `STR W0,[errno]; MRS TPIDRRO_EL0; STR W0,[[tls]+8]`). Read it via
  `adrp`+`ldr` (PC-relative, ASLR-safe).
- `console()` = dyld4::console @ **0xa2f4** — printf-family, callable from a
  tramp via adrp+add+blr (LR clobber OK if you save/restore it; re-do
  `pacibsp` in tramp if you overwrite one).
- NOP cave: thin `0x38d08`-`0x38d3c` (56B; 0x38d40 starts a real function).
  NOTE: `dyld_15.6.1_arm64e_thin.i64` IDB is **contaminated** — it shows my
  old `B 0x38d08` at 0x35754 as if native (clean file has `MOV W0,#0`).
  Trust the device pristine copy for raw bytes there.

## Patch recipe under test (thin offsets; verify bytes post-signing)

| thin | patch | purpose |
|---|---|---|
| 0x76270 | `e0031faa` (mov x0,xzr) | crossarch_trap svc→0 (iOS nosys) — REQUIRED |
| 0x34298 | `00008052` (mov w0,#0) | skip PRE-reuse → force syscall path |
| 0x3538c | `3c008052` (mov w28,#1) | files_count=1 → drop .01 subcache |
| 0x35fc8 | `0940afd2` (movz x9,#0x7a00,lsl#16) | dynregion submit addr → 0x1fa000000 |
| 0x50dfc | `0840afd2` (movz x8,#0x7a00,lsl#16) | dynamicRegion() offset → +0x7a000000 |
| 0x35700 | `1f2003d5` (nop) | diagnostic: ungate "mapped...system wide" |
| 0x35270 | `1f2003d5` (nop) | diagnostic: ungate "re-using (%s)" |

## 2025-XX session 2 — errno CONFIRMED + dead ends ruled out

**Syscall #536 errno = 40 = EMSGSIZE** (or kern_return_t 40=KERN_LOCK_OWNED —
the BSD wrapper returns `kr` raw, both readings possible). Source: iOS-16.3
CLOSED-source branch of `shared_region_map_and_slide_2_np` — not present in
xnu-8792 open source (which only produces EPERM/EINVAL/EFAULT/ENOMEM/E2BIG).
Verified via exit-probe: 6/6 stable rc=40 from `0x35698` (`CMP W0,#-1 → B.NE;
ADRP X8,#0xa9; LDR W0,[X8,#0xb10]=errno; exit(W0)`). The stub does run
`cerror_nocancel` → errno := kernel ret → W0=-1.

**Exit-probe methodology — hard rule discovered:** `exit(N)` placed BEFORE the
syscall returns (0x352bc entry / 0x3533c / 0x35680) reliably yields **137**
(SIGKILL), while exits AFTER the syscall (0x35698) work fine. i.e. the kernel
kills a task that exits while its shared-region attach is mid-flight —
early-exit probes are useless; only probe ≥ 0x35698 (or post-syscall sites).

**Intermittent 137s are environmental** (iOS-cache prebind timing / amfid),
not patch content — same file alternates 137/40 across runs, then stabilizes.
Retest before attributing.

**`deallocateExistingSharedCache` (check_np(0)) is a DEAD END:** calling it
from `mapSplitCacheSystemWide` (0x3533c tramp → cave → BL 0x3420c) kills the
task (137, consistent). Reason: `vm_shared_region_remove` rips the nested-pmap
region out of the task mid-exec. Also pointless: after detach,
`map_and_slide` gets `no shared region → EINVAL` — the region is created
once at exec by `vm_shared_region_enter`, cannot be recreated.

**Region geometry (xnu-8792):** `SHARED_REGION_BASE_ARM64=0x180000000`,
`SIZE=0x100000000` (fixed 4GB, not per-cache). macOS 15.6.1 cache: main
0x180000000–0x2255dc000 (fully in bounds), `.01` 0x22560c000–0x27dfd8000+
(TAIL CROSSES 0x280000000 — partial problem), dynamic region 0x2ac75c000
(fully out). With files_count=1+dynrelocate all submissions are in-bounds
yet EMSGSIZE persists → the rejection is NOT bounds; it's an iOS-specific
check (probably cache-identity/UUID-vs-boot-cache or file-set composition).

**iOS kernelcache extracted for RE:** device
`/private/preboot/CFD92CED…/System/Library/Caches/com.apple.kernelcaches/
kernelcache` (IMG4, 21.8MB) → decompressed via `pyimg4` (bvx2/LZVN payload)
→ `/tmp/kc_raw.bin` = 76MB arm64e Mach-O kernelcache (T8112 — device is M2,
iPad13,11). **Load this in IDA to find the EMSGSIZE return site** in
`shared_region_map_and_slide_2_np` (sysent[536]).

**Full 4.9GB 15.6.1 dyld cache staged on host:**
`/Users/ciscohe/Desktop/macPad/analysis/dyld-cache-15.6.1/` — main `dyld_shared_cache_arm64e`
(2712764416 B) + `.01` (2203500544 B), verified complete vs device sizes.
Gitignored via `analysis/` (deliberately untracked + outside /tmp). Use
`dsc_extractor` or `misc/extract_dyld_cache.py` against this pair if library
bodies needed.

## Results so far (this session)

- 5-patch config (first five above, post-reuse intact): child prints
  `Mapping the shared cache system wide` → `dyld cache '(null)' not loaded:
  syscall to map cache into shared region failed` → dyld FALLS BACK to
  on-disk mmap (`Kernel mapped /usr/bin/true`, `Mapping
  /usr/local/lib/libmachook.dylib`, `Mapping /usr/lib/libSystem.B.dylib`) →
  `libdyld.dylib not found` → rc=0. **syscall still fails** even with
  files_count=1 + dynregion relocated.
- errno still unconfirmed — tramp-on-error-path approach was flaky because
  (a) errorMessage may already be set (skip path), (b) bss-write tramp
  crashed 139, (c) stub-level redirect + console gave 137 (was actually
  trustcache/inode staleness, not the tramp).
- Next step: **freestanding probe `misc/sprobe.c`** (already written &
  compiled — static arm64e Mach-O, raw SVC, no dyld needed). Staged tests:
  A check_np state; B main-cache-only map; C +relocated dynregion;
  D check_np(0) dealloc then retry; E dynregion at original out-of-bounds
  addr. Prints errno for each → pins down WHICH check fails.
  - sprobe fixes applied: check_np syscall 464→294; staged tests added.
  - Deploy: `ldid -S`, `jbctl trustcache add`, rm+cp into
    `/var/mnt/rootfs/usr/bin/sprobe`, run via launchdchrootexec.
  - First run gave 137 — most likely trustcache add raced/silent-fail OR
    stale inode. RETRY with verified `jbctl trustcache info | grep`.

## Exit-code legend

- 0   = ran to completion (may still be DEGRADED — disk fallback when no
        cache; distinguish via prints, not rc)
- 132 = SIGILL (real illegal instr / PAC failure — NOT brk)
- 133 = SIGTRAP = `brk #0` fired (bisect marker)
- 134 = SIGABRT (dyld graceful abort/halt)
- 137 = SIGKILL: CS/trustcache/exec-policy — silent, NO .ips. If it appears
        right after a rebuild: check trustcache add actually landed AND the
        dest inode was replaced (rm then cp, not cp-overwrite).
- 138 = SIGBUS
- 139 = SIGSEGV
- 140 = SIGSYS
- **WARNING: piping the launcher into `awk`/`tail` eats `$?`** — rc then
  reports the filter's exit. Always `echo $?` on the raw command.

## Confirmed root causes (unchanged)

1. iOS 16.3 `SHARED_REGION_SIZE_ARM64=0x100000000` = region
   0x180000000-0x280000000; macOS 15.6.1 cache extent to 0x2ac760000.
   .01 subcache tail + dynamicData region exceed it → KERN_NO_SPACE.
2. DSC file CS-enforced — editing header (subcache count) kills it for
   that inode permanently. Can't patch DSC.
3. Private path `mapSplitCachePrivate` 0x342dc = plain mmap of exec DSC
   pages → CS kill 137. Dead end.
4. iOS-cache prebind + magic-only reuse → false-success variance.

## Original-project tools — what they're for (see tools-and-porting doc)

- `misc/sprobe.c` — this task's probe (freestanding, raw SVC).
- `launchdchrootexec` — chroot launcher (sets MACWS_CHROOT_HOST_ROOT,
  DYLD_INSERT_LIBRARIES for libmachook, MACWS_SUSPEND_* for lldb).
- `misc/chroot_then_exec.c`, `misc/chroot_isolation_test.c` — chroot probes.
- `misc/loadtc`, `misc/vtool_and_sign.sh`, `autosignd/` — sign+trustcache.
- `misc/extract_dyld_cache.py` — DSC header/images parser.
- `misc/disasm_remote_dyld_range.sh` — device-side dyld byte dumps.
- `misc/lldb_*` — scripted lldb attach/breakpoints (incl. `MACWS_SUSPEND_AT_EXEC`).

## IDA MCP

- Server `ida-pro-mcp-Instance1`. IDB `dyld_15.6.1_arm64e_thin.i64` —
  CONTAMINATED at 0x35754/0x38d08 by old tramp; pristine bytes live at
  device `/tmp/usr/lib/dyld` (+0x100000 delta).
- `py_eval` arg = `code`. Use `idc.GetDisasm`/`ida_funcs`/`idautils.Strings`.
- Full analysis (mostly valid modulo contamination):
  `docs/porting/dyld-15.6.1-full-analysis.md`.

## Build/test commands

```bash
# verify arm64e slice delta on device file:
python3 - <<EOF
import struct;d=open('/var/mnt/rootfs/usr/lib/dyld','rb').read(64)
for i in range(struct.unpack('>I',d[4:8])[0]):
 o=8+i*20;ct,cs,off,sz,al=struct.unpack('>IIIII',d[o:o+20])
 if cs==0x80000002: print(hex(off))
EOF
# sign + trustcache + deploy (FRESH INODE):
ldid -S /tmp/dyld_x
for h in $(ldid -h /tmp/dyld_x|grep '^CDHash='|cut -d= -f2); do jbctl trustcache add $h; done
jbctl trustcache info | grep -i <hash>   # VERIFY it landed
rm /var/mnt/rootfs/usr/lib/dyld && cp /tmp/dyld_x /var/mnt/rootfs/usr/lib/dyld
# run:
/var/jb/usr/macOS/bin/launchdchrootexec 0 0 /var/mnt/rootfs /usr/bin/true
```

## ✅✅ 2026-09-28 01:5x【重大】两种缓存均成功映射（notloaded=0）；此前失败=探针自伤
### ⚠️ 致命踩坑（必须先读）
`shared_region_check_np(294)` 的**入参为 0** 时内核会执行 `vm_shared_region_remove(task, sr)`（"unmap"语义，见
`bsd/vm/vm_unix.c: shared_region_check_np()`：`if (uap->start_address == 0) { vm_shared_region_remove(...) }`）。
⇒ **任何把 start_address 置 0（或传未初始化栈垃圾=0）的探针都会把空 region 删掉**，之后 536 恒 EINVAL(22)。
本轮此前所有 "check_np=22 / 536 失败" **全部是探针自伤**，不是真实阻塞。
**正确探针**：slot 必须先写**非零**值（如 0x1000）再调用；此时返回 **12 = 有 region 且未映射**（实测）。

### 结论：region 一直存在，536 本来就能通
同一 boot 实测（`cachereg <主缓存> <.01>` 后台运行 + 对应 dyld）：
| 缓存 | dyld | env | 结果 |
|---|---|---|---|
| iOS `/iosdsc` | `dyld_sf0.bin`(清 sf_slide) | `DYLD_SHARED_CACHE_DIR=/iosdsc` | **rc=0 notloaded=0 HELLO** ×2 |
| macOS cryptex | `dyld_plat.bin`(crossarch+plataccept, 保留 maxSlide=0x20000000) | `DYLD_SHARED_CACHE_DIR=<cryptex dyld 目录>` | **rc=0 notloaded=0 HELLO** ×2 |

`DYLD_PRINT_LIBRARIES=1` 证据：`<D161E41A-3030-339F-B135-E244271F54C6> /usr/lib/libSystem.B.dylib`
⇒ 库确实来自 **macOS 缓存**（uuid 与缓存头 0x58 一致）。

### 剩余堵点：去掉磁盘 shim 后 libSystem 被判 “wrong platform to load into process”
移走 `/usr/lib/libSystem.B.dylib` + `/usr/lib/system/libdyld.dylib` 后（缓存已成功映射！）：
```
dyld: <D161E41A…> /usr/lib/libSystem.B.dylib
dyld: Library not loaded: /usr/lib/libSystem.B.dylib
  Reason: tried: … (no such file) … '/usr/lib/libSystem.B.dylib' (wrong platform to load into process)
```
⇒ 缓存已载入，但**镜像级 platform 校验**仍拒绝（`plataccept@0x35c24` 只覆盖 preflight 那一处）。
**下一步**：定位 dyld 镜像级的 platform 检查（对照 “wrong platform to load into process” 字符串 xref），
把进程中 macOS 镜像的 platform 接受逻辑一并放开（不要用 `platstub` 那种强制 loadableIntoProcess 的暴力 stub）。
`/System/Library/dyld/` 的 symlink（主/.01/atlas/map → cryptex）齐全，**默认路径失败另有原因**（env 路径成功），待查。

### item#5 进展（cat/ls/sh）与已排错的猜测
- **排除**：`/bin/{echo,cat,ls,sh}` 与 `libSystem.B/libdyld` 的**两切片 platform 都是 macOS(1)**（工具 `analysis/dyldwork/sliceplat.py`）⇒ "wrong platform" 不是主程序被 arm64ify 打成 iOS 造成。
- **镜像级门定位**：`dyld4::JustInTimeLoader::makeJustInTimeLoaderDyldCache` @**0x2b4d8** 里
  `if (mach_o::Header::loadableIntoProcess@0x7e970(...)&1) {...} else Diagnostics::error("wrong platform to load into process")`。
- **不能 stub 0x7e970**：实测把它改成 `mov w0,#1;ret` 后**连缓存都映射失败**（该函数在缓存自身校验里也被调用）
  ⇒ 必须改**判定逻辑**（如只放开 `Platform::macOS` 的接受分支），不可 lazy stub（违反项目纪律）。
- 现象序列（移除 shim + env 且 cache 映射成功）：先打印 `<D161E41A> /usr/lib/libSystem.B.dylib`（缓存镜像是被加载过的），
  随后 "Library not loaded … Reason: tried: … (no such file) … (wrong platform)" ⇒ 走了**按路径**的加载分支
  而不是缓存索引分支，且末选被 platform 判据拒。**下一步**：查 dyld 里 libSystem 的“按路径→缓存索引”回退为何没命中
  （可能是 chroot 下 root 路径拼接/`dyldCache` 的 path 表匹配问题），而不是继续动 platform 判据。

### 2026-09-28 02:1x 本轮补证与"最后一公里"脚本
**1. 项目自带 sprobe 全文（`/var/mnt/rootfs/tmp/sprobe`，可跑，rc=0）**：
```
check_np ret  = -14 | cache path = <cryptex>/…/dyld_shared_cache_arm64e
mappingCount = 8 | platform = 1(macOS) | csSigOffset=0xa160c000 csSigSize=0x50c000(→到文件尾)
map…[8 条，addr 0x180000000/0x1ebdec000/0x1ee1ac000/0x1f9070000…]
map_and_slide = -22
```
⇒ **即使用项目自己的探针 + 完全合法的参数，macOS 缓存的 536 仍得 -22** ⇒ 内核侧对 macOS 缓存有独立判据，且**与 region 当前状态强相关**（干净 boot 首进程可成功，已被 91×`Using mapping in dyld cache` 证明）。
**2. shim 干扰（item#5 主因）**：shim 在场时 dyld 按路径优先加载 `<B90391D8>`（磁盘 shim）而不是缓存 `<D161E41A>` ⇒ `Symbol not found: ___error`；shim 移走后缓存映射**失败**时 dyld 明确说 `'/usr/lib/libSystem.B.dylib' (no such file, **no dyld cache**)` ⇒ **只要缓存映射成功，libSystem 就从缓存来**（已由 notloaded=0 那轮 91 个 `Using mapping in dyld cache` 证实）。
**3. 自建 `region_reset`（清 region 用）被 exec veto（rc=137）**，未启用；脚本内标注"禁止跑任何会调 check_np(0) 的探针"。
**4. 新增 `analysis/dyldwork/post_reboot_cli3.sh`（开机首跑，零探针）**：移走 shim → cachereg → plain dyld → echo/cat/ls/sh ×2 + `DYLD_PRINT_LIBRARIES` 取证 → 收尾还原。本地/设备 `bash -n` OK、md5 `ef91ac1d…` 一致；`wait_cli2.sh` 已改指向 cli3。
**结论**：#1~#4 已达成；#5 只差「**干净 boot 后第一个进程映射 macOS 缓存**」这一步（脚本自动完成）。

### 2026-09-28 02:3x【可复现配方 + item#5 的两个真实障碍】
**① 可复现成功配方（同一 boot 内重复成功，≥3 次）**
```
killall cachereg; ( nohup cachereg <cache> <cache>.01 & ); sleep 4      # 天然覆盖
deploy <对应 dyld>                                                     # iOS: dyld_sf0 / macOS: dyld_plat
env -i PATH=/usr/bin:/bin DYLD_SHARED_CACHE_DIR=<cache 目录> chroot /var/mnt/rootfs /bin/echo HELLO
  → iOS  : rc=0 notloaded=0 HELLO   ×多次
  → macOS: rc=0 notloaded=0 HELLO   ×多次
```
**② 结论修正**：region 并**不**需要重启复位 —— dyld 在"缓存不匹配"时会自己 `check_np(NULL)` 重置再映射，
所以同 boot 里先 iOS 后 macOS 都能成功（本回合实测：iOS ✓ 紧接 macOS ✓✓）。
**③ item#5 的两个真实障碍**
1. **shim 抢先**：shim（`<B90391D8>`）在场时，`/bin/cat` 的 libSystem 绑定到 **shim** → `Symbol not found: ___error`
   （此时 `notloaded=0`，即缓存其实已映射成功；只是绑定被 shim 抢走）。
2. **移走 shim 后 `notloaded=1`**：map 变成失败（可复现）；且**同一批里 `cat` 与 `ls` 结果不一致**（cat notloaded=0 / ls notloaded=1）
   ⇒ 存在**间歇性**（同一 boot、同一配置）。
   ⇒ 下一步应做：把**缓存里的 libSystem 抽成磁盘文件**当作 shim（`misc/extract_dyld_cache.py`），
   既满足按路径加载、又不缺符号；而不是简单移走 shim。

### 2026-09-28 02:5x【item#5 深挖：真正的 CLI 阻塞点】
**① `run_bash.sh` 自己就起不来**（与缓存/region 无关）：
```
/var/jb/usr/macOS/bin/run_bash.sh -c "..."   →
dyld: Library not loaded: /usr/lib/libncurses.5.4.dylib
  Referenced from: /bin/bash
  Reason: tried: '/usr/lib/libncurses.5.4.dylib' (no such file) … (wrong platform to load into process)
```
⇒ **磁盘上缺系统 dylib**（bash 依赖的 libncurses 等）；只有缓存映射成功时才能从缓存补上。
⇒ 这解释了"库全走缓存"为何是 CLI 的前置条件：**rootfs 只装了部分 dylib，其余全靠 dyld 缓存**。
**② `wrong platform to load into process` 不只对 shim/cache 出现过**：连**磁盘缺失文件的兜底候选**也会带上这条 ⇒
它是 dyld **镜像级**接受判据（`JustInTimeLoader::makeJustInTimeLoaderDyldCache@0x2b4d8` → `mach_o::Header::loadableIntoProcess@0x7e970`）。
**③ 抽取器路线（把缓存里的 dylib 落盘）**：
- 项目自带 `misc/extract_dyld_cache.py`（`ctypes.CDLL("/usr/lib/dsc_extractor.bundle")` 调 `dyld_shared_cache_extract_dylibs_progress`），
  bundle 在 rootfs 里是 **file 形态（272496B, Mach-O，脚本设计如此，用 isfile 校验）** ✓。
- 但执行需要 python3/bash，而二者又被 ① 卡住（bash 缺 libncurses；直接 `chroot … /usr/bin/python3` 跑抽取得 **rc=137**）
  ⇒ **鸡生蛋**：抽取器修不了缺 dylib，缺 dylib 又让抽取器跑不起来。
**④ 结论/下一步（给接棒者）**：
- 最低成本路线：**让 dyld 在缓存命中时优先用缓存 libSystem**（而不是被同名磁盘 shim 抢走）→ 缓存映射成功时 cat/ls/sh 应即通；
  或**给 shim 补缺失符号**（至少 `___error`：`mrs x0,TPIDRRO_EL0; ret`）。
- 次路线：用**其它宿主机**（macOS/另一台越狱机）跑 `dsc_extractor` 把整套 dylib 落盘，再 `sign_installed.sh` TC。

### 2026-09-28 03:0x【勘误 + 本轮实测边界】
**勘误（重要，防止误读）**：本轮曾出现"移走 shim 后 20/20 次 `notloaded=0`"，那是**测试假象**：
我的循环把 `"/bin/echo HELLO"` 整串当程序名传给 `chroot`（未拆分参数）⇒ `chroot` 报 "No such file"（rc=127）
**根本没 exec**、dyld 未运行，所以 grep 不到 "not loaded" 字样。
**修正后的真实结果**（参数正确拆分）：
```
shim 移走 + macOS 缓存 + env：e1/e2/cat/ls/sh/sh2 全部 rc=134 notloaded=1  （Library not loaded: /usr/lib/libSystem.B.dylib）
shim 在场 + macOS 缓存 + env：cat 曾出现 notloaded=0（缓存映射成功）但绑到 shim → Symbol not found: ___error
```
⇒ **现状总结（诚实版）**：
| 项 | 状态 |
|---|---|
| #1 plataccept | ✅ |
| #2 HELLO（无 env） | ✅ |
| #3 带 DYLD_SHARED_CACHE_DIR 实测 | ✅ 缓存可映射成功（可复现，见上方"配方"节；91×`Using mapping in dyld cache`） |
| #4 库全走缓存 + libSystem 来自缓存 | ✅（`<D161E41A>`，uuid 与缓存头一致） |
| #5 cat/ls/sh rc=0 | ❌ 未达：shim 在场→被 shim 抢绑定缺 `___error`；shim 移走→536 间歇性失败（同 boot 同配置两种结果都出现过） |
**下一步优先级（给接棒者）**：
1. 用**宿主机**（本 Mac，arm64 且自带 dsc_extractor）从 macOS 缓存抽取 `libSystem.B.dylib`/`libncurses.5.4.dylib` 等落盘 + `sign_installed.sh` TC ⇒ 直接解 CLI，且不影响"缓存映射"成果；
2. 或继续 RE dyld **镜像级**接受判据（`0x2b4d8/0x7e970`，注意**不可 stub**，需改判定逻辑）；
3. 或定位 536 间歇性（怀疑与 region 残留状态/被杀的进行中映射有关，需内核侧取证）。

### 2026-09-28 03:2x【负面结论】从缓存抽 dylib 落盘「不可行」+ 可复用技法
**① 宿主机抽取技法（可复用）**
本机 macOS 15.6.1 **24G90**，其缓存的 uuid 与设备 rootfs 缓存**逐位一致**（主 `4c1223e5cace3982a0036110a7a8a25c`、
`.01` `2b390646b4b5302b841aefaa5283640d`）⇒ 从本机抽取 == 设备抽取。
`/usr/lib/dsc_extractor.bundle`（272496B，与设备上同一个文件）：
- **第三参传 NULL 会 segfault**；必须构造**真 block**（`isa=_NSConcreteGlobalBlock`, `invoke=CFUNCTYPE` 指针）✓
  → `analysis/dyldwork/extract_host.py`（会崩）/ **`extract_host2.py`（可用，rc=0，抽出 3257 个文件）**。
**② 抽出的 arm64e 镜像 dyld 拒收**
把抽出的 `usr/lib/*.dylib`（387 个，含真 `libSystem.B.dylib` 13504B、`libncurses.5.4.dylib` 299968B）装进 rootfs 后：
```
dyld[7124]: Library not loaded: /usr/lib/libncurses.5.4.dylib
  Reason: tried: '/usr/lib/libncurses.5.4.dylib' (segment '__AUTH' vm address out of order) …
```
⇒ dsc_extractor 输出的 arm64e 镜像**段顺序不是独立可加载的**（`__AUTH` 段 vmaddr 非递增）⇒ **"抽 dylib 落盘"这条捷径不成立**。
（已回滚：387 个文件全部删除、两个 shim 由 `/var/mobile/BAK_*.dylib` 还原，设备自检 `RESTORED_OK2` ✓）
**③ 因此 item#5 的正解只剩**：
- (a) 让 **dyld 镜像级判据**接受缓存里的镜像（`0x2b4d8`→`0x7e970`，不可 stub），或
- (b) 修 **536 的间歇性**（同 boot 同配置两种结果都出现过；疑 region 残留状态），或
- (c) 把 shim 补成"全符号"（体积/工作量都大，且必须提供真实实现 ⇒ 又回到缓存）。

### 2026-09-28 03:5x【新线索】dyld 自带 `DYLD_FORCE_PLATFORM`（零补丁强制平台）
`strings dyld_15.6.1_arm64e_thin` 相关 env：`DYLD_SHARED_REGION` / `DYLD_SHARED_CACHE_DIR` / **`DYLD_FORCE_PLATFORM`** /
`DYLD_PRINT_LOADERS` / `DYLD_PRINT_SEARCHING` / `DYLD_PRINT_ENV` / `DYLD_USE_CLOSURES` / `DYLD_AMFI_FAKE`（后两者值得后续试）。
**实测**（macOS 缓存 + env + `DYLD_FORCE_PLATFORM=macOS`，`DYLD_PRINT_LIBRARIES=1`）：
```
dyld: <D161E41A-…> /usr/lib/libSystem.B.dylib     ← 缓存版被加载（强制平台后）
dyld: <B90391D8-…> /usr/lib/libSystem.B.dylib     ← 同一个进程里 shim 也出现了
```
⇒ `DYLD_FORCE_PLATFORM` **确实改变了镜像接受结果**（缓存版 libSystem 被纳入）⇒ **这是 option(a) 的零补丁入口**，
下一步应：用 `DYLD_PRINT_LOADERS=1`/`DYLD_PRINT_SEARCHING=1` 看清"为何 shim 仍被加载/绑定"，并试 `DYLD_FORCE_PLATFORM=1`
（数值形式）、`DYLD_AMFI_FAKE=1`、`DYLD_USE_CLOSURES=0` 等组合；期间**必须用正确拆分参数的调用**（本轮我又有一次 `set --` 引号 bug 导致 rc=127 假象）。

## ✅ 2026-09-28 07:5x【全新 boot 上的判决性结果】"iOS 先 → macOS 后"配方成立
**两轮一致性脚本**（`post_reboot_cli4.sh`，boot 后第一个 chroot 命令即被测）：
```
第一轮/第二轮完全一致：
  ctl-echo rc=0 | ctl-cat rc=134 | fp-echo1 rc=0 | fp-echo2 rc=0 | fp-cat/ls/sh rc=134
  每次均 "dyld cache '(null)' not loaded"（536 失败），cacheimg=0
```
⇒ **全新 boot 上"直接上 macOS 缓存"必失败**（与旧 boot 里第一次成功的情形不同）。

**但紧接着按序执行即成功（同 boot、两次独立复现）**：
```
1) cachereg(iOS) + dyld_sf0 → echo  : rc=0 nl=0   ← iOS 缓存映射成功
2) cachereg(macOS) + dyld_plat → echo: rc=0 nl=0   ← 紧接着 macOS 缓存也映射成功 ✓✓
```
⇒ **配方（消除歧义版）**：
1. 每次**只**跑一条 `cachereg <cache> <cache>.01`（天然覆盖，别扩覆盖），`sleep 4-5`；
2. 先跑 **iOS 缓存**（`DYLD_SHARED_CACHE_DIR=/iosdsc`, `dyld_sf0`）让 region 先落地一次；
3. 再切 **macOS 缓存**（`DYLD_SHARED_CACHE_DIR=<cryptex dyld 目录>`, `dyld_plat`）→ **即可 `nl=0` 成功**；
4. 期间**不要**再去 mv/rm 文件（触碰 FS 会让 CS blob 失效，见下）。
**#5 的两个"真障碍"（本轮反复确认）**：
- **shim 在场**：缓存映射成功（nl=0）但 `/bin/cat` 的 libSystem **绑定到 shim**（`<B90391D8>`）→ `Symbol not found: ___error`；
- **shim 移走**：536 **立刻变失败**（nl=1，≥4 次复现；移走后**重挂 cachereg 也无效**）⇒ 这一"shim 在场↔映射成功"的因果**用户态无法解释**。
- 另外：`mv/rm` 触碰 FS 后 CS blob 可能失效（本轮已测：移走后重挂 cachereg 仍失败）。
**结论**：#1~#4 在全新 boot 上**已判决性达成并可复现**；#5 只剩"**让缓存 libSystem 压过 shim 绑定**"这一件事
（方向：`DYLD_FORCE_PLATFORM=macOS` 已能使缓存版 libSystem 被加载；下一步用 `DYLD_PRINT_LOADERS/SEARCHING` 定位 shim 为何仍胜出）。

## 🔑 2026-09-28 08:2x【#5 打开缺口】磁盘覆盖缓存的判定链 + 两条铁律
### 铁律 1（操作纪律，已多次复现）
**先做完所有 FS 写（rm/cp/mv），再启动 cachereg，之后绝不再动文件** —— 否则 cachereg 挂上的 CS blob 失效、536 立刻变 EINVAL。
（这解释了此前"移走 shim 后映射必失败"的一半原因：`mv` 本身就让 blob 失效。）
### 铁律 2（关键、7+ 次复现）
**`/usr/lib/libSystem.B.dylib` + `libdyld.dylib` 必须"存在"**：删掉/移走后**连 iOS 缓存都映射不上**（`seed-ios nl=1`）；
放回后同一序列 `ios-seed nl=0` + `mac-seed nl=0` 立刻恢复。机制未明（疑与 dyld 在缺 libSystem 时的早期退出/CS 状态有关）。
### dyld 的"磁盘覆盖缓存"判定链（IDA `Loader::getLoader` block_invoke @**0x1f788**）
```
0x1fe08: fileExists("/usr/lib/libSystem.B.dylib") == true    ← shim 在盘上
0x1fe24: LDRB W23,[X20,#0x52]                                ← v49 = 允许覆盖
0x1fe28: B loc_1FFE0   →  LABEL_105/118 → makeDiskLoader(override=1)
         ⇒ 日志 "found: dylib-from-disk-to-override-cache" + <B90391D8>(shim) ⇒ Symbol not found: ___error
另一条分支（我们的进程【不】走）：
0x1fd84: BL isProtectedLibSystemPath → 0x1fd88 TBZ W0,#0,loc_1FFDC
0x1fd8c: MOV W8,#0x4E(78) → LABEL_72 → makeDyldCacheLoader ⇒ 用【缓存】✓
```
**实测的两种补丁**：
| 补丁 | 结果 |
|---|---|
| `dyld_fix.bin`：0x1fd88 TBZ→NOP | 无效（我们的路径根本不经过此处） |
| `dyld_fix2.bin`：再加 0x1fe28 `B loc_1FFE0`→`B loc_1FD98` | **目录已改对**（报错从 shim 变成 `'/usr/lib/libSystem.B.dylib' (wrong platform to load into process)` ⇒ 已改试缓存）**但破坏了 macOS 缓存映射**（`mac-seed nl=1`）⇒ 疑因跳到 `loc_1FD98` 时 X20/X8 上下文不对、`LDR W4,[X8,#0x18]` 取到垃圾索引 |
**下一步（给接棒者，已很接近）**：在 `loc_1FE08` 这条路径上把 flow 引到 **makeDyldCacheLoader**，但**必须先把 cache index 装进 W4**
（即复用 `indexOfPath` 的返回值，而非 `[X8,#0x18]`），或改为让 `isProtectedLibSystemPath` 在该路径上也被调用。
**当前设备状态**：shim 已复原、活基线 `dyld_probe_noC.bin` 已还原、`DEV_OK9` ✓。

## 🧱 2026-09-28 09:0x【#5 收口】三条路全部堵死 + 精确交棒
### 本轮新增实测
1. **`dyld_compat.bin`（`Policy::enforceSegmentOrderMatchesLoadCmds@0x80ae0` → return false）**：
   放开段序校验后 **映射不受影响**（`ios nl=0` + `mac nl=0` ✓），**但这救不了抽取路线**：
   抽出的真 `libSystem.B.dylib` 装上后报 `mmap(addr=0x2D9ED32D8, size=0x10) failed` ⇒
   **缓存镜像的段必须落在缓存 VA 上，无法作为独立磁盘 dylib 使用** ⇒ **"抽取落盘"彻底终结**。
2. **覆盖决策点不可改**：`fix2/fix4`（0x1fe28→loc_1FD98）与 `fix5`（0x200d8 TBZ→B loc_1FD98，配合 fix3 的 cave 填 index）
   **三次都使 macOS 缓存映射失败**（`mac-seed nl=1`）⇒ 该 block_invoke 在**缓存自身映射**期间就被使用，重定向会破坏映射。
3. **改 shim 的 `LC_ID_DYLIB`（libSystem→libXYSTEM）无效**：dyld 的磁盘覆盖是**按路径**匹配（日志 `dylib-from-disk-to-override-cache`），与安装名无关。
4. **铁律复核**：shim 必须存在（移走 → 连 iOS 缓存都映射不上）；shim 必须**签名有效**（签名坏了同样 `nl=1`）；FS 写必须在 cachereg 之前。
### 结论（#5 的精确卡点）
`/usr/lib/libSystem.B.dylib` 这个**同名磁盘文件**被 dyld 按设计用作 **override-cache**；而它又**必须存在且有效**（否则 536 失败）；
改判定会破坏映射；抽出真身又无法独立加载 ⇒ **必须让该文件本身变成"可用的真 libSystem"或让 dyld 对它走 `isProtectedLibSystemPath` 分支**。
### 交棒方向（按可行性排序）
1. **弄清 `ProcessConfig::DyldCache::isProtectedLibSystemPath@0xcb88` 为何对本路径返回 0**（该分支 → errno 78 → 用缓存，正是我们要的）；
   若能让它在此处返回 1（且不破坏映射），#5 即通。
2. **给 shim 补齐符号**（至少 `___error`；`mrs x0,TPIDRRO_EL0; ret`）——工作量取决于缺多少（可先用 `dyld_info` 差集算出）。
3. 内核侧：查"为何 shim 存在 536 才成功"（疑 dyld 在缺 libSystem 时的早期退出与 CS 状态）。
**设备现状**：shim 复原(165744/217088)、活基线 `dyld_probe_noC.bin` 就位、`DEV_OK10` ✓。

## 🎉🎉 2026-09-28 09:2x【#5 找到并验证了正解】手写 shim 是可扩展的！
**关键发现**：`/usr/lib/libSystem.B.dylib` 的 shim **有源码**：`tmp/shim/libSystem_shim.c`（84 行，裸 `svc` 实现）
+ `tmp/shim/build_shim.sh`（clang 双架构 + `install_name_tool -id /usr/lib/libSystem.B.dylib`）
+ `tmp/shim/deploy_shim.sh`。原作者只为 **`echo` 的 10 个导入** 写了它，所以 cat/ls/sh 缺符号。
**实测验证（决定性）**：
```
给 shim 加 ___error（C 名必须写 __error！否则导出成 ____error）→ 重建部署：
  cat: "Symbol not found: ___error"  →  "Symbol not found: ___maskrune"   ✅ 前进一格
  sh : "Symbol not found: ___error"  →  "Symbol not found: ___stack_chk_fail" ✅
```
**⇒ 正解 = 按需扩展该 shim**（每加一个符号错误就前进一格，已实测）。
**待补符号清单**（本机用 `dyld_info -imports <arm64 切片>` 导出，见 `tmp/imports/*_arm64_syms.txt`）：
- `cat` / `sh` 的完整导入列表已导出；实现手法沿用 shim 现有风格：
  - 纯数据：`___stack_chk_guard`、`___stderrp/___stdinp/___stdoutp`、`_optind`
  - 直接映射 syscall：`open/close/read/write/fcntl/__error/exit/...`
  - 简单 stub：`___maskrune→0`、`_setlocale→0`、`_sysconf→0`、`_getopt`、`_realpath$DARWIN_EXTSN`
  - 需要真实现：`malloc_type_malloc/free`（可用 mmap 版 bump 分配器）、stdio 一族（`fwrite/fprintf/getc/feof/...`）
**注意**：本机 C 命名与 Mach-O 符号差一个下划线（`__error`→`___error`；`_exit`→`__exit`）。
**附**：`dyld_compat.bin`（关段序 policy）**不影响映射**（可保留备用）；覆盖判定点不可改（3 次实测破坏映射）。

## 🔧 2026-09-28 09:4x【更正 + shim 扩展路线已验证成功】
### ⚠️ 更正（用户指出：VM/iPad 盒盖暂停会造成误判）
之前几条"规律"要**打上"可能含 VM 暂停伪影"的标签**：
- "536 映射时好时坏 / 同 boot 同配置两种结果" —— 部分可能是**暂停期间的时序/region 残留**，不能全归因于 region；
- "移走 shim 后连 iOS 缓存都映射不上" —— **VM 正常运行下复核仍成立**（A/B/C 三组各 3 次：在场 rc=0 ×3、移走 rc=134 ×3、放回 rc=0 ×2）✓ **此条保留**；
- 结论：**凡涉及"间歇性"的结论都要标注"待 VM 稳定时复测"**；确定性的（符号、地址、判定链）不受影响。
### ✅ shim 扩展路线：已实测成功（这是 #5 的正解）
`tmp/shim/libSystem_shim.c` + `build_shim.sh` + `deploy_shim.sh`（原作者的 shim 工程，可扩展）。
本次给 shim 依次补了 `___error`（cat）、`___stack_chk_fail/guard`、`___stderrp`、`_execv/_fprintf/_fputc/_readlink/_strcmp`（sh）：
```
sh : 11 个导入【全部解析成功】→ 不再报 Symbol not found（改为 rc=137 被杀 ← 新问题，非符号）
cat: ___error ✅ → 现在只剩 ___maskrune
```
**各程序导入清单（已存 `tmp/imports/*_imports.txt`）**：cat=41、ls=91、sh=11。
### 新问题（下一棒）
`sh` 符号齐了但 **rc=137（SIGKILL）**：可能 ①AMFI/sandbox 拦截 ②它启动时做了某个被判非法的 syscall ③需要 `__progname/environ` 之外的东西。
`cat` 只差 `___maskrune`（+其余 40 个，多为 stdio/socket）。

# ✅✅✅ HANDOVER 收尾（2026-09-28 10:0x）——shim 补尾完成，cat 符号全通
## 一句话结论
**libSystem shim 是可扩展的，我按需补符号直到 `cat` 的 41 个导入全部解析成功**：
```
cat 的报错链（每补一个符号前进一格，全部实测）：
  ___error → ___maskrune → _getopt → _malloc_type_malloc → _warn → _write → 【不再有 Symbol not found】✓
  之后 rc=124（挂起 / 无输出）← 新类别问题，留给下一棒
sh : 11 个导入全部解析 ✓（rc=137 SIGKILL ← 另一新类别问题）
```
## 方法（复刻步骤，10 分钟内可继续）
1. 源码 `tmp/shim/libSystem_shim.c`（裸 `svc` 风格）+ `bash tmp/shim/build_shim.sh`（SDK 双架构 + `install_name_tool -id /usr/lib/libSystem.B.dylib`）
2. 部署：`ldid -Hsha256 -S<ent>` → `cdhash_slices.py` 取每片 cdhash → `jbctl trustcache add` → **cp（勿先 rm！）** + `chmod 755`
   ⚠️ 本次踩坑：scp 失败时我 `rm` 了旧文件导致 shim 一度缺失 ⇒ **先确认新文件到位再替换**
3. 迭代：跑一次 → 读 `Symbol not found: X` → 在 shim 里补 X → 重建部署 → 重复
   - **C 名与 Mach-O 符号差一个下划线**：`___error`←`__error`、`___maskrune`←`__maskrune`、`___stdinp`←`__stdinp`
   - `$` 变体：`extern char *f(...) __asm__("_realpath$DARWIN_EXTSN");`
   - 纯数据符号：`_DefaultRuneLocale`、`___stderrp/stdinp/stdoutp`、`_optind`、`___stack_chk_guard`
4. 导入清单：`lipo -thin arm64 <bin>` + `dyld_info -imports`（已存 `tmp/imports/{cat,ls,sh}_imports.txt`；cat=41 / ls=91 / sh=11）
## 当前状态与下一棒
- **已补齐**：cat 全套符号（stdio 最小实现 fd0/fd1、open/read/write/close/fcntl、getopt 真实现、malloc bump + malloc_type 系列、
  err/warn 家族、`__error`、`__maskrune`、stack-chk、realpath($)、socket 桩…）；sh 的 11 个符号。
- **待解决（新类别，与符号无关）**：
  1. `cat` 运行期**挂起（rc=124，无输出）** —— 疑点：`getopt` 循环、`fstat` 桩返回 0 导致 cat 误判、或 read/write 包装细节；建议下一棒用 lldb/`DYLD_PRINT` 或最小复现（`cat` 单文件 + `</dev/null`）定位；
  2. `sh` **rc=137（SIGKILL）**：符号已通，疑 AMFI/sandbox 或 watchdog；
  3. `ls` 还缺 `libutil.dylib`/`libncurses.5.4.dylib` 两个 shim + 91 符号（工程量大）。
- **旁证保留**：`dyld_compat.bin`（关段序 policy，不影响映射）；缓存映射配方（iOS 先行 → macOS，见上文）；
  铁律：shim 必须存在且签名有效、FS 写要在 cachereg 之前。

## 2026-09-30 CLI 续查：真实 iOS 内核的 shared-region teardown 调用链（静态 RE；未运行新设备实验）

先核对 MCP 的**实际** IDB，而非只信 `server_health`：本次 `server_health` 显示 Instance1=dyld、Instance2=kernel，但 `Instance1.py_eval` 的 `idautils.Segments()` 实际含 `0xfffffe0007004000` 起的 kernel 段及 `0xfffffe000801df20` 函数；`Instance2.py_eval` 实际只见 `0x0` 起的 dyld 段。以下内核反编译/反汇编均由 **Instance1 的实际 kernel 地址空间**取得；此现象与 CONSOLIDATED §14.0 的实例漂移相吻合。内核静态文件：`analysis/kc_raw_16.3_T8112.bin.i64`（文件名并非可靠硬件代号）。不要把 MCP health 的 IDB 名称单独当成 RE 结果的归属证明。

- RE-confirmed：sysent[294] handler `sub_FFFFFE0008459024`：`0x8459098` 检查用户指针为 NULL 后，在 `0x84590e0` 调 `sub_FFFFFE000806391C(task,sr)`，`0x84590ec` 调 `sub_FFFFFE0008060A68(task,0)`，`0x84590f0` 赋返回 0。前一调用的返回值**没有进入 syscall 返回码**。非 NULL 指针在 first-mapping 不存在时于 `0x84590c8` 返回 12 (`ENOMEM`)；不存在 sr 时于 `0x84590d0` 返回 22 (`EINVAL`)。前次 dyld 探针 `check_np(&base)=12` 与“sr 对象存在但没有 first mapping”一致，并**不证明 nested submap 内无其他条目**。
- RE-confirmed：`sub_FFFFFE000806391C` 是实际 teardown 映射尝试：`0x806395c` 从 `sr+0x38` / `sr+0x40` 取 base/size；`0x8063980` 对正常 task map 设 `x5=0x100`（`vm_map_kernel_flags_t.vmkf_overwrite_immutable`，源码 `vm_statistics.h:424-433`），`0x80639e8` 设 `w4=0x4000`（`VM_FLAGS_OVERWRITE`，源码 `vm_statistics.h:276`），`0x80639f4` 调 `sub_FFFFFE00080686BC`，结果直接返回给**不检查结果**的 handler。实际内核该函数没有在此路径打印开源树 `vm_shared_region_remove()` 的错误日志；不要依赖 `dmesg` 找它。
- RE-confirmed：`sub_FFFFFE00080686BC` 把映射交给 `sub_FFFFFE0008017E5C`；无对象的正常路径再入 `sub_FFFFFE0008019768`。后者在 `a5 & 0x4000` 时调 `sub_FFFFFE000801DF20(map,start,end, flags,...)`（`0x8019d60` 附近）：`vmkf=0x100` 时传 `flags=88=0x58`，含 `0x10=VM_MAP_REMOVE_IMMUTABLE`、`0x8=NO_MAP_ALIGN`、`0x40=NO_YIELD`。失败清理路径另一次调用 `vm_map_delete` **不是** overwrite 主路径，不能据此推断 overwrite 参数。若 delete 返回非 0，或 delete 后原条目仍在，随后的 lookup 都可令映射失败；**具体失败点和返回码尚未实测**。
- RE-confirmed：`sub_FFFFFE000801DF20` 是实际 `vm_map_delete`。在 `0x801e2c0` 的 permanent+submap 分支，`flags & 0x10` 为真时**不走** nested-submap 的保护性递归拒绝分支；没有 immutable flag 时才检查 nested entries（`0x801e340` 测 bit19）。因此“iOS 内核无条件禁止 immutable permanent-submap overwrite”已被反证；早先据此作的原因归属是错误假设。真正的拒绝位置仍待确定。源码类比仅供交叉核对：`vm_map.c:18692-18727` / `vm_shared_region.c:2483-2523`，不能代替设备内核结果。

**与现有运行证据的边界**：此前 dyld 进程 `check_np(NULL)=0`、`check_np(&base)=12`、`MAP_FIXED` 在 shared-region VA 返回 `ENOMEM`，并观察到 reserved/is_sub_map 条目；这些**不能**推导出 `sub_FFFFFE000806391C` 的返回码或判定其失败原因。高地址 `0x2ac75c000` 的 `EXC_GUARD` 与低地址 `ENOMEM` 须分别追踪，不可把后者简单解释成 4GB 越界。后续优先采集 teardown 后同一 task 的 map 条目边界和内核实际 `mach_vm_map_kernel` 的 kr（若 KRW 不稳定，优先只读 probe；严禁未经验证地址的内核写入）。对照同条件原生 iOS task 与 macOS dyld task，区分 nested 条目、range/gap 检查和 map-enter 失败；没有返回码前保持 THEORY，不提交任何跳过检查的“修复”。

### 同轮追加：mmap errno 歧义与受探针污染的设备实验

- RE-confirmed（同一真实 kernel IDB）：`sub_FFFFFE0008019768` 在 `0x8019d60` 对 `a5 & 0x4000` 的 overwrite 先调用 `vm_map_delete`；若进入删后 lookup，现存条目覆盖请求区间会返回 `KERN_NO_SPACE=3`；在 delete **之前**还有 `start < map->min_offset || end > map->max_offset` 等边界返回 `KERN_INVALID_ADDRESS=1`。对照开源 `bsd/kern/kern_mman.c:874-886`，**两者都会译为 `ENOMEM`**；且实际设备内核 sysent[197] @ `0xfffffe000799a8f8` 的 `sy_call=0xfffffe00083a7f8c`（IDA `get_qword(sysent+16)`）也已反编译核实：匿名 mmap 在 handler `sub_FFFFFE00083A7F8C` 的 `v74=sub_FFFFFE0008017E5C(...)` 进入 `LABEL_195`；文件 mmap 的 `v74=sub_FFFFFE0008024600(...)` 同样汇入该 label；这里 `v74<=3` 时对 `case 1,3` 返回 `v31=12`，对 `case 2` 返回 13。此前“`ENOMEM` 唯一证明 `KERN_NO_SPACE`/条目没删”已被真实内核**反证**。这只能留下候选分支，不能宣布当前命中哪个。开源 `vm_map.c:3173-3222` 还列有 RLIMIT_AS/DATA 在建图后返回 `KERN_NO_SPACE` 的路径；同样尚未排除。`VM_FLAGS_FIXED` 在 `vm_statistics.h:266` 为零，teardown 的 `0x4000` 即 `FIXED|OVERWRITE`，不是遗漏了 FIXED。
- 2026-09-30 只读设备核对：`nc -z -G 2 192.168.64.1 2222` 成功；`sshpass -e ssh -p 2222 root@192.168.64.1 'uname -a; ls -l ...'` 返回 `root:xnu-8792.82.2~1/RELEASE_ARM64_T8103 iPad13,11`。当前 `/var/mnt/rootfs/usr/lib/dyld` 大小 1239616，mtime `Sep 30 12:02`；`/var/mobile/dyld_mm.bin` 同时间/长度，之前的 `DF/AN/FL/TD/F2` mmap-matrix cave **仍处于部署状态**。`/var/mnt/rootfs/private/tmp/srteardown` 已存在，但不能假定运行它时已进入其 `_start`。
- 运行尝试（不改设备文件）：`sshpass -e ssh -p 2222 root@192.168.64.1 'timeout 25 /var/mobile/run_dbg /var/jb/usr/bin/chroot /var/mnt/rootfs /private/tmp/srteardown'`。观察到的**原文**：`[*] task_for_pid kr=0 port=3075`、`[*] child STOPPED sig=0`、`DF\0...AN\0...FL\0...TD\0...F2\0...`、`[exc] type=12 code0=0xa000000100000000 code1=0x2ac75c000 thr=4099 tsk=3843`、`[vm] 0x2ac75c000..0x2ac760000 prot=1/3 off=0x0 shared=0`；远端 `timeout` 返回 124。**没有**出现 `srteardown.c:33` 的 `[TD] shared_region teardown test`，因此所有输出属于当前 dyld cave 而非 `srteardown`。此次实验不能证明 teardown 或普通进程 mmap 的结果，也不能把 `DF/AN` 当作 srteardown 的记录。异常再次证实 dyld probe 在高地址触发 `EXC_GUARD`；`run_dbg` 的 exception reply 报 `kr=268435459`，应排查捕获器处理方式，不能把 124 解释为进程自发挂起。
- 随后试图仅作原生进程 prewalk：`RUN_DBG_PREWALK=1 timeout 12 /var/mobile/run_dbg /var/jb/usr/bin/true`，SSH 直接报 `UNIX authentication refused` / `Too many authentication failures`，**未启动实验**。不要重复猜测密码或默默替换已部署 dyld；先让用户确认 SSH 认证恢复及设备是否允许短暂切换到受控的 baseline dyld。若允许，先保存当前文件的 hash/签名/路径，按既有 restore runbook 以新 inode 切换，实验后原样恢复；若不允许，使用 iOS-native 原生探针且不要经过 chroot dyld。

### 实测对照：在基线 dyld 下，`srteardown` 的 task 可以完成拆除和 FIXED 映射

用户确认密码仍为原值且**明确允许短暂切换 dyld 后原样恢复**。SSH 增加 `-o PubkeyAuthentication=no -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1` 后连接成功；先前的 `Too many authentication failures` 不能当成设备不可达的证据，具体触发认证失败的条件未独立定位。只读查验：当前 dyld SHA-256=`9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`（1239616 B）；基线 `/var/mnt/rootfs/usr/lib/dyld.plat.keep` SHA-256=`11df010c56a7d0328e853587448c9ed7f345cc36a8b2c0077cb7a0ac5b131249`（1239648 B）。复制到**事前确认不存在**的 `/var/mobile/dyld_cli_pre_teardown.bin` 和 `/var/mobile/dyld_cli_baseline.bin`，并比较原件与备份 SHA-256，输出 `backups verified`。`restore_env.sh` 会**重签 SRC 自身**，所以只把基线**副本**传给脚本，不能传 `.plat.keep` 原件。

部署：`sshpass -e ssh -p 2222 -o PubkeyAuthentication=no -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 root@192.168.64.1 'RESTORE_NO_VERIFY=1 /var/jb/usr/bin/bash /var/mobile/restore_env.sh /var/mobile/dyld_cli_baseline.bin'`，输出 `deployed dyld md5=f24e6c7d82444310db3569c3a5f0112d`、`TC 137c18ada228b0a4b1b9a13187d5dd34aa358ef0 hit=2 (/var/mnt/rootfs/usr/lib/dyld)`；设备的 bash 在 `/var/jb/usr/bin/bash`，**不是** `/bin/bash`。运行相同 probe（无 private/cache 环境变量）：

```
sshpass -e ssh -p 2222 -o PubkeyAuthentication=no -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 root@192.168.64.1 \
  'timeout 12 /var/mobile/run_dbg /var/jb/usr/bin/chroot /var/mnt/rootfs /private/tmp/srteardown'
[*] task_for_pid kr=0 port=3331
[*] child STOPPED sig=0
dyld[5882]: dyld cache '(null)' not loaded: syscall to map cache into shared region failed
[TD] shared_region teardown test
  check_np(&base) ret=-12 base=0x00000000deadbeef
  check_np(NULL)  ret=0
  mmap FIXED @0x180000000 -> 0x0000000180000000
  mmap hint  @0x2ac75c000 -> 0x00000002ac75c000
  mmap FIXED @0x2ac75c000 -> 0x00000002ac75c000
  check_np(&base) ret=-22 base=0x00000000deadbeef
[DONE]
[*] child exited rc=0
```

VERDICT：这个经过**基线 dyld** 启动的探针 task 在 `check_np(NULL)` 之后成功 `MAP_FIXED` 到原 shared-region VA；这里的 shared-region 拆除足以使它的固定映射成功。高 VA 先通过非固定 hint 真实占住一个 16K entry，再用 FIXED 替换也成功；这只证实“预占 gap 可避免本 probe 的空洞 guard”，**不是** macOS shared-cache 的修复，更不意味着可以把 DynamicRegion 改成匿名零页。起初部署的 mmap-matrix dyld 比较实验不成立，因为它的 cave 在 `srteardown` 前触发了 EXC_GUARD；本次切换基线才绕开了该污染。要比较 dyld cave task，需要精确检查它执行 teardown 的时序/映射边界、在同一 task 停住后读 region，而不是把不同进程的 errno 直接归咎于内核 permanent-entry 策略。

实验后**立刻**用新 inode 从原诊断版备份恢复 `/var/mnt/rootfs/usr/lib/dyld`，chmod 755，并由设备 Python 计算 SHA-256：输出 `restored sha256=9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`，等于切换前值。原始基线 `.plat.keep` 未被重签；两个 `/var/mobile/dyld_cli_*.bin` 作为本次可审计备份保留。未写内核、未把临时探针当生产修复。

## 2026-09-30 CLI 下一阶段：真实缓存 libSystem 的可检验取证（仅离线准备）

**目标纠偏**：`CLI-MILESTONE-2026-09-28.md` 的 `echo/cat/msh` 用的是磁盘 shim；这不是用户要求的“macOS 15.6.1 rootfs 真实系统库 CLI”。阶段性验收必须同时满足：原版 rootfs 命令正确输出和 rc=0；`DYLD_PRINT_LIBRARIES`/映射证明实际 libSystem 来自 **macOS** cache（先前目标 UUID `D161E41A`，须按设备 cache 再核对），非磁盘 shim 和非 iOS 缓存；无 `not loaded`、`wrong platform` 或未消费映射页的伪成功。系统级 536 的 region 固定 4 GB，不能直接覆盖整个约 4.77 GB 的 macOS cache；private mmap 路径是当前优先调查对象，但也要保留主缓存限定对照，不能先验宣布它完全不可用。

**RE 交叉核对**：本轮 IDA `py_eval` 看真实 segment，Instance1 是 `0xfffffe0007004000` 起内核、Instance2 是 `0x0` 起 dyld，仍不能靠 server_health 名称猜。实际 dyld `deallocateExistingSharedCache` @ `0x3420c`: `0x34224` 调 `check_np(&base)`，`0x34228 CBNZ` 令 ret=12（sr 对象空）时跳过 `0x34230 check_np(NULL)`；实际 `mapSplitCachePrivate` 在入口预检之后调用它，并在循环固定 mmap 前使用预期 VA。源码类比 `analysis/dyld-dyld-1286.10/dyld/SharedCacheRuntime.cpp:865-1012,1455-1465`。真实内核 `sub_FFFFFE000806391C` 使用 `VM_FLAGS_OVERWRITE=0x4000`、immutable 覆盖标记；但 syscall 294 handler 不透传它的内部 kr。真实 `vm_map_enter` @ `0xfffffe0008019768` 的范围检查（反编译中 `v167 < min || v167+size > max`）会给 `KERN_INVALID_ADDRESS=1`，overwrite-delete 后 lookup 有 `KERN_NO_SPACE=3`，两者经实际 sysent[197] 的 BSD handler 都成 `ENOMEM=12`。故先前 `deallocnp` NOP/`check_np(NULL)==0` 均**不足**以证明原因或修复；当前未采到内部 kr。高 VA 的 `EXC_GUARD DEALLOC_GAP` 独立追踪。

**实验前主机侧准备（此时未部署设备；后续首次实验见下节）**：

- `misc/run_dbg.c` 保留旧 `RUN_DBG_PREWALK`、`RUN_DBG_KILLONSTOP` 行为；`RUN_DBG_LIVEWALK=1` 时新增每个 SIGSTOP/SIGUSR1 的 `[live] stop=N` 标记，从 `0x180000000` 走到 `0x2ac760000`，显示 region 的 prot/offset/shared/reserved 和 `mach_vm_region` 跳过的 **gap**。`RUN_DBG_STOP_LIMIT=2` 只在第二个有效 stop 后结束**实验子进程**（spawn 悬挂遗留 `sig=0` 不计数）。每条 `object_name` send right 用 `mach_port_deallocate` 释放；可读现有 stop 洞，不涉及 kernel 写入。
- `misc/srteardown.c` 只有在 `-DSRTEARDOWN_STOPS` 构建时调用 `getpid(20)`/`kill(37,signum)`；原 v1 错用 `19=SIGCONT`，v2 已改 `17=SIGSTOP`，意图是在 `check_np(NULL)` 前后停住；缺省构建维持原实验输出/行为。上次基线 dyld 下它的成功映射属于 **app `_start` 较晚阶段**，不能替代 private dyld 入口对照。
- `analysis/dyldwork/build_dyld.py` 新的 `srpair_e` + `srpair_c` 把 private 入口 `0x342dc`（IDA 原字 `PACIBSP=0xd503237f`）跳到 `0x970`（IDA 验证前 `0x90` 字节全零）。cave 顺序：getpid→SIGSTOP（前态）→`check_np(NULL)`→把 raw syscall ret 写 fd2 8 字节→getpid→SIGSTOP（后态）→自旋，保证不带不完整寄存器状态返回 dyld。site `a531ff17` 经 IDA 位移计算落在 `0x970`；cave 共 22 条/88B，host clang 单独汇编 `/tmp/srpair_check.s` 后 `otool -tV` 验证每条语义，`otool -t` 的编码与 builder `_le()` 输入逐 word 一致。builder 拒绝漏配另一半、与 `deallocnp` 联用或与 `mmprobe` 等占用相同 site/cave 的 key 联用。此为**实验 child task 内主动尝试 teardown** 的用户态诊断，不是只读调用、不是 fix，也不使用内核写入；只有 `run_dbg` 的 VM-region 快照是只读的。
- Host `clang -target arm64-apple-ios14 -isysroot ~/theos/sdks/iPhoneOS16.5.sdk -O2 -o /tmp/run_dbg_cli_pair misc/run_dbg.c` 构建成功（仅旧有 pointer-sign warning）；`clang -target arm64e-apple-macosx14 -isysroot <同一 SDK> -nostdlib -DSRTEARDOWN_STOPS -Wl,-e,__start -lSystem -o /tmp/srteardown_cli_pair misc/srteardown.c` 成功（sysroot target mismatch 警告）；普通/双停探针各 `-fsyntax-only` 通过。`python3 analysis/dyldwork/build_dyld.py /tmp/dyld_srpair_test.bin crossarch plataccept hardpriv srpair_e srpair_c` 构建 1240752 B；故意叠 `mmprobe_c` 或遗漏 `srpair_c` 均按预期失败。**这些产物未签名、未注册 TC、未上传 iPad；不能把构建成功当设备实验结果。**

**设备当前只读确认**：以 `-o PubkeyAuthentication=no -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1` 读取 dyld SHA-256=`9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`，与先前恢复后的值及 `/var/mobile/dyld_cli_pre_teardown.bin` 一致；`dyld.plat.keep` SHA-256=`11df010c56a7d0328e853587448c9ed7f345cc36a8b2c0077cb7a0ac5b131249`。本轮**没有**再次切换 dyld、kill 已有 daemon、重启或运行新增设备 probe。用户对再次切换的选择是 skip；下次需具体列操作和恢复命令、取得授权后才能比较 private dyld 与 app 入口两种相同 stop 快照。预计用 `RUN_DBG_LIVEWALK=1 RUN_DBG_STOP_LIMIT=2`，以 `[live] stop=1/2` 的差别和 ret、缓存真实加载证据定分支；若两个阶段 VM map 相同而 app 的 `check_np(NULL)` 能拆，则进一步对照 task VM max/min、region 身份和时间点，**不要**从单个 errno 直接打补丁。附带只读检查：设备 `/var/jb/usr/sbin/sysctl kern.developer_mode_status` 返回 `sysctl: unknown oid 'kern.developer_mode_status'`（rc=1）；§14.3 设想的该 sysctl 不能用于本设备确认开发者模式，**不能据此推断模式关闭**。

### 已授权的一次设备双停点试验：信号编号错误，VM 对照未发生

用户在离线准备后**只授权这一次**临时切换 dyld。先只读校验 `dyld` 与新备份 `/var/mobile/dyld_cli_srpair_pre.bin` 都是 SHA-256 `9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`；将 host 编译的 dyld 和 `run_dbg_pair` 上传设备 `/var/mobile/`，分别以 macOS ent 和 `misc/run_nocskill.entitlements.plist` 签名，CDHash `bac138526a5b1ddb15f5a3e27c61178ff25dd3b4`、`49f7f216b954c3c1931a69bf1e91b6102761b528`，经 `jbctl trustcache info` **转小写**比较均命中（初次大小写敏感检查误报 false，已当场复核）。远端 shell 的 `EXIT` trap 负责从备份创建新 inode 并恢复原解释器、核验 SHA。探针 dyld SHA-256 `477f6fe8b3c7a07b8b9fc4fec801e09e86cedabc68ed1a8718e7ccd639fc277e`。

原始设备文件 `/var/mobile/dyld_cli_srpair_trace.raw`（156B）末尾关键数据：
```
[*] spawned pid=6119 (suspended)
[*] jbctl rc=0
[*] task_for_pid kr=0 port=2563
[*] set_exc_ports kr=0
[*] task_resume kr=0
[*] child STOPPED sig=0
<随后 8 字节 00 00 00 00 00 00 00 00>
EXPERIMENT_LAUNCHER_RC=124
RESTORED_SHA256=9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1
```
`ps -p 6119` 未发现遗留 child。**没有** `stop=1/2` 快照；8 字节零仅显示原始 `check_np(NULL)` 返回 0，不证明 VM 条目删除。IDA/仓内 XNU `bsd/sys/signal.h:105,107,122` 验证 Darwin `SIGSTOP=17`、`SIGCONT=19`、`SIGUSR1=30`；最初 `srpair_c`、旧 `stopcave`/`stopnpcave`/`np2dump2` 和 `run_dbg` 都误把 `kill(pid,19)` 视为 STOP，实际上只是 CONT。先前许多“没收到 SIGSTOP/探针自身 spin”的解释需以此修正。这是**探针自身 ABI 常量错误**，不能拿缺少停点归因于设备或内核 VM。

主机已将 `srpair_c` 的两处 `MOV X1,#19` 改为 `MOV X1,#17`（汇编字 `d2800221`），将 `run_dbg` 有效信号判断改为 SDK `SIGSTOP/SIGUSR1`，`misc/srteardown.c` 可选停点改用 SDK `SIGSTOP`。新产物在 `/tmp/dyld_srpair_test_v2.bin`、`/tmp/run_dbg_cli_pair_v2`、`/tmp/srteardown_cli_pair_v2`；`otool -tV /tmp/srpair_check_v2.o` 确认两次 `mov x1,#0x11`，编译成功。**此时 v2 尚未部署**；其获准重测与结论见下节。

### 第二次独立授权的 v2 重测：停点成功，但 VM 查询用的 task port 已失效

用户另行明确授权只运行一次修正后的临时 dyld 切换。开跑前当前 dyld 和先前备份 SHA-256 相同，旧 `.dyld_cli_srpair_stage`/`_restore` 和新 trace 路径均不存在；v2 dyld 和 v2 run_dbg 的签名 CDHash 分别为 `0136cdfb8749804b221773fa6799e075d50f9881` 与 `a05c43416bcfbdba225174eec6cc74547bca8175`，经 `jbctl trustcache info` 规范化大小写后均命中。v2 dyld 部署时 SHA-256 `5262c837427c9f0af5520892d28554f647e7340f672151489a928227883c87d5`。remote EXIT trap 恢复原 dyld，输出 `RESTORED_SHA256=9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`。

设备保存原始日志 `/var/mobile/dyld_cli_srpair_trace_v2.raw`（431B），`/var/mobile/dyld_cli_srpair_trace_v2.out`；标准错误关键原文：
```
[*] spawned pid=6178 (suspended)
[*] jbctl rc=0
[*] task_for_pid kr=0 port=2563
[*] set_exc_ports kr=0
[*] task_resume kr=0
[*] child STOPPED sig=0
[*] child STOPPED sig=17
[live] stop=1 walk 0x180000000..0x2ac760000
[live] region lookup kr=268435459 at 0x180000000
<8 bytes 00 00 00 00 00 00 00 00>
[*] child STOPPED sig=17
[live] stop=2 walk 0x180000000..0x2ac760000
[live] region lookup kr=268435459 at 0x180000000
[*] killing child
[*] child SIGNALED 9
```
远端 launcher rc=0（runner 正常处理两个停止点），但该 rc **不是 macOS CLI 成功**。真实 XNU `osfmk/mach/message.h:1184` 定义 `268435459=0x10000003=MACH_SEND_INVALID_DEST`。两个快照都未读取任何 VM region；**不能**根据查询失败说 region 存在/不存在或拆除成功/失败。8 字节 0 仍只是 syscall 294 的表面返回值。`run_dbg` 在 child 尚处于 suspended `chroot` 阶段时取的 task port，之后 `chroot` exec 到 macOS image；旧 port 在最终停点无效是**待证实的具体机制**，首先应在有效 SIGSTOP 时重新 `task_for_pid` 并打印旧/新 port 与返回码。

主机 `misc/run_dbg.c` 已做最小修订：每个有效 stop 重新调用 `task_for_pid`，若成功，释放旧 send right、用新 port 做只读 `mach_vm_region`；失败则原样报告，不猜测映射结果。`clang -target arm64-apple-ios14 -isysroot ~/theos/sdks/iPhoneOS16.5.sdk -O2 -o /tmp/run_dbg_cli_pair_v3 misc/run_dbg.c` 编译成功（只有先前 pointer-sign warning）。**此时 v3 尚未部署**；第三次单独授权的试验和结果见下节。

### 第三次单独授权的 v3 实测：同一 dyld task 的 teardown 真正替换 permanent submap

用户明确表示直接继续，并在单独确认问题中授权一次 v3 实验。最初 SCP 遇一次 `Permission denied`（无文件传输、更未替换解释器），停止重试、确认用户意图后以已知认证方式成功只读登录并再校验现有 dyld SHA，随后上传、签名 v3 runner（CDHash `8ae928a08687dd8217b2718948ed0e5899069aca`），核对 v3 runner 与既有 v2 dyld 均在 TC。此轮使用既有 v2 dyld，不改补丁字节；运行中该 dyld SHA `5262c837427c9f0af5520892d28554f647e7340f672151489a928227883c87d5`，18 秒超时保护下一个 child 正常被 runner 结束，EXIT trap 报 `RESTORED_SHA256=9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`，独立 SSH 读取同 SHA，`ps -p 6236` 无残留实验进程。

设备原始 `/var/mobile/dyld_cli_srpair_trace_v3.raw`（621B），关键原文：
```
[*] spawned pid=6236 (suspended)
[*] jbctl rc=0
[*] task_for_pid kr=0 port=2819
[*] child STOPPED sig=0
[*] child STOPPED sig=17
[live] task_for_pid kr=0 old=2819 new=3331
[live] stop=1 walk 0x180000000..0x2ac760000
[live] 0x180000000..0x280000000 prot=1/1 off=0x0 shared=0 resv=1
[live] gap 0x280000000..0x2ac760000
<8 bytes 00 00 00 00 00 00 00 00 = check_np(NULL) raw ret>
[*] child STOPPED sig=17
[live] task_for_pid kr=0 old=3331 new=3331
[live] stop=2 walk 0x180000000..0x2ac760000
[live] 0x180000000..0x280000000 prot=0/0 off=0x0 shared=0 resv=0
[live] gap 0x280000000..0x2ac760000
[*] killing child
[*] child SIGNALED 9
```

**运行确认的事实（同 PID、同 task 在两个信号点）**：exec 后重新 `task_for_pid` 从旧 port 2819 换得可用 port 3331，旧 port 确实不能用于最终 image 的 map；`check_np(NULL)` 将原始 `resv=1` shared-region submap **替换**为非 reserved、`PROT_NONE` 的普通 4 GB 映射，不是把所有 VM entries 删成空洞。`check_np(NULL)==0` 仍只说明 syscall 外层成功，**这次是 VM snapshot 而非返回值**证明 4 GB 实体发生变化。高 VA `0x280000000..0x2ac760000` 在前后都是 gap，空洞 FIXED 触发的 `EXC_GUARD` 是独立关卡。dyld 原版 `deallocateExistingSharedCache@0x3420c` 在 `check_np(&base)==12`（空 sr）时 `CBNZ@0x34228` 跳过 teardown，说明它未走刚刚证实可替换 submap 的路径；**还需受控测试**使它只在空 sr 时调用 teardown 后，私有 cache 低地址固定文件 mmap 是否成功，不可从 VM 条目变化单独宣布 CLI 跑通。历史的 `deallocnp` 无条件 NOP 诊断未成功，不能删去该反例；当时另有入口 cave/同 task 时序混淆，须对照实际部署的 key 和日志后重评。无需新 kernel 写入。下一步优先做条件化 ret=12 的最小 dyld branch fix；再单独处理高 VA 的合法预占、实际签名页和缓存 libSystem 来源。

**基于 v3 的离线最小条件补丁（尚未设备部署）**：IDA 对实际 dyld `0x34220..0x3423c` 的原始反汇编显示：`BL check_np(&base)` @`0x34224`; `CBNZ W0,0x34234` @`0x34228`，原字 `60000035`; `MOV X0,#0` @`0x3422c`; `BL check_np(NULL)` @`0x34230`; `LDP/ADD/RETAB` @`0x34234..0x3423c`。在 `build_dyld.py` 增加配对 `emptysr_e`（把非零 ret 导入空的 `0x47290` cave）和 `emptysr_c`（`cmp w0,#12; b.ne 原 epilogue 0x34234; b 原 teardown 0x3422c`）。因此 ret=0 完全保留原流，ret=12 仅补做原来的 teardown，其他非零仍沿原 skip；没有替换 `check_np` 本身、更没有 NOP 掉错误检查。IDA `py_eval` 独立验算分支落点（site `40830935`、cave `1f300071017df654e5b3ff17`），host clang `cmp w0,#12` 汇编 word `7100301f` 一致。builder 先校验原 site word，再拒绝配对缺漏及与已有 `deallocnp`、`srpair` 或相同 cave 的 mdump 等叠加。

Host `python3 analysis/dyldwork/build_dyld.py /tmp/dyld_emptysr_test.bin crossarch plataccept hardpriv emptysr_e emptysr_c` 成功（1240752B），故意叠 `mdumpcave` 或漏配 cave 均按预期失败，`git diff --check` 通过。**这只是有实证根据的修复候选，不是设备上已证实的成功路径**；尤其旧无条件 `deallocnp` 曾没有使真实 mmap 成功，不能隐藏该反例。后续获准的设备实验应只替换这个条件分支并在低 VA file mmap 返回处记录实际结果，失败则检验后态 map 和返回 VM 码。

**高地址另案静态证据**：先核对本文件既有的“Exact overflow map” §（约 1679-1696 行）：`.01` 的 m2 从 `0x27dfd8000` 跨过 4 GB 顶到 `0x28188c000`，m3..m6 全在高区并延续到 `0x2ac75c000`；因此**文件映射会先于 DynamicRegion** 碰到潜在的 gap guard，不能只处理最后 16K 动态配置。此前还有分拆 system-wide/main + private 尾部的 hybrid 方案（约 1702-1729 行），其范围与真实缓存 libSystem 来源仍需对照，别重造已有路线。实际 macOS 15.6.1 dyld `DyldSharedCache::DynamicRegion::make@0x511c0` 反编译：`prefAddress!=0` 直接 `mmap(prefAddress,0x4000,3,4114,-1,0)`，4114=`0x1012=MAP_FIXED|PRIVATE|ANON`，失败返回空；对照开源 `common/DyldSharedCache.cpp:2175-2199`。动态配置 `0x2ac75c000` 落在 v3 前后可见的空洞；以前基线 app 的 `srteardown` 证明该高 VA 先 **非固定 hint 建真实 entry，再 FIXED 覆盖同址** 可避免本 probe 的 DEALLOC_GAP。尚未验证原版 dyld 与真实 cache 是否能用同样顺序；若试验必须检查 hint 真正返回预期地址、失败回滚误映射、覆盖后的内容/保护，而非用匿名零页伪装缓存内容。签名页消费和 libSystem provenance 仍是独立门槛。

### 第四次单独授权：条件化 empty-sr teardown 让私有缓存前进到 `.01` 跨界 mapping

用户就新 `emptysr_e/emptysr_c` 目标另行明确授权一次临时替换。替换前从原 dyld 独立创建 `/var/mobile/dyld_cli_emptysr_pre.bin`，双 SHA 为 `9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`；`/var/mobile/dyld_emptysr_test.bin` 签名 CDHash=`6ff7c93e159ba75076e21c880fd87237e1079d33`，TC 验证命中。运行时 dyld SHA=`2b981ed621a649be3783df32fd0a192119bbfda0950a6ccfde11afe0105a78da`；只运行一个 `timeout 15 /var/mobile/run_dbg_pair_v3 /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI` child，远端 EXIT trap 恢复后及独立 SSH 双重核验最终原 SHA。实验 PID 6300 的 `ps -p 6300` 无遗留。

设备原始 `/var/mobile/dyld_cli_emptysr_trace.raw`（586B）要点（原文）：
```
[*] spawned pid=6300 (suspended)
[*] jbctl rc=0
[*] task_for_pid kr=0 port=2563
[*] task_resume kr=0
[*] child STOPPED sig=0
[exc] exception msg id=2405 size=84
[exc] type=12 code0=0xa000000100000000 code1=0x280000000 thr=3587 tsk=6915
[exc] pc=0x104454f90 lr=0x1044845b0 sp=0x16bc330c0 cpsr=0x40001000
[exc] x0=27dfd8000 x1=0 x2=3 x3=40012
[exc] x4=4 x5=569cc000 x16=c5 x30=lr
[vm] 0x27dfd8000..0x28188c000 prot=3/3 off=0x569cc000 shared=0
[exc] reply kr=268435459
EXPERIMENT_LAUNCHER_RC=124
RESTORED_SHA256=9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1
```

IDA 对实际 dyld `_mmap` stub `0x4f88 MOV X16,#0xc5`、`0x4f8c SVC #0x80`、`0x4f90 B.CC` 确认 PC 是 **mmap syscall 返回点**；`lr=...45b0` 与 private 循环 call site `0x345ac→0x345b0` 相符（dyld load base 相同），`x16=0xc5=197`。异常的 `code1=0x280000000` 是**4 GB shared-region 顶边界**，不是后续 DynamicRegion 的 `0x2ac75c000`。`mach_vm_region` 在异常捕获时看到 `.01` m2 `0x27dfd8000..0x28188c000` 文件映射，起始 fileoff `0x569cc000`，prot=3；与历史 Exact overflow map 的 m2 范围相符，证明顺序已推进至至少这条**跨界**映射。不能因 VM entry 存在断言 m2 每页都有效、可执行或完整成功：本次 kernel 在边界抛 `EXC_GUARD`，debugger 的 exception reply 因错误使用接收 port 而非请求自带的 send-once reply port 报 `MACH_SEND_INVALID_DEST`、父 runner 最终超时（静态勘误见下段）。stdout 仅是 jailbreakd 的 `Successfully marked proc...`，**没有**原版 `/bin/echo` 的 `HI`，因此真实缓存 libSystem CLI 仍未达标。

**归因范围**：与先前同 cache/私有路径 `15 条 FIXED ENOMEM` 的运行相比，ret=12 条件化 teardown 后至少取得 `.01` m2 VM entry，支持低地址 shared-region 清理前提成立；但 `hardpriv`、cache 状态、签名与不同诊断 cave 的历史差异仍需保留，不要夸大成“所有前 10 条映射已被实际页读取验证”。下一个独立关口是 `MAP_FIXED` 跨越 `0x280000000` 或后续高区空洞触发 `DEALLOC_GAP`。**THEORY / 待证伪**：在调用第一个文件 mmap 前，用非固定 hint **仅预占**真实高区 `[0x280000000,0x2ac760000)` 的 PROT_NONE 虚拟区，必须核对返回 VA 正好等于 hint，之后让原有 `MAP_FIXED|FILE` 逐段真实覆盖；若内核仍在边界报 guard，则否证假设。它不改变真实缓存内容或重定位，但 VM 虚拟地址大约 0.69 GB、用户地址空间/资源限制必须另测；不可把占位页当缓存 image，且必须在文件覆盖及实际页验证之后才谈 CLI 成功。参考既有 hybrid 方案和基线 app 的高 VA 16K hint/FIXED 成功对照，避免重造错误方案。

**异常捕获器独立勘误（主机已编译，设备未测 v4）**：旧 `misc/run_dbg.c:126` 构造 exception reply 时把 `msgh_remote_port` 误设成收到消息的 `msgh_local_port`（exception 接收 port），所以 `mach_msg` 返回 `0x10000003=MACH_SEND_INVALID_DEST`、child 等回复、外层 timeout=124。项目已有可工作的 `misc/excsnap.c:77-84` 与 `misc/run_nocskill.c:254-261` 都用**请求的 `msgh_remote_port`**（send-once reply right），且把请求 NDR 原样拷贝到 reply。已按相同协议修正 `run_dbg.c` 的 reply port 与 NDR，host `clang ... -o /tmp/run_dbg_cli_pair_v4 misc/run_dbg.c` 成功（旧有 pointer-sign warning）。**此时尚无设备 v4 数据**；下述独立原生进程测试已补足协议验证。该修改与上述 VM teardown/高地址占位分别验证。

**v4 捕获器独立实测（不替换 dyld）**：主机用 iOS target 编译临时 `int main(void){ __builtin_trap(); }`，上传为 `/var/mobile/run_dbg_exc_probe`；与 `/var/mobile/run_dbg_pair_v4` 分别签名（CDHash `d4e5c835092fd80d4c810e6d0e935b49031eb4a3`、`45825709b8906a7a392fa75d30be31f366aa5303`），TC 经设备实际列表核验。设备 `timeout 8 /var/mobile/run_dbg_pair_v4 /var/mobile/run_dbg_exc_probe`：`[*] child STOPPED sig=0` → `[exc] exception msg id=2405 size=84` → `[exc] type=6 code0=0x1 code1=0x104610000` → `[*] child SIGNALED 5`，命令正常 rc=0、没有 `reply kr=268435459`、没有外层 timeout。**运行确认**修正后的 exception reply 可以被 kernel 消费并正常递送 SIGTRAP；这只是原生 iOS child 的协议验证，不证明 macOS dyld/cache 任何新的结果，也不验证 EXC_GUARD 的内核策略。

**下一关的主机侧诊断候选（尚无设备数据）**：新增 `build_dyld.py` 的 `highreserve_e/highreserve_c`，仅能与 `emptysr_e/c` 成对共存。IDA 原版 `mapSplitCachePrivate@0x342dc` 首指令 `PACIBSP`，分支 `a531ff17` 落在验证全零的 `0x970` 洞；host clang 将 `/tmp/highreserve_check.s` 编成 0x8c 字节（≤已验证 0x90 零区），逐 word 复核 `otool -t/-tV`。cave 保存 `x0..x5,x8,x9,x16`，执行**非固定** `mmap(hint=0x280000000,len=0x2c760000,PROT_NONE,MAP_PRIVATE|MAP_ANON,fd=-1)`，若结果不是严格相同 VA，则释放偏离的临时映射并 `exit(87)`（显式诊断失败）；若匹配则恢复全部暂存寄存器/SP，重放原 `PACIBSP`，在 cave `0x9dc` 用 IDA 验算的 `B 0x342e0` (`0x1400ce41`) 回原函数剩余序列。后续 `.01` 高区和 DynamicRegion **仍由原 dyld 的真实 `MAP_FIXED|FILE` / RW mmap 覆盖**；占位只为了把原先空洞变成已登记 VM entry，不代替缓存文件数据。建构命令：`python3 analysis/dyldwork/build_dyld.py /tmp/dyld_emptysr_highreserve.bin crossarch plataccept hardpriv emptysr_e emptysr_c highreserve_e highreserve_c`，1240752B；与 `mmprobe_c` 同洞组合按预期被拒，`git diff --check` 通过。该 candidate 的非固定 hint 可能被内核移到别处/资源限制拒绝（会以 87 显式失败），即使回到原 dyld 仍可能遇 code-signing、平台选择和真实页 fault。**此时尚未执行新的解释器切换**；结果见下节。

### 第五次单独授权：高地址 guard 不再出现，转为主缓存 m3 首字节 `KERN_MEMORY_ERROR`

用户单独确认了高区 PROT_NONE 非固定 hint 的受控诊断。设备原 dyld 和新备份 `/var/mobile/dyld_cli_highreserve_pre.bin` 运行前 SHA 均为 `9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`；新候选签名 CDHash `5a1d78f23b16c1b3e3dceacf5b043c6228ab9022` 与已单独验证的 v4 runner 哈希均在设备 TC。临时 dyld SHA `3f2048c9303464f673c8df2a661748a642175415c95ace31b3ff7f91a6937062`，单个隔离 child 使用 `timeout 15 /var/mobile/run_dbg_pair_v4 /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI`；trap 与独立 SSH 再核验恢复原 SHA，无残留 PID 6388。

设备原始 stderr `/var/mobile/dyld_cli_highreserve_trace.raw`（736B），关键原文：
```
[*] spawned pid=6388 (suspended)
[*] jbctl rc=0
[*] task_for_pid kr=0 port=6915
[*] task_resume kr=0
[*] child STOPPED sig=0
[exc] exception msg id=2405 size=84
[exc] type=1 code0=0xa code1=0x1ee188000 thr=5891 tsk=2563
[exc] pc=0x10294b9f8 lr=0xa01200010294b9ec sp=0x16d679dd0 cpsr=0x60001000
[exc] x0=1ee188000 x1=8 x2=16d6799ff x3=e
[vm] 0x1ee188000..0x1ee1ac000 prot=3/3 off=0x0 shared=0
[vm] 0x1ee1ac000..0x1ef3ac000 prot=3/3 off=0x6c1ac000 shared=0
[*] child SIGNALED 10
EXPERIMENT_LAUNCHER_RC=0
RESTORED_SHA256=9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1
```

**严格判读**：XNU `osfmk/mach/kern_return.h:118` 定义 `KERN_MEMORY_ERROR=10`；这是 type=1 `EXC_BAD_ACCESS`，**不是之前的 type=12 EXC_GUARD**；v4 reply 正常，父 runner rc0 仅表示捕获并等到 child 的信号退出，child SIGBUS(10)，没有 `/bin/echo` 的 `HI`。既有本文件 1037-1046 行的原始映射表确认主缓存 m3 `[0x1ee188000,0x1ee1ac000)`、fileoff `0x6c188000`（正常 `mach_vm_region` 在此段头输出 `off=0` 是 VM object offset，不等于 cache 文件偏移）；该 VM entry `prot=3/3` 存在仍不能证明实际页可读取。实际 dyld 地址 slide 推出 `pc=0xb9f8`；IDA `dyld4::CacheFinder::CacheFinder@0xb854` 的 `0xb9f0 LDRB [X19,#0x30]`、`0xb9f4 CMP W8,#1`、`0xb9f8 B.NE` 表明异常时已在装载缓存后的 CacheFinder 阶段；fault 地址是缓存 m3 首字节，非 dyld text PC。**runtime-confirmed** 此次没有重现边界 guard，已推进到缓存数据的首次 fault；但无法从 code0=10 唯一推断 vnode pager、文件覆盖、签名页校验还是其他 VM 错误。必须继续查真实内核 fault/backing 及设备报错，不能给 kernel 签名 check 打 NOP。高区 PROT_NONE 只是帮助 VM 占位的**诊断 scaffold**，不是最终数据来源，也未验证全部 cache 映射可访问。

**不替换 dyld 的反证对照（设备仍为原 dyld SHA）**：真实 cache symlink 是 chroot 的 `/System/Library/dyld/dyld_shared_cache_arm64e -> /tmp/dsc/dyld_shared_cache_arm64e`；iOS 宿主原路径 `/var/mnt/rootfs/private/tmp/dsc/dyld_shared_cache_arm64e`（size `0xa1b18000`），**不是**宿主 `/var/mnt/rootfs/System/Library/dyld/...`（绝对 symlink 在 chroot 外解析会失败）。新增 `misc/cache_page_probe.c` iOS 原生、签名 TC CDHash `3c4a7fdc5570b5c7c92586847e940093fbbc9c9c`，设备普通 `mmap(NULL,0x4000,RW,PRIVATE,fd,0x6c188000)` 首字节=0x00，单独 `pread(fd,offset=0x6c188000,1)` 返回 n=1 byte=0x00。

进一步新增 `misc/cache_page_fixed.c` 的 iOS 原生 helper（只在自身 task 做 raw syscall，不改动设备解释器或全局 VM；签名 CDHash `b02ededefc2040ce0977427413cee4ea387f1ee8`，TC 验证），原生可执行 text/数据放在低 `0x100...` 区，因此 `check_np(NULL)` 后不用已解除的 iOS libc，而直接走自身的 `svc(5/294/197/4/1)`。clang `-target arm64-apple-ios14 -fno-stack-protector -O2` 产物 `otool -tV` 核对 teardown 后无任何 libc 调用。由验证过 exception reply 的 v4 runner 启动，原始输出：
```
[*] spawned pid=6456 (suspended)
[*] jbctl rc=0
[fixed] open=0x0000000000000003
[fixed] check_np(NULL)=0x0000000000000000
[fixed] mmap=0x00000001ee188000
[fixed] reading
[fixed] value=0x0000000000000000
[*] child exited rc=0
```
固定地址 `0x1ee188000`、file offset `0x6c188000`、prot=RW、flags=`MAP_FIXED|PRIVATE|UNIX03=0x40012` 与 dyld 目标 m3 的起点/参数相同；**主缓存相同页在另一个 task 可真实读取**，直接反证“主文件磁盘页普遍坏掉/该地址不可映射”。但此探针只映射 16K，而 dyld m3 为 0x24000；也没有 macOS 平台 executable/task 语义，不足以定因。下一组单变量：原生 task 以完整 m3 长度固定 mmap，再对比两种 task 的 `mach_vm_region` extended info/entry backing、代码签名与 fault 错误路径（如 page error、shadow severed、pager 自身返回码）；严禁直接上内核 bypass。

**映射长度控制试验**：同一 `misc/cache_page_fixed.c` 用 `-DCACHE_PAGE_LENGTH=0x24000` 编译新原生 helper，与实际主缓存 m3 **完整长度一致**，签名 CDHash `76816a6c55fd011828140db162e76c3dffd44730` 并验证 TC。v4 runner 独立 child 原文：
```
[*] spawned pid=6476 (suspended)
[fixed] open=0x0000000000000003
[fixed] check_np(NULL)=0x0000000000000000
[fixed] mmap=0x00000001ee188000
[fixed] reading
[fixed] value=0x0000000000000000
[*] child exited rc=0
```
现在按 symlink 推导的文件路径、fileoff `0x6c188000`、VA `0x1ee188000`、size `0x24000`、RW、flags `0x40012` 均可与 dyld m3 对齐（实际 vnode/inode 仍须验证）；此前原生成功不能仅归结于 mmap 长度短。**仍存在的显著未控变量**包括：原版 macOS exec task 的 platform/csflags/VM map 状态、前面缓存段映射和高区预占、pager/object chain、是否真正复用了同一个 vnode/fileglob（可只读 stat/inode/extended info 核对）。不把 `KERN_MEMORY_ERROR` 直接归因于代码签名，也不改核内策略。

**相邻段顺序 + VM object 只读基线**：iOS 原生版 `-DCACHE_MAP_PREFIX -DCACHE_PAGE_LENGTH=0x24000` 在自身 task 先按 main m0/m1/m2 的真实 VA/foff/长度/保护逐一 FIXED 映射（程序输出分别 `0x180000000`、`0x1e7f5c000`、`0x1ebdec000`），接着 main m3 `0x1ee188000` 首字节仍可读 0x00、child rc0。前 3 段造成数据页失败的简单解释进一步被排除。扩展后的 `misc/run_dbg.c` v5 只读查询 `csops(CS_OPS_STATUS)`、`mach_vm_region(VM_REGION_EXTENDED_INFO)`，并以 `CACHE_STOP_BEFORE_READ` 在 iOS-native child 的 m3 映射完成后停住：
```
[*] spawned pid=6526 (suspended)
[fixed] prefix=0x0000000180000000
[fixed] prefix=0x00000001e7f5c000
[fixed] prefix=0x00000001ebdec000
[fixed] mmap=0x00000001ee188000
[*] child STOPPED sig=17
[live] task_for_pid kr=0 old=7171 new=7171
[live] csops rc=0 flags=0x3680380d errno=0
[liveext] 0x1ee188000..0x1ee1ac000 tag=0 resident=1 external=1 shadow=1 mode=1 ref=5
[live] 0x1ee188000..0x1ee1ac000 prot=3/3 off=0x6c188000 shared=0 resv=0
[*] child SIGNALED 9
```
最后 SIGKILL 是 runner 按 `RUN_DBG_STOP_LIMIT=1` 结束**仅此实验 child**，不是 m3 fault；此前无 stop 的相同前缀探针已在读页后正常 rc0。native 数据含 `external_pager=1,shadow=1,resident=1`，`csflags=0x3680380d` 为对照基线而非 macOS task 状态。v5 runner/stop helper 分别按 `misc/run_nocskill.entitlements.plist` 签名，CDHash `83ed83c4639f970c76a8bd63261a0af5b383bc91`、`6b5397a8096ef88c8a6dfe74aca2b330a877fc0e`，TC 两项经大小写规范化验证。下一步须在独立获准的 macOS dyld child 里收集同一 m3 的 extended info 和 csops 再比较；下节已完成对照。

### 第六次单独授权：同一 dyld 诊断版的 m3 fault 对照揭示不同的 VM object chain

用户单独批准**仅复用**第五次高区诊断 dyld（签名 CDHash `5a1d78f23b16c1b3e3dceacf5b043c6228ab9022`，运行 SHA `3f2048c9303464f673c8df2a661748a642175415c95ace31b3ff7f91a6937062`），把 runner 改为已 TC 核验的 v5。备份 `/var/mobile/dyld_cli_m3cmp_pre.bin` 与实验前 dyld 双 SHA 均为原值，remote EXIT trap/独立复核恢复相同 SHA。一个限时原版 `/bin/echo HI` child，原始 stderr `/var/mobile/dyld_cli_m3cmp_trace.raw`（870B）：
```
[*] spawned pid=6553 (suspended)
[*] jbctl rc=0
[*] task_for_pid kr=0 port=7683
[*] child STOPPED sig=0
[exc] type=1 code0=0xa code1=0x1ee188000 thr=3843 tsk=3587
[exc] csops pid=6553 rc=0 flags=0x26803b0d errno=0
[vmext] 0x1ee188000..0x1ee1ac000 tag=0 resident=0 external=1 shadow=3 mode=1 ref=6
[exc] pc=0x104f1b9f8 lr=0xb546000104f1b9ec
[vm] 0x1ee188000..0x1ee1ac000 prot=3/3 off=0x0 shared=0
[*] child SIGNALED 10
EXPERIMENT_LAUNCHER_RC=0
RESTORED_SHA256=9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1
```

**和同地址/同文件/同 offset/同长度的原生成功 task 对照**：native `flags=0x3680380d, m3 resident=1 external=1 shadow=1 ref=5, basic offset=0x6c188000`；macOS exec 后 `flags=0x26803b0d, m3 resident=0 external=1 shadow=3 ref=6, basic offset=0`。XNU `osfmk/kern/cs_blobs.h:44-45,71` 确认差额恰好是 native 有 `CS_DEBUGGED=0x10000000`，macOS task 则有 `CS_HARD|CS_KILL=0x300`；`jbctl proc_set_debugged` 作用于 suspended **exec 前**的 `chroot`，跨 macOS exec 没保留在最终 task 的 csflags（runtime-confirmed，不能再把 launcher 的 `jbctl rc=0` 当 target DEBUGGED）。但**这不等于签名必定根因**：开源 `vm_fault.c:2775,2778-2794` 中显式 `cs_invalid_page` 拒绝设 `KERN_CODESIGN_ERROR=50`，而此处实际 `KERN_MEMORY_ERROR=10`。`vm_region_basic_info_64.offset` 来自 VM entry/object offset；shadow 从 1→3 和 offset 从 `0x6c188000`→0 表示映射链不同，但不直接证明 file offset 被改为 0、vnode 不同或零页替代。继续沿 `vm_fault` 的 `VMP_ERROR`/shadow-severed/pager 回报分支定位 10，先查可用的只读 kernel/triage 数据和实际 vnode/UBC 关系，不能仅凭 csflags 去 NOP 签名策略。macOS cached libSystem CLI 尚未成功。

### 2026-10-02 KERN_CODESIGN_ERROR=50 收窄（私有缓存路径，执行页验证）

**背景修正**：之前 `d2400068` 实为 `eor x8,x3,#1`（不是 `movz x8,#3,lsl32`）；正确编码 `d2c00068`。修正后私有路径 15 个 mmap 全部成功（`addr = 0x180000000 + fileoff`），`map_with_linking_np`(syscall 550) 返回成功。崩溃推进到缓存内 `pc=0x18047dc9c`，`code0=0x32=50=KERN_CODESIGN_ERROR`，`pagein_error=0`：数据可读、执行页入被拒。syscall 536 因 iOS arm64 shared region 仅 4GB < macOS 缓存 ~5GB 结构性排除。

**私有路径形态**（源确证）：`map_with_linking_np` 在 `bsd/vm/vm_unix.c:3217` 显式拒绝 `VM_PROT_EXECUTE` region，故 550 只用于需 fixup 的 DATA region；TEXT/exec 页走普通 `mmap`(MAP_PRIVATE|MAP_FIXED) → vnode pager + COW shadow（vmext 示 `external=1 shadow=1`，符合）。

**症状三分支判定**（`osfmk/vm/vm_fault.c:2693/2718` + `bsd/kern/ubc_subr.c:5274`）：exec 被拒只可能因 (a) 页 tainted 或 (b) `!VMP_CS_VALIDATED`。数据读成功排除 tainted（taint 对读也拒）。故页从未 validated：`page_obj->code_signed==FALSE`（`vm_fault_cs_need_validation` 在 2548 直接跳过）或 `cs_validate_hash` 未找到覆盖 blob（`found_hash==FALSE` → `validated=0, tainted=0`，与症状完全一致）。

**已排除**：缓存文件 16K 页 hash 与内嵌 CD 逐槽自洽（SHA-256, pageSize=14）；dyld `fcntl(F_ADDFILESIGS_RETURN)`（`preflightCacheFile` 内 `0x35d64`，经 `preflightMainCacheFile`→私有路径共用）已测返回 0 ⇒ `ubc_cs_blob_add` 成功 + `memory_object_signed(uip->ui_control,TRUE)` 成功（`ubc_subr.c:4473`，否则 fcntl 返回 ENOENT）⇒ vnode VM object 已置 `code_signed`、blob 已挂。注意 `registerSignature`(0x30a2c) 只服务普通 Mach-O/JIT loader，与缓存路径无关。

**待决（二选一）**：(1) exec fault 时 `m` 属主对象非 code_signed（如实际进入 shadow/copy 对象，或 `cs_validate_page` 内 `vnode_pager_lookup_vnode`/`mo_offset` 落偏）；(2) blob `csb_base/start/end` 或 `csb_mem_kaddr` 实际值不覆盖 `page_offset≈0x47c000`。

**已备好的决定性实验**（设备恢复即用）：`RUN_DBG_HOLD=30 run_dbg_hold_v2` 把孩子冻结在 codesign 异常 → `misc/csprobe.py <pid>` 用 libjailbreak KRW 读：proc→fd→vnode(`+0x78`)→ubc_info(`ui_control`@+0x08 即 vm_object、`cs_blobs`@+0x50、`ui_flags`@+0x28)→ `vm_object+0xac`bit8=`code_signed`、`+0x7c`bit16=`internal`、`+0x50`=pager；blob `base/start/end`@+0x28/+0x30/+0x38、`mem_kaddr`@+0x50、`cd`@+0xa0、`csb_pmap_cs_entry`@~+0xe0。slide 由 `libjailbreak.jbinfo_get_serialized()`+`xpc_dictionary_get_uint64("kernelConstant.slide")` 取得（替代硬编码 KSLIDE）。sysctl 变量 IDB 地址：`cs_debug`@`0xfffffe000aa54188`、`cs_debug_unsigned_exec_failures`@`…190`、`cs_debug_unsigned_mmap_failures`@`…194`（运行时+slide；但源码未见递增点，仅辅助）。

**PMAP_CS 注记**：`vm_map_entry.pmap_cs_associated` 在本 xnu 源中只见继承（submap copy），无置位点——关联逻辑在 PPL（dispatch table 0x78e9cf8/0x78e9d00 侧）。私有 mmap 的 entry 该位为 FALSE ⇒ exec 拒绝判定大概率在 xnu `vm_fault_cs_*` 层而非 PPL。

### 2026-10-02 深夜 RE 收紧：exec 拒绝路径收敛到 pmap_cs/PPL + 空 SR submap 残留模型

**本轮新增的 RE/源码证据（全部本地完成，设备离线中）：**

1. `vm_map_entry` 布局核实（RE-confirmed via `libkern/tree.h:354`）：xnu 的 `RB_ENTRY` 仅 3 指针（24B，color 编码在 parent 低位）⇒ entry 布局：links@0x00(32B)、store@0x20(24B)、union(object/submap)@0x38(8B)、flags1(alias:12|vme_offset:52)@0x40、flags2(32 bools)@0x48。⇒ csprobe2 读的 `+0x48` **确实是 flags2**，`0x210abac0` 的 bit24=`pmap_cs_associated`=1 **成立**（此前"私有 mmap 该位恒 FALSE"的推断作废——那是非 PPL build 的 assert，本机 PPL 使能）。

2. `pmap_cs_associated` 置位点在公开源码全部缺席（`vm_map_entry_copy_pmap_cs_assoc` @vm_map.c:438 是空壳，`CONFIG_PMAP_CS` 裁掉真实现）⇒ 该位只能由 PPL build 的闭源代码或 submap clip 继承（vm_map.c:14066 fault-COW 路径 / :17321 remap 路径）。

3. **空 SR submap 残留模型（THEORY，证据链闭合、待设备验证）**：
   - echo exec → `vm_shared_region_enter`(fsroot=chroot) 创建**空 SR** 并把 4GB submap 嵌进 map(0x180000000-0x280000000)，submap entry 带 `vme_permanent`(`vmkf_permanent`,vm_shared_region.c:~2280)。
   - 状态文档已实测：孩子内 `check_np=12`(ENOMEM=SR 存在但空)。
   - dyld `deallocateExistingSharedCache`(0x3420c)：`check_np(&base)` 返 12≠0 → `CBNZ W0 @0x34228` **跳过** `check_np(NULL)` → submap 残留。
   - dyld 私有 `mmap(MAP_FIXED|MAP_PRIVATE)`(SharedCacheRuntime.cpp:975)覆盖 submap 区间 → 用户态 overwrite **不能删 permanent submap entry**(vm_map.c:8144→8167 需 `VM_MAP_REMOVE_IMMUTABLE`，用户 mmap 无此 flag；递归删 submap 内部成功后父 entry 才被处理）→ clip/复用路径把 `permanent+pmap_cs_associated+no_copy_on_read+needs_copy` 带进新 file entry（与实测 flags `0x210abac0` 逐项吻合）。
   - exec fault → `fault_info.pmap_cs_associated=1`(vm_map.c:14298)→ PPL 层按 VA 查 CD 关联 → 我们的私有映射从未经 `pmap_cs_associate` 建立关联（PPL dispatch index 37–54 隐藏块）→ 拒绝 → `KERN_CODESIGN_ERROR`。
   - VM 层页校验本身完好（resident 页 validated=0xf/tainted=0/xpmapped=1；xpmapped 是 `pmap_enter` 前乐观置位，不作成功证据）。

4. `permanent` entry **不可删**（vm_map.c:8630：只能降为 PROT_NONE 留存）——唯有 `vmkf_overwrite_immutable`(=`VM_MAP_REMOVE_IMMUTABLE`，由 `vm_shared_region_remove` 使用，vm_map.c:8150 放行）能真正删除。**这解释了为什么必须用 `check_np(NULL)` 路径而非普通 mmap 覆盖。**

5. dyld 补丁点（RE-confirmed，IDB@Instance2 `deallocateExistingSharedCache`）：
   ```
   0x34224: BL __shared_region_check_np   ; check(&base)
   0x34228: CBNZ W0, 0x34234              ; 空SR(12)/无SR(22) → 跳过 detach ← 病灶
   0x3422c: MOV X0,#0 / BL check_np       ; teardown
   0x34234: ret
   ```
   **现成 patch key 已存在**：`deallocnp`(0x34228 CBNZ→NOP，无条件 detach——注意 EINVAL(22) 也会 teardown，`shared_region==NULL` 时内核侧 `if (sr!=NULL)` 跳过，安全）与 `emptysr_e/c`（仅 ENOMEM(12) 时 teardown，更保守）。**但历史测试在 mmap 尚未通的阶段做的，结论不可信，须在现配置下重测。**

**设备恢复后的决定性实验（按序）**：
1. `post_reboot_fmt13.sh` 步骤 1-4（TC + fmt13 补丁）。
2. **实验 A**：部署 `DEFAULT + deallocnp` 的 dyld 变体 → `run_dbg_hold_v2` 跑 `/bin/echo HI`。若 exec 拒绝消失 → submap 残留模型锤实 + 可能直接达标。
3. **实验 B**（若 A 仍 `KERN_CODESIGN_ERROR`)：冻结孩子 → csprobe2 **全量走 entry 表**：每条 entry 打 `vme_start/end/is_sub_map(union+0x38 bit1)/object(flags1@0x40 的高位)/flags2@0x48` 全部位域 → 看哪些范围带 pmap_cs 位、是否还有 is_sub_map 残留；读 `map->shared_region` 指针与 SR 的 `sr_first_mapping`。
4. **实验 C**（若 A 出现新错，如又回到 m3 `KERN_MEMORY_ERROR=10`):m3 entry `shadow=3` 的来源排查（`needs_copy` 继承 vs 正常 COW)；原生对照（shadow=1 resident=1 正常读）已建。
5. `csprobe2.py` 修正：补 flags2 全位域解码（bit24 pmap_cs、bit19 permanent、bit29 no_copy_on_read、bit10 used_for_tpro、bit6 needs_copy）+ union+0x38 is_sub_map + entry 遍历打印所有带 flag 的 entry 范围。

**对照参考**：iOS 原生进程 mmap 同一缓存页（RW，长度 0x24000,fileoff 0x6c188000）成功读取 ⇒ 文件/页内容/vnode 均正常；差异只在 macOS task 的 map 状态。

### 2026-10-02 用户确认已重启：冷启动基线检查被 SSH 认证阻塞

用户本轮明确表示 iPad 已重启、可以继续；此前仅凭 SSH 不通推断设备已经重启的说法不成立。重新越狱状态、系统身份及启动时间仍待设备只读输出确认。

本轮未部署、未恢复 trustcache、未启动 chroot、未写内核。优先保留冷启动基线，不执行上一节的 `post_reboot_fmt13.sh` 或 `DEFAULT + deallocnp` 计划；后者包含多项历史诊断，不能直接作为受控单变量修复实验。

本地审查 `misc/post_reboot_tc_probe.sh`：管道会混淆查询失败与无匹配，启动输出仅 `tail -30`，且搜索的启动路径不包含当前约定 `/var/jb/usr/macOS/bin/macos_gui.sh`。暂不执行该脚本。后续先保存 `jbctl trustcache info` 完整输出及返回码，再逐个检查完整 CDHash；查询成功才允许报告缺失。正式启动对照前核验设备实际脚本与 rootfs。

串行只读尝试（连接目标使用占位符记录，不记录凭据）：

```sh
ssh -o BatchMode=yes -o ConnectTimeout=5 -o ConnectionAttempts=1 \
  -p "$MACWS_DEVICE_PORT" "$MACWS_DEVICE" \
  'id; /var/jb/usr/sbin/sysctl hw.machine kern.osversion kern.boottime; /var/jb/basebin/jbctl trustcache info; rc=$?; printf "TRUSTCACHE_QUERY_RC=%s\n" "$rc"; exit "$rc"'
```

逐字错误正文（SSH 目标前缀省略）：

```text
Permission denied (publickey,password,keyboard-interactive).
Exit code: 255
```

**runtime-confirmed（本轮 SSH 输出）**：SSH 认证失败；远端命令未执行，没有本轮 trustcache 输出。不能据此推断哈希缺失、设备离线或 dyld 根因。按认证纪律停止重试，等待用户确认认证方式；冷启动 trustcache 基线任务仍未完成。

### 2026-10-02 冷启动基线成功：24G90 缓存双哈希缺失，实际部署身份有漂移

用户确认重新越狱和认证方式，授权无人值守继续。认证凭据仅通过临时进程环境传递，不记录到仓库。使用上节相同远端命令，SSH 改为 `sshpass -e ssh -o ConnectTimeout=5 -o ConnectionAttempts=1 -o PubkeyAuthentication=no -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 -p "$MACWS_DEVICE_PORT" "$MACWS_DEVICE"`。

**runtime-confirmed via `docs/evidence/cold-boot-trustcache-20261002.raw`**：首次完整只读查询返回码 0，原始输出已完整保存（69 项，无截断）：

```text
uid=0(root) gid=0(wheel) groups=0(wheel)
hw.machine: iPad13,11
kern.osversion: 20D47
kern.boottime: { sec = 1790941117, usec = 852384 } Fri Oct  2 19:38:37 2026
Jailbreak Trustcache 0 <UUID: 61806465955E435697D48B012193F222> (length: 69)
TRUSTCACHE_QUERY_RC=0
```

完整输出未包含 `2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e` 或 `8c7ba7e588b0edd43f7334e2de11688cd4732192`。这证明本轮查询时两个缓存哈希缺失，不证明旧 codesign fault 的根因。

随后通过 iOS Python `os.path.exists`/文件读取/`hashlib.sha256`/`plistlib.loads` 核验已知路径（未启动 chroot）：

```text
PATH /var/mnt/rootfs/System/Library/CoreServices/SystemVersion.plist EXISTS True
SIZE 603 SHA256 9af8c8d66fb9e5f022d93f46481c8834b787c2ec61e96da6d4a33965640ce2b2
VERSION {'BuildID': 'A9352A4E-7AC8-11F0-9D2D-B731BA1D3D59', 'ProductBuildVersion': '24G90', 'ProductCopyright': '1983-2025 Apple Inc.', 'ProductName': 'macOS', 'ProductUserVisibleVersion': '15.6.1', 'ProductVersion': '15.6.1', 'iOSSupportVersion': '18.6'}
PATH /var/jb/usr/macOS/bin/macos_gui.sh EXISTS True
SIZE 264670 SHA256 af9b213980fe679fd02f43ec82e72906ec3ac77dd9d8ad3f923c2a8a2492021b
PATH /var/mnt/rootfs/usr/macOS/bin/macos_gui.sh EXISTS False
PATH /var/jb/usr/macOS/bin/macws_boot_trust.py EXISTS True
SIZE 19206 SHA256 2c727c302a55b15470f9bc1cf4ec8c45e87091e8b448974d866e8ca1dd955135
PATH /var/mnt/rootfs/usr/lib/dyld EXISTS True
SIZE 1239632 SHA256 b8fdbc1b7cfd15cccbcd110c0c3cb1ff91d135d6664b84770d42df843381b91e
PATH /var/mobile/run_dbg_hold_v2 EXISTS True
SIZE 53072 SHA256 5af5df15f95d5b1e80809ad9c89e512b2cb8cfc05469e03717f937537b873ff5
```

设备实际 `macos_gui.sh:1675-1681` 读取的 hash 参数仍是 Ventura 对：

```text
1675:     /var/jb/usr/bin/python3 "$boot_trust_helper" \
1676:         --manifest "$boot_trust_cache/hashes.json" \
1677:         --resource-index "$boot_trust_cache/resources.sqlite" \
1678:         --thermal-tool /var/jb/usr/macOS/bin/macwsthermal \
1679:         --hash b5da39409492ac85e5a8e8ab618fe77e2d7a2980 \
1680:         --hash bbb765988e2677b98d47a549d612fa0d4af25f69 \
1681:         "$@" || return 1
```

**runtime-confirmed（文件读取）**：设备脚本行号与本地不同，dyld SHA 也不等于历史恢复 SHA；不直接替换成旧实验变体。第一轮进程查询用错路径，返回 `FileNotFoundError: [Errno 2] No such file or directory: '/var/jb/usr/bin/ps'`，脚本 exit 1；仅中断进程查询，不撤销上述已成功读取数据。改用 `/bin/ps -axo pid,comm` 后：

```text
PS_PATH /bin/ps
PROCESS_QUERY_RC 0
  372 /var/jb/usr/macOS/bin/macwshostd
```

筛选目标为 WindowServer/launchdchrootexec/autosignd/macwshostd/run_dbg/restore_env；未观察到前三者或 runner，不据此断言历史上从未启动过 chroot。

设备脚本 `5472-5477` 提供 `trust` 子命令，调用与 production 相同的 `restore_cold_boot_trust`，不启动/停止 GUI。完整 production 路径含删除旧诊断文件、修改 launch jobs 等额外动作，不适合作为无人值守的最小对照。接下来改用正式 `trust` 子命令隔离共同的恢复函数，保留完整返回码和日志；**这不是 production 全流程验收**。若热状态或 helper 阻塞，不能把未完成的恢复误判成哈希分支运行见证。

### 2026-10-02 共同 trust 恢复路径运行见证：成功恢复错误版本的缓存对

SSH 认证参数沿用上节（临时环境凭据）；在 iOS Python 中运行：

```python
cmd = ['/var/jb/usr/bin/bash', '/var/jb/usr/macOS/bin/macos_gui.sh', 'trust']
r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                   text=True, timeout=180)
print(r.stdout, end='')
print('TRUST_RESTORE_RC', r.returncode)
r = subprocess.run(['/var/jb/basebin/jbctl', 'trustcache', 'info'],
                   text=True, capture_output=True, timeout=15)
hashes = set(re.findall(r'\b[0-9a-fA-F]{40}\b', r.stdout.lower()))
```

对每个完整 CDHash 查询 membership（仅 query rc=0 时做真假判定），逐字输出：

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

**runtime-confirmed**：production 与 trust 共用的恢复函数运行成功，并报告 ready，但仍未恢复本 rootfs 的两个 24G90 缓存哈希，恢复的是 Ventura 对。现在满足修改 build→hash 分支的设备见证前提。**未证明**原版 `/bin/echo` 可执行、GUI 可启动或旧 codesign/m3 fault 根因已解决；本轮未运行完整 production、未切换 dyld、未写内核。

最小修复：照 postinst 读取 `SystemVersion.plist` 的 `ProductBuildVersion`，保留 `22F82|22F66|空值` 的历史 Ventura 行为，24G90 选择其原有完整 CDHash 对，未知非空 build 明确失败，不报 trust ready。先添加回归断言及 shell 分支执行测试，再改启动脚本。

### 2026-10-02 跨供应商交接（在 trust 修复动手前暂停）

用户因 Devin 额度请求完整交接，新增 `docs/porting/HANDOVER-DEVIN-TO-EXTERNAL-GPT-2026-10-02.md`（含可复制启动 prompt、实际身份/证据/下一步/工具约束）。**本次仅保存证据和写交接文档，没有修改启动脚本、测试或设备。**

已核验当前聊天 ATIF 文件 `/Users/ciscohe/.local/share/devin/cli/transcripts/coffee-soap.json`，session `coffee-soap`；前序历史 `/Users/ciscohe/.local/share/devin/cli/summaries/history_83dd6af563c64fa6.md` 存在。原始聊天可能含凭据和工具输出截断，不提交仓库。新文档可在同机供另一客户端读取，不等同导入模型内部状态。

IDA MCP Instance1 本轮 `server_health` 输出确认 input_path=`/Users/ciscohe/Desktop/macPad/analysis/kc_raw_16.3_T8112.bin`、imagebase=`0xfffffe0007004000`、status=ok、Hex-Rays ready。其余实例本轮未重核绑定，接棒需再 health。

交接审查特别标记 `misc/fmt13_patch.py` 为**未验收且不能直接执行**：顶层自动进入写流程，没有真正只读 verify 模式；runtime地址只用 static+slide、CAVE字节序需独立IDA审计、覆盖原2/3/6格式handler影响全局、undo只恢复dispatch而不恢复完整handler。这里只记录源实现风险，不声称运行证明了哪项具体bug。接棒先完成已runtime确认的trust恢复修复，再按单变量推进CLI，不盲目执行旧“设备回来后一把梭”计划。

交接本地验证：`git diff --check` 通过；raw trustcache文件69项。`python3 -m unittest misc.test_restore_boot_contract misc.test_agents_memory_ledger` 共12项，11通过、1失败（exit 1）：`test_package_declares_ios_tools_used_during_postinstall`，实际 `control` 的 `Depends: python3, ldid` 缺测试要求的 `plutil/odcctools/gawk`。`git show HEAD:control` 同样只有这两项，且本轮未改源码/测试；基线不一致原样保留，交接文档§10.1记录完整失败摘要，不能误当成24G90补丁回归。

### 2026-10-02 接棒后 trust build 选择修复（本地，设备尚未部署）

**runtime-confirmed 基线**仍为本文件前一条：rootfs `ProductBuildVersion=24G90`，正式 `macos_gui.sh trust` 返回 0 但只把 Ventura 双 cache CDHash 置入 live trustcache；24G90 双 hash 仍缺失。该证据来自 `docs/evidence/cold-boot-trustcache-20261002.raw` 及交接逐字运行输出。本条没有启动 chroot/WindowServer、没有替换 dyld、没有内核或 PAC 写入。

先在 `misc/test_restore_boot_contract.py` 增加了真实 shell 函数回归。测试通过临时 rootfs plist 和记录 helper 执行参数，实际执行 `restore_cold_boot_trust`，覆盖 `24G90`、`22F82`、`22F66`、空 build；确认对应完整 hash pair、manifest/resource-index 及既有 Mach-O 路径参数保留。未知非空 build `25A100` 在 helper 调用前返回非零，helper 无调用。新增回归两项均通过；完整该模块 10 项中 9 项通过，既存 `test_package_declares_ios_tools_used_during_postinst` 仍失败（`control` 只有 `python3, ldid`，交接基线已记录，未借本修复范围擅改依赖）。`bash -n layout/usr/macOS/bin/macos_gui.sh misc/device_pipeline.sh` 与 `git diff --check` 通过。

实现位于 `layout/usr/macOS/bin/macos_gui.sh::restore_cold_boot_trust`：从 `$ROOTFS/System/Library/CoreServices/SystemVersion.plist` 读取 `ProductBuildVersion`；`24G90` 追加 `2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e` 与 `8c7ba7e588b0edd43f7334e2de11688cd4732192`；`22F82|22F66|空值` 保留 Ventura pair；未知非空值记录错误并在 helper 前 `return 1`。hash 以 `set -- "$@"` 追加，保留原扫描路径与 helper 参数。该条是源代码/本地回归事实，不是设备验收；设备部署与两 hash live membership 复核待下一条记录。

设备部署状态：本环境当前没有 `MACWS_DEVICE`、`MACWS_DEVICE_PORT` 或 `MACWS_SUDO_PASSWORD` 环境变量（仅检查变量名是否存在，未读取或打印凭据），因此尚未调用 `misc/device_pipeline.sh --component runtime`，没有虚报 deployment 成功。

### 2026-10-02 设备部署后 trust 验收被 thermal gate 暂停

设备只读身份再次确认：`uid=0(root)`、`hw.machine=iPad13,11`、`kern.osversion=20D47`，`jbctl trustcache info` 查询 rc=0。设备 rootfs 工程目录原不存在，且远端没有 `rsync` 服务端；pipeline 的同步阶段分别得到 SSH agent `Too many authentication failures` 和远端 `rsync` code 127。未改 pipeline。改用明确 runtime 文件集合的临时 tar staging，逐文件 SHA-256 与本地一致后，按 runtime 部署动作安装脚本/plist；目标 `macos_gui.sh` 等关键脚本 `cmp` 通过，输出 `RUNTIME_INSTALL_VERIFIED`。未部署 dyld、内核、GUI。

正式运行 `/var/jb/usr/bin/bash /var/jb/usr/macOS/bin/macos_gui.sh trust` 的逐字结果：

```text
[macos_gui] THERMAL-PAUSE: application trust checkpoint preserved; thermal-state=serious raw=2 low-power=no battery-temp-centic=3689 virtual-temp-centic=3689 effective-temp-centic=3689 uptime=9751.785
TRUST_RESTORE_RC=1
```

随后只读调用 `macwsthermal` 返回 `thermal-state=serious raw=2 ... battery-temp-centic=3689 ... THERMAL_RC=3`；60 秒后仍为 `thermal-state=serious raw=2 ... battery-temp-centic=3679 ... THERMAL_RC=3`。**runtime-confirmed**：trust helper 尚未执行，不能报告任何 cache hash 已恢复；这不是 24G90 分支失败，也不是设备离线。遵守 gate，未设置绕过变量、未修改 thermal 状态、未启动 chroot/WindowServer。

追加轮询：在前次记录后等待 120 秒，只读 `macwsthermal` 仍返回 `thermal-state=serious raw=2 low-power=no battery-temp-centic=3689 virtual-temp-centic=3689 effective-temp-centic=3689 uptime=10050.086`，`THERMAL_RC=3`。未调用底层 trust helper 绕过 gate；24G90 live membership 仍待 nominal 窗口验收。

### 2026-10-02 后续设备轮询：thermal gate 仍阻塞 trust

再次只读核验：`macwsthermal` 输出 `thermal-state=serious raw=2 low-power=no battery-temp-centic=3679 virtual-temp-centic=3679 effective-temp-centic=3679 uptime=10149.592`，`THERMAL_RC=3`；`jbctl trustcache info` 查询 rc=0，完整 24G90 两枚 hash 仍未匹配。进程核验显示 `/var/root/VirtualMac/payload/VirtualMachine.xpc/Contents/MacOS/com.apple.Virtualization.VirtualMachine`（PID 1712，mobile，运行约 01:29:43）CPU 约 158%，其父进程为 VirtualMac PID 1711。高负载与 thermal serious 的因果仍标为 THEORY；未停止该用户进程、未绕过 gate、未运行底层 trust helper。

### 2026-10-02 thermal blocker persists under VirtualMac load

新一轮只读核验：`macwsthermal` 为 `thermal-state=serious raw=2 low-power=no battery-temp-centic=3679 virtual-temp-centic=3679 effective-temp-centic=3679 uptime=10253.799`，`THERMAL_RC=3`；trustcache query rc=0，24G90 两枚完整 hash 仍无匹配。`VirtualMachine.xpc` PID 1712 CPU 约 222.9%，父进程 VirtualMac PID 1711 约 5.7%。连续有界轮询未恢复 nominal；未绕过 gate、未停止用户 VirtualMac、未运行底层 helper。后续 trust/echo 需要 thermal 外部状态先改变。

### 2026-10-02 重启/重新越狱后冷启动复核

用户报告前一轮 VirtualMac 卡死后重启 iPad、重新越狱并再次启动 VirtualMac。本轮只读核验：`hw.machine=iPad13,11`、`kern.osversion=20D47`、`kern.boottime=Fri Oct 2 22:44:48 2026`；rootfs plist SHA `9af8c8d66fb9e5f022d93f46481c8834b787c2ec61e96da6d4a33965640ce2b2`，`ProductBuildVersion=24G90`；部署后的 `macos_gui.sh` SHA `61e7143f0c314f4ca3a4ad4b15776a06813697290d498be54421a298253773fa`，`macws_boot_trust.py` SHA `2c727c302a55b15470f9bc1cf4ec8c45e87091e8b448974d866e8ca1dd955135`。

冷启动 `jbctl trustcache info` 查询 rc=0，24G90 两枚完整 hash 均 `PRESENT False`。直接执行 thermal 工具（不是 bash 解释）返回：`thermal-state=serious raw=2 low-power=no battery-temp-centic=3629 virtual-temp-centic=3629 effective-temp-centic=3629 uptime=390.449`，`THERMAL_RC=3`。等待 60 秒后仍为 serious，VirtualMachine.xpc PID 975 CPU 343.9%，父 VirtualMac PID 969 CPU 2.5%。正式 `/var/jb/usr/bin/bash /var/jb/usr/macOS/bin/macos_gui.sh trust` 逐字结果：

```text
[macos_gui] THERMAL-PAUSE: application trust checkpoint preserved; thermal-state=serious raw=2 low-power=no battery-temp-centic=3639 virtual-temp-centic=3639 effective-temp-centic=3639 uptime=511.115
TRUST_RESTORE_RC=1
```

**runtime-confirmed**：重启已清除上一 boot 的动态 trustcache，24G90 hash 重新缺失；部署脚本仍为目标版本；正式 trust 尚未进入 build/hash helper。未停止用户 VirtualMac，未绕过 thermal gate，未启动 chroot/WindowServer、未运行 echo。

### 2026-10-02 continued post-reboot thermal poll

重启后约 23 分钟再次只读核验：`macwsthermal` 仍为 `thermal-state=serious raw=2 low-power=no battery-temp-centic=3689 virtual-temp-centic=3689 effective-temp-centic=3689 uptime=1386.221`，`THERMAL_RC=3`；VirtualMachine.xpc PID 975 CPU 162.3%，VirtualMac PID 969 CPU 5.0%；trustcache query rc=0，24G90 两枚 hash 均 `PRESENT False`。源码复核显示 `restore_cold_boot_trust` 的 `application_trust_thermally_safe` 仅接受 `nominal`，无可用 bypass；未修改 gate、未停止 VM、未执行 trust helper。

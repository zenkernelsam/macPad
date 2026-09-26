# HANDOVER — macOS 15.6.1 dyld shared cache on iPadOS 16.3 (2026-09-26)

**写给一个没有任何记忆的 AI。** 目标：让另一个 agent 不重新推导就能继续干活。
本文档 = 全部已知事实（每条都带证据等级）+ 全部 patch + 完整复现步骤 + 当前未决问题。
同样内容的实时状态在 `docs/porting/dyld-15.6.1-state.md`（更长、按时间序）。

---

## 0. 一句话现状

macOS 15.6.1 dyld 能在 iPadOS 16.3 chroot 里执行到 `start()` 深处的应用入口调用点
（`0x6b94 blraaz x8`），syscall 536 (`shared_region_map_and_slide_2_np`) 本身已能在
空 shared region 上返回 0 并把主缓存 8 条映射真实落地。**未决的三件事**：
(1) 每次 exec 的 dynregion 生命周期；(2) `.01` 子缓存尾部 ~0.7GB 超出 iOS 4GB
shared region 的混合映射；(3) 若干二进制仍被 exec 阶段 SIGKILL(137) 的分类归因。

---

## 1. 环境 / 访问

| 项 | 值 |
|---|---|
| 设备 | M1 iPad Pro `iPad13,11`（SoC `0x8103`，代号 T8112 = M2 系） |
| iPadOS | 16.3 (20D47)，XNU `xnu-8792.82.2`（源码参考用 8792.81.2 亦可） |
| 越狱 | Dopamine **rootless**，`JBROOT=/var/jb` |
| SSH | `sshpass -p cisco ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no -p 2222 root@192.168.64.1` |
| macOS rootfs | 设备上 `/var/mnt/rootfs` = **数据卷上的目录树**（不是独立卷），内含 bindfs 子挂载 `mc/`、`ios/` |
| 缓存真实路径（host 视角） | `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e[.01]`；`/var/mnt/rootfs/System/Library/dyld/…` 是指向它的符号链接 |
| 仓库 | `/Users/ciscohe/Desktop/macPad` |
| 本地工作目录 | `/tmp/dyldwork`（会被清，重要产物放 `analysis/`） |

**持久化的分析文件（`analysis/` 目录——不要再放 /tmp）：**

| 文件 | 说明 |
|---|---|
| `analysis/dyld_15.6.1_arm64e` | 15.6.1 fat dyld（1257136 B） |
| `analysis/dyld_15.6.1_arm64e_thin` | arm64e thin slice（1240752 B），**所有 patch 偏移都以此为准** |
| `analysis/dyld_15.6.1_arm64e_thin.i64` (+.id0/.id1/.nam/.til) | IDA IDB，当前挂在 **ida-pro-mcp-Instance1** |
| `analysis/kernelcache_16.3_T8112.img4` | 设备原始 IMG4（21845088 B） |
| `analysis/kc_raw_16.3_T8112.bin` | 解压后的内核 Mach-O（**80052224 B**, arm64e），imagebase `0xfffffe0007004000`；应加载到 **ida-pro-mcp-Instance2** |

重新提取内核的命令（若需要重做）：
```bash
scp -P 2222 root@IP:/private/preboot/*/System/Library/Caches/com.apple.kernelcaches/kernelcache kc.img4
python3:  import pyimg4; im=pyimg4.IMG4(open('kc.img4','rb').read()); p=im.im4p.payload; p.decompress(); write(p.data)
# payload 是 bvx2/LZVN，解压后 cffaedfe Mach-O
```

## 2. 签名 / trustcache 运维（重启后必须重做）

```bash
JB=/var/jb/basebin/jbctl                      # Dopamine 自带
H=$(ldid -arch arm64e -h <file> | grep CDHash= | cut -c8-)   # 取 cdhash
$JB trustcache info | tr '[:upper:]' '[:lower:]' | grep -qi "$H" || $JB trustcache add "$H"
```

- `ldid -S<ent.plist> -M <file>` 重签（项目 entitlements 在 `/var/jb/usr/macOS/bin/entitlements.plist`，含 `com.apple.private.security.no-sandbox`——**dyld 必须带它**，否则 syscall 536 被 sandbox 挡 errno=40）。
- trustcache 每次重启清空；vnode 上的 cs_blob 也在 panic/reboot 后丢失。
- **重启后完整恢复清单**：①重签+重加所有测试二进制 cdhash；②重跑 cachereg holder（下述）；③确认 rootfs 内 dyld 是预期构建（hash 对得上）。

## 3. 缓存签名注册（syscall 536 前置条件，runtime-confirmed）

内核要求提交文件的 vnode 上挂有 cs_blob。办法：

```c
fcntl(fd, F_ADDFILESIGS=61, &user_fsignatures{fs_file_start:0,
        fs_blob_start:hdr[0x28](cs offset), fs_blob_size:hdr[0x30](cs size)})
```

工具已写好：**`/tmp/dyldwork/cachereg_ios`**（源码 `cachereg.c`），host 侧跑
（`mac_vnode_check_signature` 只允许 host 上下文成功），`pause()` 常驻保活 fd/vnode。
设备上已部署 `/var/mobile/cachereg`。注意它要一直活着——进程死了 blob 可能回收。

两个缓存的 cdhash（已验证加入 trustcache）：
`2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e`（主）、`8c7ba7e588b0edd43f7334e2de11688cd4732192`（.01）。

## 4. syscall 536 的完整门禁链（xnu-8792.81.2 `bsd/vm/vm_unix.c` `shared_region_map_and_slide_setup` + IDA 内核反编译双确认）

按源码顺序：

| # | 检查 | 失败返回 | 我们的状态 |
|---|---|---|---|
| 1 | `files_count==0` | EINVAL | 提交≥1 |
| 2 | `task->shared_region==NULL` | EINVAL | exec 时 vm_map_exec 无条件建——**chroot 进程有 region（check_np=12=存在但空）** |
| 3 | `sr_vnode` ≠ 进程 chroot root | EPERM | 文件必须在 region root 树下 → 用 `/var/mnt/rootfs` 内的数据卷路径 |
| 4 | fd=-1 伪条目：count≥2 且 sms_address/size 页对齐 | EINVAL | dynregion 条目遵守即可 |
| 5 | fd→vnode / FREAD / VREG | 各自 errno | OK |
| 6 | `mac_file_check_mmap`（sandbox） | 40 | **no-sandbox entitlement 已过** |
| 7 | `uid!=0` | EPERM | 全程 root |
| 8 | `v_mount` ∈ {根卷, preboot cryptex 卷} | EPERM | 数据卷文件过（见§6矩阵） |
| 9 | `scdir_enforce` 父目录校验 | EPERM | `System/Volumes/Preboot/Cryptexes/OS/...` 路径过 |
| 10 | `ubc_getobject` 空 | EINVAL | OK |
| 11 | **`ubc_cs_is_range_codesigned(vp,off,size)`** | EINVAL | **需 F_ADDFILESIGS——已通过 cachereg 解决** |

内核 IDA 要点（Instance2 可复核）：sysent @ `0xfffffe0007999680`（24B 表项，
+0x10=sy_call）；[536]=`0xfffffe0008459134`；`sub_8459570`=setup；`sub_8063590`=
task+0x3E8 shared_region getter；`sub_8063720`=vm_shared_region_enter（exec 无条件建）。
详见 `kernel-syscall536-re-handover.md` / `kernel-syscall536-finding.md`。

## 5. Shared region 结构墙（runtime+源码双确认）

- iOS 16.3: base `0x180000000`，size `0x100000000` = **4GB**（`[0x180000000,0x280000000)`）
- macOS 15.x: size `0x180000000` = 6GB；15.6.1 缓存实际跨 `0x180000000→0x2ac75c000` ≈ **4.69GB**
- 主缓存 8 条映射 `0x180000000-0x22560c000`：全在界内，已真实映射成功（崩溃报告 `180000000-1e7f5c000 __TEXT SM=COW` 为证）
- `.01` 溢出表：
  - m0 `0x22560c000+0x54808000→0x279e14000` ✓；m1 `→0x27bfd8000` ✓
  - m2 `0x27dfd8000+0x38b4000→0x28188c000` ✗越界；m3-m6 全越界
  - m1/m2 间天然 gap `[0x27bfd8000,0x27dfd8000)`
  - 主缓存 m5/m6 间天然 gap 含 `0x1f8000000`（dynregion 候选位）

**混合映射方案（未实现）**：界内走 536；界外尾巴私有 `mmap` 到自然 VA
（借鉴 dyld `mapSplitCachePrivate`）；或改 .01 头部 `mappingCount`+重签只提交界内部分。

## 6. dyld patch 全表（thin-slice 文件偏移，全部字节级验证过）

| 偏移 | 原始 | patch | 语义 |
|---|---|---|---|
| `0x76270` | `011000d4`(svc) | `e0031faa c0035fd6` = `mov x0,xzr; ret` | crossarch trap stub；**缺它=所有 exec 140(SIGSYS)** |
| `0x30140` | `7f2303d5 f44fbea9` | `00008052 c0035fd6` = `movz w0,#0; ret` | `hasExistingDyldCache`→0，强制走 map 路径 |
| `0x34298` | `c4030094`(bl) | `00008052` = `movz w0,#0` | loadDyldCache 内 reuse 判定→0 |
| `0x3538c` | `7cca51b9` | `3c008052` = `movz w28,#1` | mapSplit 内 flag 强制 1 |
| `0x35fc8` | `e9634ff9`(ldr x9,[sp,#0x1ec0]) | `0900afd2` = `movz x9,#0x7800,lsl#16` | dynregion 提交地址 offset（+base=**0x1f8000000**）；`0x35fd8` 处 `add x9,x9,x11` **必须保留** |
| `0x50dfc` | `f840f9..`(ldr x8,[x0,#0x1f0]) | `00afd2` = `movz x8,#0x7800,lsl#16` | `dynamicRegion()` accessor 同步改 |
| `0x3576c-0x35afe` | preflightMainCacheFile+preflightSubCacheFile 死区 | 自研 blob | 见 §7 ⚠ |

**立即数陷阱**：`movz` 只有 16 位立即数——`0x27c00` 会被静默截断成 `0x7c00`
（曾经因此提交到 0x7c000000 越界失败而 accessor 指 0x27c000000 → SEGV）。
编码前永远用汇编器验证。

## 7. dyld_pi 构建里有什么（重要——别再把死区当空地）

`/tmp/dyldwork/dyld_pi` = pristine + 上表全部 patch + **注入 blob @0x3576c-0x35afe**
（替换了 `preflightMainCacheFile` 函数体；`mapSplitCacheSystemWide` 在 `0x3537c` 调它）。
blob 做的事：open `/System/Library/dyld/dyld_shared_cache_arm64e` → F_ADDFILESIGS →
读 header 填 CacheInfo/mappings（含 fd=-1 dynregion @0x1f8000000）→ dump 到
`/tmp/MTOUT5.txt` → 返回让原代码继续走 syscall。

⚠️ `0x3588c`（preflightSubCacheFile 起点）在 dyld_pi 里**属于 blob 内部，不是死区**。
曾把寄存器探针打在这里（dyld_pm）→ 探针在 blob 半途中触发，dump 的 x8=1/x9=0x228
是 blob 内部状态而非 glue 现场，exit 103 ≠ 到达应用入口。**这是本次 session 确认的
坑：`true`/`ls` 的 RC=103 不能当"dyld 到了 glue call"的证据。**

## 8. 已确认的入口/退出语义

- `start`(0x53dc) 早段调 `hasExistingDyldCache`（@0x5a8c 处 BL）→ check_np →
  **deref dynamicRegion()**。若 region 已填缓存而 dynregion 未映射 → SEGV@0x1f8000000。
- 应用入口调用：`0x6b7c ldr x8,[sp,#0x38]`(glue)；`0x6b80/6b84` x9=[[sp+0x1d0]+8]；
  `0x6b88-90` 取 argc/argv/envp/apple；`0x6b94 blraaz x8`。
- **`dyld_file=<fsid>,<fileid>`** apple 参数：`start` 用它取 dyld 自身路径，
  缺省回落 `/usr/lib/dyld`——不是"可执行文件是 dyld"判定。
- `restartWithDyldInCache` @0x6844：`handleDyldInCache` 分支命中时重启进缓存内 dyld。

## 9. 未决问题（按优先级）

1. **proof2 的 137**：本地构建的 arm64e/platform=1 测试 exe（`/tmp/dyldwork/proof2`，
   ctor 写 `/tmp/ctor_ran`、main 写 `/tmp/main_ran`+stdout+ret7），cdhash 已在
   trustcache 仍被 SIGKILL；而重签+注册的 `date2` 曾 rc=0。**假设**：自制二进制缺
   某个 AMFI launch constraint 要素（platform、CODE_DIRECTORY 形态、或
   `CS_KILL`/`CS_HARD` flag）。验证法：逐步二分（最小 Mach-O→加 LC_BUILD_VERSION→
   加 ctor），或对每次 kill 抓 amfid `log show --predicate 'process=="amfid"'`。
2. **dynregion 持久化**：536 成功后 fd=-1 条目随 mapper 进程退出消失（runtime
   confirmed），文件映射存活。待测：在已填 region 上重交 536 是否重建 dynregion；
   或常驻 keeper（有先有蛋：keeper 自己 exec 时就撞上 §8 的 deref——除非 keeper
   是 mapper 本体且别的进程全走 reuse）；或把 dynregion 改成文件映射。
3. **glue-call 真相未取证**：上一段的 reg dump 被证明是 blob 内部值。要真量
   `0x6b94` 处的 x8/x9，需挑**真正死的**代码洞放探针（如 `__text` 尾部 padding
   或用 `verifyChecksums`/未调用函数），并把 `blraaz` 换成 `b <probe>`。
4. **.01 尾部混合映射**（§5）——设计已定，实现未动。
5. 远景：AGX `0xe00002c2` 结构墙（GUI 的独立 mega-blocker，见 AGENTS.md）——
   与 dyld 线正交，暂不碰。

## 10. 失败史速查（别重复）

| 现象 | 真相 |
|---|---|
| errno 40 | sandbox `file-map-executable` → dyld 签 no-sandbox entitlement |
| errno 22 | UBC CS range 校验 → F_ADDFILESIGS（cachereg） |
| errno 1 (EPERM) | 文件卷/scdir 不对（数据卷↔preboot 与 region root 的 XOR 关系） |
| 140 SIGSYS | crossarch 未打（0x76270）或探针忘了 x16=1 |
| 139 @0x1f8000000 | dynregion 未映射（accessor 已指向那里但映射不存在） |
| 137 SIGKILL | **不是单一原因**：exec 前 AMFI / stale cdhash / launchdchrootexec 注入 libmachook / launch constraint 都可能；每个二进制单独归因 |
| 134 | dyld 磁盘 fallback 死在 libdyld.dylib 缺失 |
| 首跑 0 后续 139 | dynregion ephemerality（§9.2） |
| RC=103 | 本次证明是 blob 内探针 exit(0x67)，**不是** glue 到达证据 |
| 探针写到 /tmp/glueptrr | movk 立即数错位（path byte12 写成 'r'）——手写字符串每字节核对 |
| `check_np`=12 vs 170 | 12=region 存在但空（真实）；170 是 copyin 失败读栈垃圾的假阳性 |

## 11. 复现最短路径（从头到一个能跑的测试）

```bash
# A. 设备端（重启后）
ldid -S/var/jb/usr/macOS/bin/entitlements.plist -M /var/mnt/rootfs/usr/lib/dyld   # 若 dyld 重打过
for f in /var/mnt/rootfs/usr/lib/dyld /var/mnt/rootfs/tmp/proof2 /var/mobile/cachereg; do
  H=$(ldid -arch arm64e -h $f | grep CDHash= | cut -c8-); /var/jb/basebin/jbctl trustcache add $H
done
/var/jb/basebin/jbctl trustcache add 2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e   # 主缓存
/var/jb/basebin/jbctl trustcache add 8c7ba7e588b0edd43f7334e2de11688cd4732192   # .01
nohup /var/mobile/cachereg /var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e \
       /var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e.01 >/tmp/cachereg.log &
# B. 测试
chroot /var/mnt/rootfs /usr/bin/true; echo $?     # 纯 chroot（无 libmachook 变量）
# launcher 路径单独测（注入了 libmachook，结果不可与纯 chroot 混用）
```

## 12. IDA 约定（用户硬性要求）

- 逆向一律走 ida-pro-mcp；Instance1=dyld IDB，Instance2=内核 IDB（`analysis/kc_raw_16.3_T8112.bin`，
  imagebase `0xfffffe0007004000`，若用户处 IDB 丢失需重载）。
- 需要新文件加载 IDA 时**先报文件绝对路径**等用户加载。
- 不用 python 猜指令编码/结构——编码用 `clang -c`+objdump，结构用 IDA py_eval。

## 13. 相关文档

- `dyld-15.6.1-state.md` — 时间序全状态（本文件的超集）
- `kernel-syscall536-re-handover.md` — 536 wrapper/setup 完整反编译笔记
- `kernel-syscall536-finding.md` — sysent 布局
- `dyld-15.6.1-deep-re-handover.md` / `dyld-15.6.1-full-analysis.md` — dyld 全貌
- `patch-ledger.tsv` — 补丁台账（老 13.4 对照 + 新 15.6.1）
- `AGENTS.md` — 项目纪律（symptom-suppress ≠ fix；每条结论要 IDA/runtime 证据）

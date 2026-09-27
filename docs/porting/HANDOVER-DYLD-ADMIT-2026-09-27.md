# HANDOVER — dyld 修改准入已破解 → 现在可以测 `DYLD_SHARED_CACHE_DIR` 路径

**日期**: 2026-09-27 晚
**前置**: 先读 `dyld-15.6.1-state.md` 顶部 MILESTONE 段 + `HANDOVER-HELLO-2026-09-27.md`。
本文件记录：如何造一个"能跑的修改版 dyld"，以及下一个实验（iOS 缓存灌进 chroot
region）的全部前置材料。

---

## 1. 本 session 破解了什么

**现象**：任何字节改动过的 dyld（哪怕改 NOP 填充死区）在 exec 时被内核 SIGKILL
（exit 137，`run_nocskill` 报 `child SIGNALED 9`，dyld 零输出，不留 .ips 崩溃报告）。

**结论**：杀点 = execve hook 里 AMFI 的 dyld 签名验证
（`_cred_label_update_execve` @ kernelcache `0xfffffe00092a1fdc`）：

```c
if ( cs_system_require_lv() | (*a10 & 0x2000) )       // REQUIRE_LV
    if ( (*a10 & 0x2000000) == 0 )                    // "verified" 位
        kill("dyld signature cannot be verified")
```

准入的真正要求（实测归纳，不要再推导）：

| 项 | 要求 | 实测 |
|---|---|---|
| CD `flags` | **必须 0x0（非 adhoc）** | `-Cadhoc`(0x2) 签的修改版全死；裸签(0x0)全活 |
| superblob | 4 项：CD + req + XML ent(0xfade7171) + DER ent(0xfade7172) | ldid -S 自动产出 |
| hashType | 2 (SHA256) | `-Hsha256` |
| 页哈希 | 全部自洽 | ldid 重签自动重建 |
| cdhash | 在 jailbreak trustcache | `jbctl trustcache add`（见下） |
| CPU subtype | fat 里 arm64e slice 需 subtype=0（`fat_arm64ify.py`） ——对 dyld thin 不适用（本来就是单 slice） | — |

**唯一活的签名配方**（设备上跑）：

```bash
/var/jb/usr/bin/ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist /var/mobile/dyld_X.bin
# 注意：不加 -Cadhoc、不加 -M
```

cdhash = `sha256(CD blob)` 取**前 20 字节 = 40 hex**（`cdhash.py` 的输出截前 40 字符）：

```bash
/var/jb/basebin/jbctl trustcache add <40hex>
```

部署：**必须 `rm` 再 `cp`**（新 inode），`chmod 755`：

```bash
rm -f /var/mnt/rootfs/usr/lib/dyld && cp /var/mobile/dyld_X.bin /var/mnt/rootfs/usr/lib/dyld && chmod 755 /var/mnt/rootfs/usr/lib/dyld
```

### 已证伪/修正

- `resign_dyld.py`（保 superblob 只改页哈希）：文件自洽+TC 在册**仍死**。原因未查清，
  但 ldid 裸签可过——**保 blob 路线放弃，一律用 ldid 重新签**。
- `-Cadhoc`：对未改内容的文件签了反而能跑（诡异），对改过内容的必死。
  **规则就是：永远别加**。
- trustcache info 是大写 hex，grep 要 `-i`。
- exec 时 SIGKILL **不产生 .ips**（veto 发生在进程生成前）；runtime 的
  "Invalid Page" SIGKILL 才产生 `subtype=0x32` 报告。两者要分清。

### 已知例外（记录但不阻塞）

`dyld_es.bin`（隔壁 AI 早期产物：crossarch+entrymark+segvcap）：能跑（dyld 打印了
"dyld cache not loaded"），随后 **SIGILL(4)** ——死在它自己的探针代码，不是准入问题。
说明"修改 __TEXT 的 dyld 可以执行"，本次配方验证同结论。

---

## 2. 下一个实验：iOS 缓存灌进 chroot region（不需要内核 patch）

**目标**：让 chroot 的 macOS dyld 通过 syscall 536 把 **iOS dyld 缓存**映射进
chroot 专属的空 shared region → 整栈真 iOS 库可用。

**原理**（dyld 源码 `dyld-1286.10` 已确认）：
- chroot 进程 exec 时拿到按 `fd_rdir` 键控的**空 region**
  （`__shared_region_check_np`=12/ENOMEM,base=0 —— HELLO 实验实锤）
- dyld fallback 走 syscall 536 (`shared_region_map_and_slide_np`) 填 region
- dyld 支持 `DYLD_SHARED_CACHE_DIR` env → `openat(cacheDirFD, ...)` 找缓存文件
- **唯二已知的 536 阻塞点**:
  1. dyld 打开缓存后做 platform preflight（thin `0x35c1c-0x35c24`:
     `LDR W8,[hdr.platform]; CMP W8,W0; B.NE → altPlatform 检查 → 拒绝`
     "dyld cache ... is for a different platform"）。iOS 缓存 platform≠macOS
     → 需要 `plataccept` 补丁：**`0x35c24` `B.NE` → `NOP`**(RE-confirmed via IDA)。
  2. iOS 缓存文件在 chroot 内不可见 + 需要 vnode CS blob（`cachereg`/`fcntl F_ADDSIGS`）。

### 现场已备好

- `/var/mnt/rootfs/iosdsc/` 已拷入全部 46 个 iOS 缓存分片
  （源 `/System/Cryptexes/OS/System/Library/Caches/com.apple.dyld/`,~3.2G。
  验证 `ls | wc -l`=46）。
- iOS 缓存主文件 platform 字段值：与 macOS 不同 → 触发上述 preflight。
- `cachereg_ios`（设备 `/var/mobile/cachereg_ios`？或 `misc/` 里源码）可对
  缓存文件做 `fcntl(F_ADDSIGS)` 附加 CS blob。**注意：cachereg 是常驻型进程，
  循环里会卡住——要后台 & 运行，别串行等它**。

### 实验步骤（建议顺序）

```bash
# 1. 构建: pristine + crossarch + plataccept(0x35c24 NOP)
#    用 analysis/dyldwork/build_dyld.py 加 plataccept patch
# 2. 签名: ldid -Hsha256 -S<ent>（无 -Cadhoc！）→ cdhash → jbctl add → rm+cp+chmod
# 3. 无 env 先验证 dyld 本身活着（HELLO 应过）
# 4. 带 env 测:
DYLD_SHARED_CACHE_DIR=/iosdsc /var/mobile/run_nocskill /var/jb/usr/bin/env -i \
  PATH=/usr/bin:/bin DYLD_SHARED_CACHE_DIR=/iosdsc \
  DYLD_PRINT=cache,info,warnings,segments \
  /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO
# 5. 判定: 出现 "dyld cache '…' is for a different platform" = plataccept 没生效;
#    EINVAL(22) = VA 冲突还在（不该出现——iOS 缓存地址本来就是 region 窗口）;
#    HELLO+全 iOS 库 = 成功
```

预期成功标志：dyld 不再走磁盘 shim；`/bin/cat`、`/bin/ls`、`/bin/sh` 等
直接可用（真 libc）。若成功，**shim 方案废弃**，只用磁盘补 macOS 独有库。

### 如果平台补丁生效但映射仍失败

候选：536 要求映射文件 code-signed-attached —— 先对 `/iosdsc/*` 全量跑
`cachereg`（后台）再测；或检查 `ubc_cs_is_range_codesigned` 门
（kernel IDA `0x8459CBC`，见 state.md §155）。

### 如果 `DYLD_SHARED_CACHE_DIR` 根本不走 536

dyld 可能先尝试 reuse（空 region check_np=12 → fail → 536）。fallback 链
顺序见 `SharedCacheRuntime.cpp`；必要时对 thin dyld 加第二个补丁强走
`mapSplitCachePrivate`/`loadDyldCache` 分支（`0x34240` 附近，IDA 看）。

---

## 3. 关键文件/地址速查

| 物 | 位置 |
|---|---|
| pristine dyld thin | `analysis/dyld_15.6.1_arm64e_thin`（IDB 在 Instance1） |
| 构建器 | `analysis/dyldwork/build_dyld.py`（patch 定义在文件内；**构建后必须验证产物字节**——此文件可能被并行编辑覆盖） |
| cdhash 计算 | `analysis/dyldwork/cdhash.py`（输出 40hex；jbctl 取前 40） |
| fat arm64ify | `analysis/dyldwork/fat_arm64ify.py`（普通可执行文件过 exec 门用） |
| 设备入口 | `sshpass -p cisco ssh -p 2222 root@192.168.64.1` |
| 测试 harness | `/var/mobile/run_nocskill`（自动清 CS_HARD\|CS_KILL） |
| rootfs | `/var/mnt/rootfs`;iOS 缓存拷贝 `/var/mnt/rootfs/iosdsc/` |
| dyld platform 检查 | thin `0x35c24` `B.NE`→`NOP`（plataccept） |
| dyld entry | `0x6b94` `BLRAAZ X8` |
| dyld reuseExistingCache | `0x351a8`;check_np stub `0x76dcc`;536 stub `0x76df8` |
| kernel execve kill | `0xfffffe00092a1fdc` `_cred_label_update_execve`（Instance2 IDB） |
| kernel vnode_check_signature | `0xfffffe00092a45e4` |
| 536 内核门表 | `shared_region_map_and_slide_setup` `0xfffffe0008459570`（state.md §155 有门表） |

## 4. 行为判据

- HELLO 验证：`dyld[...]: dyld cache '(null)' not loaded` + `HELLO` + `rc=0`
- exec veto:`SIGNALED 9` + dyld 零输出 + 无 .ips → 签名配方问题（先查 flags）
- SIGILL(4)：探针代码自身问题（不是准入）
- 连跑 **≥3 次**再下结论

# HANDOVER REPLY — 2026-09-28：iOS dyld 缓存已成功灌进 chroot 的 shared region（syscall 536 打通）

回复对象：`HANDOVER-DYLD-ADMIT-2026-09-27.md`（原 AI 分派：测通 iOS dyld 缓存灌入 macOS chroot 的 shared region）
单一事实来源：`docs/porting/dyld-15.6.1-state.md` 顶部「📌 顶部摘要」

---

## 0. 一句话结论

**536 打通了。** 真凶不是签名、不是 CS、不是 VA 冲突，而是 **`files[].sf_slide` 非 16K 对齐**。
清零后 `shared_region_check_np` 返回 `0 / base=0x180000000`，dyld 报 `Using mapping in dyld cache ×91`
与 `re-using existing shared cache ×2`、`cache not loaded ×0`，`/bin/echo HELLO` rc=0（3× 复现）。

**唯一未完成项**：`libSystem.B.dylib` 仍取磁盘 shim（`/bin/echo` 只需 `_err` 故过关；`/bin/cat`/`/bin/sh` 需
`___error` 而 shim 无 → 失败）。移走 shim 后 dyld 会用缓存解析 91 库，但 `/usr/lib/libSystem.B.dylib`
被平台检查拒（`wrong platform to load into process`），用 `platstub` 强载则 **SIGILL(132)**（无 .ips）。
⇒ 结论：**iOS 库 ≠ macOS 进程**；真正要跑 macOS 软件需映射 **macOS 自己的缓存**（下一步，需干净 region）。

---

## 1. 本任务要求 vs 实际达成（逐条审计）

| handover 要求 | 状态 | 证据 |
|---|---|---|
| 1. `build_dyld.py` 加 `plataccept`(0x35c24) | ✅ | 已有 key；产物字节校验 `0x35c24=d503201f` |
| 2. 按配方签名部署，不带 env 验 HELLO | ✅ | `chroot $R /bin/echo HELLO` → rc=0（3×） |
| 3. 带 `DYLD_SHARED_CACHE_DIR=/iosdsc` 测 | ✅ | 536 成功；`check_np base=0x180000000`；`cache not loaded ×0` |
| 4. 成功 = HELLO + 库全走 iOS 缓存 | ⚠️ 部分 | 91 image 走缓存映射；**libSystem 仍是磁盘 shim**（平台/兼容墙） |
| 5. `/bin/cat /bin/ls /bin/sh` | ❌ | `___error` 缺失 / `libutil wrong platform`（同 §4 的墙） |

---

## 2. 最小修复（本任务核心，务必照抄）

### 2.1 真因
`shared_file_np` = **12B/条 `{int sf_fd; int sf_mappings_count; uint64 sf_slide;}`**（sf_slide 在 +8）。
iOS split-cache 主分片携带 `sf_slide = 0x539b0000`（**非 16K 对齐**）→ 内核在
`shared_region_map_and_slide_setup` 阶段直接 `EINVAL(22)`。

### 2.2 patch（在 536 调用点前，清零所有 entry 的 sf_slide）
- 构建：`python3 analysis/dyldwork/build_dyld.py dyld_sf0.bin crossarch plataccept`
- 再打 cave：hook `0x35690 → 0x9b578`（thin），cave 内容（机器码见 `dyld_sf0.bin`）：
  ```
  mov x9,x1 ; mov x10,x0                 ; x1=files, x0=files_count
  loop_files: str wzr,[x9,#8] ; add x9,x9,#12 ; subs x10,x10,#1 ; b.ne loop_files
  mov x3,x26 ; b 0x35694                 ; 复现被覆盖的原指令并回到 536 调用
  ```
- 产物：`analysis/dyldwork/dyld_sf0.bin`

### 2.3 部署（配方不变）
```bash
ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist <file>   # 不加 -Cadhoc/-M
python3 misc/cdhash_slices.py <file>            # 每个 slice 的 cdhash
jbctl trustcache add <每个 40hex>
rm -f /var/mnt/rootfs/usr/lib/dyld && cp <file> .../dyld && chmod 755
```

---

## 3. 验收（3×，命令与原始输出）

```bash
env -i PATH=/usr/bin:/bin DYLD_SHARED_CACHE_DIR=/iosdsc \
  DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_SEGMENTS=1 \
  /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO
```
- `rc=0`、输出 `HELLO`
- `Using mapping in dyld cache` × **91**
- `re-using existing shared cache` × **2**
- `cache not loaded` × **0**
- 探针：`dyld_cknp2.bin` → `check_np ret=0, base=0x180000000`（region 已被 iOS 缓存填上）

---

## 4. 前置条件（缺一不可；已脚本化）

1. **重启后补 trustcache**：`bash /var/mobile/restore_env.sh`（内部用 `misc/cdhash_slices.py`；纯文件层，无内核 patch）
2. **cachereg 给缓存文件挂 CS blob**：`cachereg_ios /var/mnt/rootfs/iosdsc/*`（46 片；日志 `READY ok=1`）
3. **CS blob 覆盖整文件（KRW）**：`set_blob_cov.py <每个 dsc>`（把 `cs_blob.csb_end_offset(@+0x38)` 改成文件大小）
   - 依据：`ubc_cs_is_range_codesigned()` 要求 `[csb_base_offset+csb_start_offset, +csb_end_offset] ⊇ [file_offset, +size]`（xnu `bsd/kern/ubc_subr.c`）

---

## 5. 关键坑（省你时间）

- **签名/准入**：cdhash = `sha256(CodeDirectory[0:CD.length])[:20]`；不是到尾、不是整个 superblob。
  `SIGNALED 9` 先查这个（`misc/cdhash_slices.py` 对每个 slice 都算）。
- **重启易失**：jailbreak trustcache（内存）+ vnode 上的 cs_blob（cachereg 进程死即丢）；rootfs 不丢。
- **`run_nocskill` 旧件硬编码 kernel slide** → 重启后 `proc not found`；**实测 TC 复原后直接 `chroot` 即可**。
- **marker 探针（mkA…/m2u…）不可靠**（`SIGNALED 11`、零 marker）→ 用 `cknp2` 或自建 cave（如本任务的 dump 探针）。
- **shared region 跨进程持久**（`check_np` 连续两次 `0/0x180000000`）⇒ 换缓存必须**新 region（重启）**；
  bindfs 新建 root 在本机不可用（`mount_bindfs: No such file or directory`）。
- **macOS 缓存其实没有 slide-info**（`slideInfoVersion=0/slideInfoOffset=0`）⇒ 旧"slide-info v5"说法对本缓存不成立。

---

## 6. 下一步（交给下一位 agent / 原 AI）

**目标**：把 **macOS 缓存**（`/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e` 2.7G + `.01` 2.2G）
灌进 region，从而让 `/bin/cat /bin/ls /bin/sh` 真正从缓存跑起来。

**已备好一键脚本**（设备上）：`bash /var/mobile/post_reboot_mac.sh`
- 校验 region 干净度 → 复原 TC → cachereg + blob 覆盖（macOS 缓存）→ 部署 `dyld_noslide.bin`
  （= crossarch+plataccept + `sf_slide` 清零 + mapping `slide_size/slide_start` 清零）→ 验收 HELLO/using/notloaded ×3 → 再验 cat/ls/sh。

**注意**：必须在**重启后的干净 region** 上跑（当前 region 已被 iOS 缓存占住）。

---

## 7. 交付物清单

- 代码/脚本（均已 commit+push，见 `git log`）：
  - `misc/cdhash_slices.py`、`misc/restore_env.sh`、`misc/run_nocskill.c`（修 kernel slide 扫描）
  - 设备侧：`/var/mobile/nm/cdhash_slices.py`、`/var/mobile/blob_read.py`、`/var/mobile/set_blob_cov.py`、
    `/var/mobile/post_reboot_mac.sh`、`/var/mobile/dyld_{sf0,final,noslide,nr2,cknp2,e5}.bin`
- 文档：`docs/porting/dyld-15.6.1-state.md`（顶部摘要 = 最新事实）

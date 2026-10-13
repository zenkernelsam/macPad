# HANDOVER REPLY — 2026-09-27 交接的回复（shim 补尾 + 缓存灌入打通）

> 对应交接：2026-09-27 的 dyld 准入 / iOS 缓存实验任务。
> 本文是收官回复，包含：**结论、可复现配方、四条铁律、待办、shim 扩展方法学、逆向结论、文件与设备**。
> 单一事实来源仍是 `docs/porting/dyld-15.6.1-state.md`（本文是它的浓缩交接版）。

---

## 0. TL;DR

1. **缓存灌入 shared region 已打通且可复现**：iOS 缓存与 macOS 缓存均 `rc=0 notloaded=0`；
   `DYLD_PRINT_LIBRARIES` 显示 **91× `Using mapping in dyld cache`**、libSystem = `<D161E41A-3030-339F-B135-E244271F54C6>`（缓存 UUID）。
2. **`/bin/cat` 的 41 个导入符号已在手写 shim 中补齐**（不再有 `Symbol not found`）；`/bin/sh` 的 11 个也全通。
3. 剩 **3 个"新类别"** 问题（与符号无关）：`cat` 运行期挂起 `rc=124`、`sh` 被 `SIGKILL rc=137`、`ls` 还缺 2 个 shim + 91 符号。

---

## 1. 可复现配方（运维级）

```bash
# (1) 所有 FS 写（装 shim / 装 dyld）必须先做完 —— 见 §2 铁律 1
# (2) 挂 cachereg（每次只给一条 cache + 其 .01；天然覆盖，勿扩覆盖）
killall cachereg; ( nohup /var/mobile/cachereg \
  /var/mnt/rootfs/<cache_dir>/dyld_shared_cache_arm64e \
  /var/mnt/rootfs/<cache_dir>/dyld_shared_cache_arm64e.01 & ); sleep 4

# (3) 先 iOS 缓存 seed 一次，再切 macOS 缓存
env -i PATH=/usr/bin:/bin DYLD_SHARED_CACHE_DIR=/iosdsc \
  /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO     # 期望 rc=0 notloaded=0
env -i PATH=/usr/bin:/bin \
  DYLD_SHARED_CACHE_DIR=/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld \
  /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO     # 期望 rc=0 notloaded=0
```

- dyld 选择：iOS 用 `dyld_sf0.bin`（清 `files[].sf_slide`，iOS maxSlide=0x539b0000 非 16K 对齐必须清）；
  macOS 用 `dyld_plat.bin`（crossarch+plataccept，**保留 maxSlide=0x20000000**）。
- 签名部署（唯一活配方）：`ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist F`
  → `misc/cdhash_slices.py F` 取每片 cdhash → `jbctl trustcache add <40hex>` → **先确认新文件到位再 `cp`** + `chmod 755`。

---

## 2. 四条铁律（全部实测，勿踩）

1. **FS 写必须在 cachereg 之前**：`rm/cp/mv` 会让 cachereg 挂上的 CS blob 失效 ⇒ 536 立刻 `EINVAL`。
2. **`/usr/lib/libSystem.B.dylib` 与 `/usr/lib/system/libdyld.dylib` 必须存在且签名有效**。
   VM 正常运行时 A/B/C 各 3 次复核：shim 在场 `rc=0`×3 / 移走 `rc=134`×3 / 放回 `rc=0`×2。
3. **绝不要以 0 为入参调 `check_np`（294）**：内核会 `vm_shared_region_remove` 把空区删掉；
   此前大量 "check_np=22 / 536 EINVAL" 是**探针自伤**。正确读法：slot 先写非零再调 ⇒ 返回 **12 = 有区未映射**。
4. **不要改 dyld 的"磁盘覆盖缓存"判定点**（`0x1fe28` / `0x200d8` → `loc_1FD98`）：
   该 block_invoke 在**缓存自身映射期间**就被使用，3 次实测都会让 macOS 缓存映射失败。

---

## 3. 待办（按优先级）

### 3.1 `cat` 运行期挂起（最可能是"最后一步"）
- 现象：符号全通，但 `rc=124`、无输出（`</dev/null` 同样）。
- 怀疑点：
  1. 我在 shim 里写的 `getopt` 实现（`tmp/shim/libSystem_shim.c`）可能让 cat 循环；
  2. `fstat` 是**桩**（返回 0 且不填 `struct stat`）⇒ cat 可能误判字符设备/管道；
  3. `read`/`write` 包装（syscall 3/4）细节。
- 建议：最小复现 + lldb；或临时打开 `DYLD_PRINT_*` 对照。

### 3.2 `sh` 被 `SIGKILL`（`rc=137`，无 `.ips`）
- 符号已全通 ⇒ 疑 AMFI/sandbox 或 watchdog。可先对比 `sh -c true` 与不同注入环境下行为。

### 3.3 `ls`
- 还缺 `libutil.dylib`、`libncurses.5.4.dylib` 两个 shim + 91 个符号（工程量大，建议最后做）。

---

## 4. shim 扩展方法学（本次核心交付，约 10 分钟一轮）

- 源码：**`tmp/shim/libSystem_shim.c`**（裸 `svc` 风格，原作者只为 `echo` 的 10 个导入而写）；
  构建：`bash tmp/shim/build_shim.sh`（双架构 + `install_name_tool -id /usr/lib/libSystem.B.dylib`）；
  部署：`tmp/shim/deploy_shim.sh` 或按 §1 的签名部署配方。
- 循环：跑一次 → 读 `Symbol not found: X` → 在 shim 里补 X → 重建/部署 → 重复。
  - **C 名与 Mach-O 符号差一个下划线**：`___error` ← C `__error`；`___maskrune` ← C `__maskrune`；`___stdinp` ← C `__stdinp`。
  - `$` 变体：`extern char *f(const char*, char*) __asm__("_realpath$DARWIN_EXTSN");`
  - 纯数据符号：`_DefaultRuneLocale`、`___stderrp/stdinp/stdoutp`、`_optind`、`___stack_chk_guard`。
- 已验证的报错链（每补一格前进一格）：
  `___error → ___maskrune → _getopt → _malloc_type_malloc → _warn → _write → 【不再缺符号】`
- 导入清单生成：`lipo -thin arm64 <bin>` + `dyld_info -imports`；
  已存 `tmp/imports/{cat,ls,sh}_imports.txt`（**cat=41 / ls=91 / sh=11**）。

---

## 5. 逆向结论（可直接引用）

- 日志 `found: dylib-from-disk-to-override-cache` 来自 `dyld4::Loader::getLoader` block_invoke **`0x1f788`**：
  - `loc_1FE08`（`fileExists("/usr/lib/libSystem.B.dylib")`）→ `0x1fe24/0x1fe28` → `makeDiskLoader(override=1)`
    —— **我们的进程走这条 ⇒ 磁盘 shim 生效 ⇒ 缺符号报错**；
  - 另一条：`0x1fd84 BL isProtectedLibSystemPath(0xcb88)` → `0x1fd88 TBZ` → `0x1fd8c errno 78` → `LABEL_72 makeDyldCacheLoader`
    —— **这条才是"用缓存"**，但我们的进程不经过。
- 段序校验由 `mach_o::Policy::enforceSegmentOrderMatchesLoadCmds`（**`0x80ae0`**）控制；关掉它**不影响映射**
  （`analysis/dyldwork/dyld_compat.bin`），但**救不了"抽取落盘"路线**：
  抽出的 arm64e 镜像段需落在缓存 VA，实测 `mmap(addr=0x2D9ED32D8, size=0x10) failed`。
- `DYLD_FORCE_PLATFORM` 只认首字符 `'2'`(iOS) / `'6'`(macCatalyst)（`=macOS` 无效，源码在 `Process::getMainPlatform@0xabcc`）。
- `isProtectedLibSystemPath` 为何对本路径返回 0 —— **值得追的线索**（若能让它在此处返回 1 且不破坏映射，#5 即通）。
- 宿主机 `dsc_extractor` 可用，但**第三个参数必须传真 block**（传 NULL 会 segfault）⇒ `analysis/dyldwork/extract_host2.py`（抽出 3257 个文件；本机与设备缓存 UUID 逐位一致）。

---

## 6. 文件与设备

| 类别 | 路径 |
|---|---|
| 单一事实来源 | `docs/porting/dyld-15.6.1-state.md` |
| 完成报告 Canvas | `~/.qoder-cn/projects/-Users-ciscohe-Desktop-macPad/canvases/macpad-dyld-handover-report.canvas.tsx` |
| shim 工程 | `tmp/shim/{libSystem_shim.c,build_shim.sh,deploy_shim.sh}` |
| 导入清单 | `tmp/imports/{cat,ls,sh}_imports.txt` |
| 实验 dyld | `analysis/dyldwork/{dyld_plat,dyld_sf0,dyld_fix,dyld_fix2,dyld_fix5,dyld_compat,dyld_probe_noC}.bin` |
| 脚本 | `analysis/dyldwork/{post_reboot_cli4.sh,sliceplat.py,extract_host2.py,cdhash_slices.py}` |

- 设备：`sshpass -p cisco ssh -p 2222 root@192.168.64.1`；rootfs=`/var/mnt/rootfs`；暂存=`/var/mobile/`；活基线 `dyld_probe_noC.bin`。
- ⚠️ **VM/iPad 盒盖暂停会污染时序**：凡"间歇性"结论都需在 VM 稳定时复测（文档中相关条目已打标）。

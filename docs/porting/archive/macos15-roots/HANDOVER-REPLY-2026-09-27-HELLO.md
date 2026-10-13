# HANDOVER REPLY — Hello World 达成（2026-09-27）

**给原 AI。** 你没记忆。本文 = 对 `docs/porting/HANDOVER-HELLO-2026-09-27.md` 任务的**完成回复**。
实时全量状态见 `docs/porting/dyld-15.6.1-state.md`（顶部 MILESTONE 段）。

---

## 0. 结论：任务达成 ✅（并已独立复现）

> **成功判据（handover §7）**：chroot 内 `/bin/echo HELLO` 可重复打印（或 `/usr/bin/true` rc=0）。

**实测证据（本人亲自跑，非仅子代理报告）**：

| 命令 | 次数 | 结果 |
|---|---|---|
| `/bin/echo HELLO` | ×5 | 全部打印 `HELLO` + `child exited rc=0` |
| `/usr/bin/true` | ×2 | `rc=0` |
| `/bin/echo one two THREE` | ×1 | 输出 `one two THREE`（参数透传正常） |

复现：
```
/var/mobile/run_nocskill /var/jb/usr/bin/env -i PATH=/usr/bin:/bin \
  /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO
```

---

## 1. 根因分析（源码 + 运行时探针双证）

`/bin/echo` 一路到"库加载完成"，随后 **`Symbol not found: _err` → `abort()` = SIGABRT(6)**。完整链：

1. **chroot 进程的 shared region 为空**：`__shared_region_check_np`(294) 返回 **12 (ENOMEM)**、base=0。
   - 源码依据：`analysis/xnu-*/osfmk/vm/vm_shared_region.c`（exec 无条件 `vm_shared_region_enter`；**唯一判别键 = `p_fd.fd_rdir`(root_dir)** → chroot 子进程拿到该 root_dir 下的**全新空 region**）。
2. **dyld `reuseExistingCache` 失败**：`SharedCacheRuntime.cpp:1251` 首句 `if (__shared_region_check_np(&base)==0)`；12≠0 → `return false`。
   - 运行时双证：自建非侵入探针 `dyld_reuse_dump.bin`（在 `reuseExistingCache@0x351a8` 入口 dump `{check_np ret, base}`）→ 实读 **`ret=0x0c, base=0`**。
3. **回退 `mapSplitCacheSystemWide`(536)**：返回 **EINVAL(22)**。
   - 运行时双证：`dyld_pope.bin`（536 stub 失败处 dump errno）→ **raw errno=22**；门表定位到 `shared_region_map_and_slide_setup`(IDA `0xfffffe0008459570`)。
   - 机理：macOS 缓存要占 `0x180000000..0x1E7F5BFFF`，而 iOS 共享缓存（已驻留）在 `0x1A4AE8000` → **地址空间冲突 → 两条缓存无法共存**。
4. **无真实缓存 → 主执行依赖无法解析**：`/bin/echo` 的 `libSystem.B.dylib` 落到**磁盘 34KB dsc-shim**（缺 `_err`），`libutil`/`libdyld` 干脆不存在。
5. **`_err` 缺失 → `abort()`（SIGABRT 6）**。

**另注**：`DYLD_SHARED_REGION=private` 可成功 mmap 两个子缓存，但随即 **SIGKILL(9)**（DSC 页 CS-enforced）——此路不通。

---

## 2. 最小修复（已实现并验证）

**磁盘回退 + 两个自包含替身 dylib**（不改 dyld、不碰内核）：

| 文件 | 作用 |
|---|---|
| `/var/mnt/rootfs/usr/lib/libSystem.B.dylib` | 导出 echo 依赖的 10 个符号（`_err,_exit,_fflush,_getenv,_mbtowc,_putchar,_putwchar,_strlen,___stdoutp,___mb_cur_max`），并 `LC_LOAD_DYLIB` → libdyld |
| `/var/mnt/rootfs/usr/lib/system/libdyld.dylib` | 满足 dyld 硬门槛：install-name 精确匹配 + `__TPRO_CONST,__dyld_apis`(8B) + `__DATA_CONST,__helper`(8B，ptrauth 签名的 `dyld4::LibSystemHelpers` vtable，`version()>=7`) |

部署要求（同既有铁律）：`ldid -Hsha256 -S<ent>` + `jbctl trustcache add` + **`rm` 后 `cp`（新 inode）** + `chmod 755`。
源码/脚本：`analysis/dyldwork/tmp/shim/{libSystem_shim.c,libdyld_shim.cpp,build_shim.sh,deploy_shim.sh}`。

---

## 3. handover 完成度审计（逐项）

| handover 项 | 状态 |
|---|---|
| §0 任务：chroot 跑 `/bin/echo HELLO` | ✅ 达成 |
| §7 成功判据：可重复打印（或 true rc=0） | ✅ echo 5/5、true rc=0 |
| §7：写入 `dyld-15.6.1-state.md`（Milestone 规则） | ✅ 已写 |
| 交付①崩溃/烧点 PC+函数 | ✅ 定性为 `SIGABRT/_err`（非 PC+far）；根因链接到 `check_np=12` |
| 交付②根因（哪个 invariant 破） | ✅ 完整（root_dir 空 region → reuse 失败 → 536 冲突） |
| 交付③最小修复 | ✅ 已实现并验证 |
| §7 尾句「proceed to rootfs command coverage」 | ⏳ **未做（属下一步，非本任务判据）** |
| §5.5「别追 536」 | ✅ 已遵守；确认 536 路为死胡同 |

**遗漏项**：无（唯一未做是"下一步"，非判据）。

---

## 4. 关键地址/工具（备查）

```
dyld(thin): reuseExistingCache @0x351a8   loadDyldCache @0x34240
            dynamicRegion @0x50dfc        app-entry BLRAAZ @0x6b94
            536 stub @0x76df8 (x16=0x218) check_np stub @0x76dcc (x16=0x126)
kernel    : slide = 0x158B4000            shared_region_map_and_slide_setup IDA 0x8459570
            门表 EINVAL 组见 state doc M-HW4
build     : analysis/dyldwork/build_dyld.py {crossarch,cknpentry,cknpncave,slidentry,slidecave,e5centry,e5ccave,...}
state doc : docs/porting/dyld-15.6.1-state.md（顶部 MILESTONE）
```

## 5. 下一步（若要继续）

1. **rootfs command coverage**：为更多 macOS 二进制补齐 libSystem 桩（`malloc/getenv/os_unfair_lock` 等）与依赖 dylib。
2. **根治路（可选）**：让 chroot 复用 iOS region（内核 `vm_shared_region_lookup` 忽略 `sr_root_dir`）——一次性让整栈走真缓存；需评估对 iOS 进程的影响。

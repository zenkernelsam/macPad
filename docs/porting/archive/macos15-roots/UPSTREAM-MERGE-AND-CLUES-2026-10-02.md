# Upstream merge 2026-10-02 + clues mined from the refreshed AGENTS.md

Static-side session, /Users/ciscohe/Desktop/macPad.
Scope of the request: (a) merge upstream's new commits into this fork, (b) read the
newly added/refreshed `AGENTS.md`, (c) mine it (and the new commits) for anything
useful to the 15.6.1 shared-cache blocker, (d) produce a reply prompt for the
neighbouring agent.

Evidence labels used below: **RE-confirmed** (from a binary/source artifact),
**runtime-confirmed** (verbatim log/crash excerpt), **THEORY** (unverified).

---

## 1. Merge result

| Item | Value |
|---|---|
| Upstream commits merged | 7 — `8c59f89`, `e7a1c55`, `49fdccc`, `9450db3`, `8586fd9`, `bd6dd82`, `e697d84` |
| Files | 25 changed, +2580 / −1352 |
| Conflict | **1**: `AGENTS.md` (both sides inserted a top-level section at the same seam) |
| Resolution | Kept **both**: upstream's new `## Current Project Memory and Operating Baseline (2026-10-01)` first (it declares precedence over older sections), then this fork's `## Project-Knowledge-First & IDA Pro RE Workflow` (absent upstream) |
| Merge commit | `cc56e15`, pushed to `origin/main` (kernelcommit d3cecb9 → cc56e15) |
| Behind upstream now | 0 |
| Post-merge gate | `python3 -m unittest misc.test_agents_memory_ledger` → **4/4 OK** |

Content of the 7 commits is **GUI/windowing only** (floating-Dock layout authority,
Magic Keyboard during IME, virtual-keyboard toolbar routing, Codex Code Mode V8
startup, Terminal tab/Dock windowing). **No dyld / shared-cache / 15.6.1 content.**
The substantive change is the AGENTS.md refresh.

---

## 2. Clues from the refreshed AGENTS.md (authoritative, dated 2026-10-01)

### 2.1 Platform matrix — 15.6.1 is *not* an evaluated target

`AGENTS.md` "Validated platform matrix" lists only Ventura 13.4 / 22F66 for
iPad13,6 and iPad14,5. macOS 15.6.1 does not appear at all; the 15.6.1 install
toolchain lives in the tree but is not part of the author's validated set.
The NathanLR row is the one that matters to us (see 2.2).

### 2.2 The author's own statement of *why* the cache needs the trustcache

**`layout/DEBIAN/postinst:83-88`** (verbatim, labelled runtime-confirmed on
iPad13,7 / iPadOS 16.6 / NathanLR):

> The chroot executes patched macOS dyld/shared-cache images whose fixed
> CodeDirectories cannot be made admissible by CoreTrust-signing ordinary
> Mach-O files. A full jailbreak trustcache backend is therefore structural,
> not an optional post-install accelerator. Runtime-confirmed … the
> CoreTrust-signed loader, target and injected dylib still die with SIGKILL,
> while AMFI selector 2 rejects an identically entitled package helper with
> kIOReturnNotPermitted (0xe00002e2).

Two consequences for our blocker:

1. "**fixed CodeDirectories**" ⇒ the cache's per-page hashes are baked into its
   embedded CodeDirectory; re-signing the cache file is impossible, so the
   *only* admission channel is the dynamic trustcache (`jbctl trustcache add
   <cache cdhash>`). This matches our host-side finding that the cache is
   **ad-hoc signed** (§3.2).
2. AMFI *does* have a path that rejects an otherwise-entitled helper in this
   space (`AMFI selector 2` → `0xe00002e2`). Whether the gate we are hitting is
   the same one is **THEORY**, not confirmed.

### 2.3 Cold-boot witness: the cache CDHashes are load-bearing

**`layout/DEBIAN/postinst:437-445`** (verbatim):

> Runtime-confirmed on a cold 2026-09-09 boot: after the executable loader
> closure below was admitted, dyld rejected the codesign probe with
> "code signature registration for shared cache failed" and consequently
> could not resolve cache-resident libTLE.dylib. These are the same exact
> cache hashes validated by macos_gui.sh before every cold workspace start.

This is the author's *own* instance of the failure class we are fighting
("code signature registration for shared cache failed"), and their fix was
exactly the two `jbctl trustcache add <cache cdhash>` calls. dyld's
registration site is `analysis/dyld-dyld-1286.10/dyld/SharedCacheRuntime.cpp:301-313`
(`fcntl(fd, F_ADDFILESIGS_RETURN, …)` → mmap first page `PROT_READ|PROT_EXEC`
`MAP_PRIVATE` → `memcmp`). **RE-confirmed.**

### 2.4 New diagnostics / workflow we should adopt

| Item | From | Value to us |
|---|---|---|
| `MACWS_LLDB_HOLD_AFTER_INIT` | `docs/runtime-switches.tsv` | Stops right **after** libmachook installs interposes + its dyld image callback; narrowable with `MACWS_SUSPEND_TARGET`. This is a better cold-start hook than `MACWS_SUSPEND_AT_EXEC` for per-PID triage of `run_bash.sh -c "echo hi"` |
| `misc/device_pipeline.sh --component <c>` | AGENTS.md | Content-verified deploy; replaces ad-hoc `scp`. Explicit rule: "Avoid direct in-place `scp` over a signed dylib: reusing the vnode can leave the kernel's code-signature cache stale" — matches our known AMFI Invalid-Page bug |
| `MACWS_CHROOT_HOST_ROOT` | `include/macws_chroot_environment.h`, `Metal_hooks.x:3452` | Canonical **host** path captured before chroot, used to translate kernel file-ID paths into the process-visible root namespace. Relevant if our fault path compares a kernel-reported path against a chroot-visible one |
| `__mac_syscall` / `__sandbox_ms` AMFI interposition | `libmachook/mac_hooks.m:11592-11700` | Existing pattern: answer an exact `("AMFI", op)` policy query by writing the caller's output struct, gated by policy name + operation number + env switch. A reusable scaffold if the blocker turns out to be a *userland* AMFI query (it is **not** the runtime-fault path we currently chase) |
| `misc/test_agents_memory_ledger.py` | new | Contract test that the imported project-memory ledger stays in AGENTS.md — passes after our merge |

### 2.5 Newly added evidence note (closest thing to our domain)

`docs/evidence/codex-code-mode-v8-code-range-20261001.md`: a V8 `IsolateGroup::EnsureCodeRange`
failure (`EXC_BREAKPOINT` via `FatalProcessOutOfMemory`), fixed by injecting
`MACWS_JIT_MPROTECT_COMPAT=1` + `MACWS_JIT_FAULT_WRITE_COMPAT=1` only into the
exact `codex-code-mode-host` basename. Related but **not** our failure: theirs is
a `mmap(MAP_JIT)` virtual reservation, ours is `cs_validate_page → KERN_CODESIGN_ERROR`.
Worth noting only because it shows the chroot's **executable-page protections are
a live, separately-hooked surface**; if the W^X adapter ever mangles protections on
a cache mapping it would be worth an A/B with both switches off (**THEORY**).

---

## 3. Host-side RE facts (this Mac *is* macOS 15.6.1 / 24G90 — same build as the target)

Run on the host, read-only. All **RE-confirmed**.

### 3.1 The 24G90 cache is 2 files, and the 2 registered CDHashes are correct

```
/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/
  dyld_shared_cache_arm64e      2,712,764,416 B   (= our analysis copy, byte-size identical)
  dyld_shared_cache_arm64e.01   2,203,500,544 B
```

`codesign -vvv -d dyld_shared_cache_arm64e` (host, 15.6.1/24G90):

```
Identifier=com.apple.dyld.cache.arm64e.development
Format=OS X Shared Library Cache (unknown type)
CodeDirectory v=20400 size=5288224 flags=0x2(adhoc) hashes=165251+2 location=embedded
CDHash=2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e          <-- == misc/postinst.sh 24G90 case
Signature=adhoc
```

`dyld_shared_cache_arm64e.01` has **no embedded signature** (the SuperBlob lives in
the main file). Header parse of the main file:

| Field | Value | Note |
|---|---|---|
| magic | `dyld_v1  arm64e` | |
| uuid | `4c1223e5-cace-3982-a003-6110a7a8a25c` | |
| `cacheType` | **0** | 0 = development, 1 = production, 2 = multi-cache |
| `platform` | 1 | macOS |
| `osVersion` | `0x0f0600` | 15.6.0 |
| `sharedRegionStart` | `0x180000000` | |
| `sharedRegionSize` | `0x12c760000` = **4.687 GiB** | **> iOS arm64 `SHARED_REGION_SIZE_ARM64` (4 GiB)** → confirms the syscall-536 shared-region path is structurally excluded |
| `codeSignatureOffset` / size | `0xa160c000` / `0x50c000` | matches codesign's 5,288,224-byte CodeDirectory |
| `subCacheArrayCount` | **1** | so exactly 2 files total — the "2 CDHashes" registration matches the file count, not a truncation |

### 3.2 Why re-signing is not an option (mechanism, not theory)

The macOS arm64e shared cache is **ad-hoc signed** (`flags=0x2(adhoc)`,
`Signature=adhoc`, identifier carries `.development`). On a real Mac the cache is
trusted because it is mapped from the SSV-sealed system volume, not because of a CMS
signature. In the iOS chroot there is no such seal and no platform trust anchor, so
the *only* way iOS AMFI can admit its pages is the **dynamic trustcache entry for
the cache's own CDHash** plus the embedded CodeDirectory's per-page hashes. This is
the same conclusion the author states in §2.2. **RE-confirmed (host codesign + header).**

---

## 4. Concrete defect found (source-level RE-confirmed): the *runtime* trust gate has no 24G90 branch

- `layout/usr/macOS/bin/postinst.sh:1063-1076` — **has** both cases: `22F82|22F66|""`
  → Ventura pair, and `24G90` → the 15.6.1 pair (`2b9cccd5…`, `8c7ba7e5…`).
- `layout/usr/macOS/bin/macos_gui.sh:1685-1688` — the **cold-boot trust restore that
  runs before every workspace start** passes **only** the Ventura pair:
  ```
  --hash b5da39409492ac85e5a8e8ab618fe77e2d7a2980 \
  --hash bbb765988e2677b98d47a549d612fa0d4af25f69 \
  ```
  with the comment "Exact **Ventura** shared-cache CodeDirectories remain required."
- `layout/usr/macOS/bin/macws_boot_trust.py` discovers CDHashes by parsing **Mach-O**
  `CodeDirectories` (`directory_hash()`, lines 48-126). A dyld shared cache is **not
  a Mach-O** (magic `dyld_v1  arm64e`), so the manifest scan can never pick the
  cache up — the two `--hash` extras are the *only* source.
- The project itself documents Dopamine's dynamic trustcache as **reboot-volatile**
  (`macos_gui.sh:1654-1656`, `docs/runtime-switches.tsv` line 408).

⇒ Chain: on a 15.6.1 rootfs, after any reboot the 15.6.1 cache CDHashes are gone
from the dynamic trustcache, and `macos_gui.sh production` restores only the
*Ventura* hashes — so the macOS 15.6.1 cache pages can no longer be validated at
exec time. This produces exactly the observed shape (`data readable, exec page
denied`, `KERN_CODESIGN_ERROR=50`).

**Honest limits of this finding (must be respected):**

- This is a **source-level RE-confirmed defect**, and it explains a
  "worked right after `postinst`, fails after reboot" pattern. It is **NOT**
  runtime-confirmed as the cause of the specific fault recorded in
  `dyld-15.6.1-state.md` (that note records the pair as *verified present* in the
  trustcache during the tests, and the route-E runs bypassed `macos_gui.sh`).
- Consequence: it is a **candidate root cause for the post-reboot failures**, and
  absolutely worth a 2-minute on-device check before anything else.

**Why earlier sessions still saw the pair "verified present"** (resolves the tension
above): a full-tree grep for the four CDHashes shows that, outside `analysis/` and my
own `misc/post_reboot_cli_test.sh:38`, the only *shipped* registration sites are
`misc/postinst.sh:973-979` and `layout/usr/macOS/bin/postinst.sh:1070-1076` — both
have the `24G90` branch. But `analysis/dyldwork/*.sh` (`next_boot.sh`,
`run_all_cold.sh`, `keeper_start.sh`, `post_reboot_noslide.sh`, `catch_segv.sh`,
`keeper_test.sh`) each call `$JB trustcache add 2b9cccd5… / 8c7ba7e5…` on every cold
start. Those were **session scaffolding**, not the shipped runtime path: they masked
the `macos_gui.sh` gap for every experiment that ran after them. That is exactly why
the state doc can say the hashes were verified present while the shipped cold-start
gate still cannot restore them.

---

## 5. Proposed next steps (for the device-side/neighbouring session)

Ordered cheapest-and-most-decisive first.

1. **Prove or kill §4 in one command pair.** After a reboot, *before* any chroot
   start: `jbctl trustcache info | grep -i -e 2b9cccd5 -e 8c7ba7e5`. Then run
   `macos_gui.sh production` and re-check. If the pair is absent pre-start and
   absent post-start, §4 is the (or *a*) root cause; if absent pre-start but
   present post-start, §4 is a real bug but not the current failure.
   (`jbctl trustcache info` prints **UPPERCASE** hex — the grep must be `-i`;
   state doc item 4.)
2. **Candidate fix for §4** (small, mirrors the proven `postinst.sh` logic — read
   the build from `SystemVersion.plist` and choose the pair). Not applied in this
   session: it is a boot-critical trust gate, and the project's own discipline
   requires a device-side acceptance witness before shipping. A `--hash` pair
   selected like `postinst.sh:1066-1078` is the minimal change; the existing
   `misc/test_restore_boot_contract.py` is the natural place for a regression gate.
3. **Then** the already-prepared decisive experiment from
   `docs/porting/dyld-15.6.1-state.md` (2026-10-02 entry): `RUN_DBG_HOLD=30
   run_dbg_hold_v2` to freeze the child at the codesign exception and
   `misc/csprobe.py <pid>` to read `proc→fd→vnode(+0x78)→ubc_info` and check
   (a) whether the faulting `m`'s owner object has `code_signed`
   (`vm_object+0xac` bit 8) and (b) whether the blob's `base/start/end`
   (`+0x28/+0x30/+0x38`) actually cover `page_offset ≈ 0x47c000`.
4. Use `MACWS_LLDB_HOLD_AFTER_INIT=1` (+ `MACWS_SUSPEND_TARGET`) for the per-PID
   triage of `run_bash.sh -c "echo hi"` instead of `MACWS_SUSPEND_AT_EXEC`.

---

## 6. Reply prompt for the neighbouring agent

```text
你是 macPad 项目的「设备侧」Agent 的接棒者。仓库：/Users/ciscohe/Desktop/macPad。

【背景】
静态侧刚把上游 7 个新 commit merge 进本 fork（merge commit cc56e15，已 push，
`git rev-list --count main..upstream/main` = 0）。上游这 7 个 commit 全是
GUI/输入/窗口（floating Dock、Magic Keyboard IME、虚拟键盘工具栏、Codex Code
Mode V8、Terminal tab/Dock），**没有任何 dyld / shared-cache / 15.6.1 内容**。
唯一实质变更是 AGENTS.md 的刷新（新增
"Current Project Memory and Operating Baseline (2026-10-01)" 一节，并导入了原
agent memory 的 ledger）。merge 只有一个冲突（AGENTS.md 两侧在同一位置各插一节），
已两边都保留。

【请先读这些（按序）】
1. AGENTS.md 顶部的 "Current Project Memory and Operating Baseline (2026-10-01)"
   —— 作者 2026-10-01 的权威现状声明（平台矩阵、架构、agent workflow）。
2. docs/porting/UPSTREAM-MERGE-AND-CLUES-2026-10-02.md —— 静态侧本次的 merge +
   线索挖掘报告（含下面所有结论的出处与证据档位）。
3. docs/porting/dyld-15.6.1-state.md 最后一条 (2026-10-02 KERN_CODESIGN_ERROR=50 收窄)。
4. layout/DEBIAN/postinst 的 83-88 行 与 437-445 行 —— 作者本人对
   "shared-cache 只能靠 dynamic trustcache 准入" 的 runtime-confirmed 陈述。

【本轮的 4 个关键线索（详见报告第 2/3 节）】
A. 作者在 layout/DEBIAN/postinst:83-88 明确写：缓存是 "patched macOS
   dyld/shared-cache images whose **fixed CodeDirectories cannot be made
   admissible by CoreTrust-signing ordinary Mach-O files**"，所以 "a full
   jailbreak trustcache backend is therefore structural"。→ 我们的
   KERN_CODESIGN_ERROR=50 只能走 trustcache 路线，重签缓存文件不可能。
B. 主机的 15.6.1/24G90 缓存实测（RE-confirmed）：`codesign -d` 得到
   Identifier=com.apple.dyld.cache.arm64e.development、flags=0x2(adhoc)、
   CDHash=2b9cccd5…（与 postinst 24G90 分支一致）；header 解析得
   cacheType=0(development)、platform=1(macOS)、sharedRegionSize=0x12c760000
   (4.687 GiB > iOS arm64 共享区 4 GiB)、subCacheArrayCount=1（共 2 个文件）。
   缓存是 **ad-hoc 签名**，没有 CMS/Apple 平台签名 —— 这正是"只能靠
   trustcache 准入"的机制原因。
C. **静态侧发现的源码级缺陷（RE-confirmed，但未 runtime 确证为本轮根因）**：
   layout/usr/macOS/bin/postinst.sh:1063-1076 有 24G90 分支（注册
   2b9cccd5…/8c7ba7e5…），但 **layout/usr/macOS/bin/macos_gui.sh:1685-1688**
   （每次冷启动 workspace 前跑的 trust restore）**只传 Ventura 13.4 的那一对
   --hash**，没有 24G90 分支。而 macws_boot_trust.py 只能从 Mach-O 扫出
   CodeDirectory（缓存不是 Mach-O，永远扫不到），项目自己也记录 Dopamine 的
   dynamic trustcache 是 reboot-volatile。→ 15.6.1 rootfs 每次重启后缓存的两个
   CDHash 都不会被恢复，症状正好是 "数据页可读、执行页被拒
   (KERN_CODESIGN_ERROR=50)"。这只解释了 "postinst 后能跑、重启后跑不了"，
   不能解释 state doc 里"trustcache 已验证存在"时的那次失败。
D. 新诊断开关：MACWS_LLDB_HOLD_AFTER_INIT（+ MACWS_SUSPEND_TARGET）——
   在 libmachook 装完 interpose 与 dyld image callback 之后停下。比
   MACWS_SUSPEND_AT_EXEC 更适合 `run_bash.sh -c "echo hi"` 的 per-PID triage。
   另外部署请走 misc/device_pipeline.sh（内容校验），不要就地 scp 覆盖已签名
   dylib（会 stale vnode → AMFI Invalid Page）。

【请按这个顺序做（最便宜、最有决定性优先）】
1. 用两条命令证实/证伪 C：重启后、任何 chroot 启动之前
   `jbctl trustcache info | grep -i -e 2b9cccd5 -e 8c7ba7e5`；再跑
   `macos_gui.sh production`，再查一次。注意 jbctl 输出是 **大写十六进制**，
   grep 必须加 -i；`jbctl trustcache add` 有时静默失败，务必复核。
2. 若证实：给 macos_gui.sh 加 24G90 分支（照抄 postinst.sh:1066-1078 的
   "读 SystemVersion.plist 的 ProductBuildVersion 再选哈希对" 逻辑），
   并在 misc/test_restore_boot_contract.py 里加回归断言。改动属 boot-critical
   trust gate，**必须先有设备侧可见/协议见证再算完成**。
3. 然后做 state doc 里已备好的决定性实验：RUN_DBG_HOLD=30 run_dbg_hold_v2
   把孩子冻结在 codesign 异常，misc/csprobe.py <pid> 读
   proc→fd→vnode(+0x78)→ubc_info，确认 (a) 触发 fault 的 m 的属主对象是否
   code_signed（vm_object+0xac bit8），(b) blob 的 base/start/end
   (+0x28/+0x30/+0x38) 是否真的覆盖 page_offset≈0x47c000。
4. 记录纪律不变：每步都要写进 docs/porting/dyld-15.6.1-state.md（含逐字 log/命令/
   档位标注），removal 只允许移到 ~/.Trash。

【约束】中文回复；证据驱动（RE-confirmed / runtime-confirmed / THEORY 三档）；
先读文档与源码再动手；commit 后立即 git push origin main；动设备前先按"串行"
约定与用户确认。
```

# Kernel RE Finding — `sysent[536]` / `shared_region_map_and_slide_2_np` return 40

Target: `/private/tmp/kc_raw.bin` (iPadOS 16.3 / T8112 kernelcache), IDB
`/private/tmp/kc_raw.bin.i64`, imagebase `0xfffffe0007004000`, analyzed via
**ida-pro-mcp-Instance2** (port 13338). Every address below is a kernelcache VA
and every claim is labeled RE-confirmed (decompile/disasm from this IDB) or
INFERRED.

This supersedes the "Remaining unknowns" in
`docs/porting/kernel-syscall536-re-handover.md`.

---

## TL;DR — conclusion chain

The `#536` handler cannot emit 40 itself (its own exits are {0,5,6,12,14,22}).
The value can only arrive by **passthrough** through `shared_region_map_and_slide_setup`
(`sub_8459570`). Within that function the only unbounded (arbitrary-errno)
sources are:

| passthrough source | proven return set | can it be 40? |
|---|---|---|
| `sub_8377D54` (fd → fileglob) | `{0, 9}` (EBADF); other paths are `os_refcnt` / table-lookup **noreturn** panics | **NO** |
| `vnode_getwithref` (0x80e44f8) → `sub_80DD088` | `{0, 2, 19}` ∪ `sub_83C2DD4` lock-wait result | practically no |
| `mac_file_check_mmap` (`sub_867A738`) | `{0,1}` (AMFI) ∪ `{sandbox eval}` | **only non-{0,1} source** |
| `vnode_getattr` (0x81135e4) | `{0, 12, 22}` ∪ FS `VNOP_GETATTR` result | opaque (filesystem) |

**No `mpo_file_check_mmap` hook returns a literal 40.** AMFI's hook
(`_file_check_mmap`) is capped at **1 (EPERM)**; AppleImage4 does not implement
it; the only hook that can return a value outside `{0,1}` is the **Sandbox**
hook, which returns the sandbox evaluation of operation **`file-map-executable`**
for any mapped vnode that lacks the **VSHAREDCACHE** vnode flag.

→ The 40 is produced by the **Sandbox `mpo_file_check_mmap`**
(`hook_file_check_mmap` @ **0xa659664**, sandbox `mac_policy_ops`+0x120), whose
return is `cred_sb_evaluate(cred, 16 /*file-map-executable*/, …)`, and which
funnels into the BSD syscall return **verbatim** as an errno through
`mac_file_check_mmap` → `sub_8459570` → `sub_8459134`.

---

## 1. sysent indexing (confirmed for #536 and #294)

`sysent` @ `0x7999680`, 24-byte entries (`+0x00` munge, `+0x08` flags/narg,
`+0x10` sy_call):

- `sysent[536]` @ `0x799c8c0`: munge = `0x80b5f4c`, sy_call = **`0x8459134`**.
  - `0x80b5f4c` is a bare `RET` no-op ("nullsub") — it does NOT return an errno
    (verified: `idc` disasm `fffffe00080b5f4c RET`). So 40 is not from the munger.
- `sysent[294]` @ `0x799b210`: munge = 0, sy_call = **`0x8459024`**.

Handler `0x8459024` (`check_np`) decompiled → returns only `{0, 12, 22, copyout_ret}`
(copyout → EFAULT). Confirms the table index and that the sibling syscall is not
the producer. (subtask 4 ✓)

## 2. Handler → setup (single unbounded path)

`sub_8459134` (wrapper): its own exits are `{0,5,6,14,22}` plus
`v9 = v30 = sub_8459570(...)` on the setup-failure branch. On the success branch
it maps `sub_8061EF0`'s return through a jump table to `{0,0,1,12,14}` (any value
>3 → 22), so `sub_8061EF0` cannot smuggle 40 either (subtask 5 ✓ — it returns
`sub_80623D4` / `sub_8062CA8` / `sub_806172C` results and `0`, all clamped).

`sub_8459570` (setup) returns `v25`, which is assigned only from:
`22`, `1`, `0`, `12`, and the four passthroughs in the TL;DR table.

## 3. `mac_file_check_mmap` (`sub_867A738`) — RE-confirmed

Disassembly (0x867a78c … 0x867a994) shows **two** policy-list loops
(counts @ `0x79e175c` and @ `0x79e1758`, entry-array @ `0x79e1768`). For each:

```
policy = list[i];  if (!policy) continue;
ops  = *(policy + 0x20);            // mac_policy_conf.mpc_ops  (conf+0x20)
hook = *(ops + 0x120);              // mpo_file_check_mmap
if (!hook) continue;
w0 = hook(cred, fg, /*vnode=*/0, flags, prot, offset, &maxprot);   // BLRAA
```

Aggregation priority (verbatim constants): `0xB(11) > 0x16(22) > 3 > 2 > 0xD(13) > 1`,
else `CSEL W25, W25, W0, EQ` → **last non-zero wins**. So a hook returning 40
would survive unless another hook returns one of {1,2,3,11,13,22}.

The call from setup is `mac_file_check_mmap(cred, fg, /*flags=*/7, /*prot=*/18,
/*offset=*/0, &maxprot)`, hence the hook receives `(cred, fg, 0, 7, 18, 0, &maxprot)`.

## 4. The three `mac_policy_ops` tables (handover's AMFI candidate is WRONG)

Only **three** kexts ever call `_mac_policy_register` (BL target `0x865b770`;
scanned every `__text`):

| kext | register site | conf | `mpc_ops` | ops base | `ops+0x120` |
|---|---|---|---|---|---|
| AMFI | `0x92a349c` | `mac_policy` @ `0x7bac090` | `0x7bac0b0` | **`0x7bab618`** (`mac_ops`) | **`0x7bab738`** = `_file_check_mmap` @ `0x92a1a90` |
| Sandbox | `0xa64c208` | `_policy_conf` @ `0x7e3fb20` | `0x7e3fb40` | **`0x7e3fb80`** | **`0x7e3fca0`** = `hook_file_check_mmap` @ `0xa659664` |
| AppleImage4 | `0x9169b30` | `___policy_conf` @ `0x7b70530` | `0x7b70550` | `0x7b70580` | **`0x0` (NULL)** |

RE evidence: `_initializeAppleMobileFileIntegrity` (`0x92a3044`) writes
`mpc_ops = &mac_ops` (`qword_FFFFFE0007BAC0B0 = &mac_ops`, and `__ZL7mac_ops` =
`0x7bab618`); `_policy_conf` resolves to `0x7e3fb20` with name `"Sandbox"` /
fullname `"Seatbelt sandbox policy"`. **The handover's candidate `0x7bacbe8` is not
a policy table** (its `+0x120` lands in unrelated AMFI data). CoreTrust registers
no MAC policy at all.

### AMFI `mpo_file_check_mmap` = `_file_check_mmap` @ `0x92a1a90` (RE-confirmed)

```c
proc *v11 = current_proc();
int   v12 = cs_require_lv();
if ((a4 & 4) != 0 && v12) {                       // a4 = flags (=7 → bit2 set)
    if ((library_validation(v11, a2, a6, 0, 0) & 1) == 0) return 1;   // EPERM
} else if (v12) {
    *a7 &= ~4u;
}
return 0;
```
→ **returns only {0,1}. It can never yield 40.**

### Sandbox `mpo_file_check_mmap` = `hook_file_check_mmap` @ `0xa659664` (RE-confirmed)

Disasm (0xa65968c …):
```
TBZ  W3, #2, .return0        ; if (flags & 4)==0 → return 0
X0 = fg → fg_get_vnode       ; if (vnode == NULL) → return 0
BL vnode_isdyldsharedcache   ; = (vp->v_flag >> 9) & 1   (LDR W8,[X0,#0x54]; UBFX #9,#1)
CBZ W0, .evaluate            ; if NOT shared-cache → evaluate
.return0: return 0
.evaluate:  args = { .type=1, .vnode = vnode, cred attached by cred_sb_evaluate }
            return cred_sb_evaluate(cred, /*op=*/0x10 /*16*/, args)
```
`cred_sb_evaluate` (`0xa64bf04`) → `sb_evaluate_internal` (`0xa65bca0`) →
`eval_op` (`0xa65c180`) → `eval` (`0xa65c24c`), and the result's **low 32 bits**
is what `mac_file_check_mmap` consumes as the errno.

`operation_names.502[16]` = **`b"file-map-executable"`** (read from
`0x7e40880 + 16*8`).

## 5. Literal-40 scan — nothing in the reachable path (subtasks 1 & 3)

- `com.apple.kernel:__text` (0x7f18000..0x86b8000): **552** `MOV Wn,#0x28`
  requests (`word & 0xffffffe0 == 0x52800500`). A BFS from every passthrough /
  hook root over BL edges (2360 functions visited) finds **no** such constant on a
  function whose value is used as an errno. The three shared-region-adjacent hits
  are false positives: `sub_8454840` (=`vnode_pageout`, 0x84549ac etc.) and
  `sub_845DB4C` (AppleImage4 sysctl hook) use 40 as a **struct stride / UPL size**,
  e.g. `sub_804F0F8(..., 0x4000, 40, &…)` and `*(q + 40*idx)`.
- AMFI literal-40: `_proc_check_launch_constraints` (`0x929ff20`),
  `_validateImage4` (`0x92a9e48/0x92a9ee0/0x92a9f10/0x92a9f74`) — unrelated hooks.
- Sandbox literal-40: `sub_A6551A4`, `sub_A656DDC`, `_hook_policy_init`,
  `sub_A66A828`, `_protobox_register`, `___evaluate_and_collect_mach_message_filters_block_invoke`
  — all init / mach-message paths, none in the file-mmap path.
- `_file_check_mmap`, `hook_file_check_mmap`, `cred_sb_evaluate`,
  `sb_evaluate_internal` and `eval` contain **no** literal errno 40. `eval`'s
  returned low dword is data-driven (`MOV W25,#{0,1,0x1000}` plus
  `LDR W25,[…]` from the compiled profile; its "invalid" path returns
  `0x500000001` → low dword **1 = EPERM**).
- The other opaque source — `vnode_getattr`'s `VNOP_GETATTR` (`vnop_getattr_desc`
  = index `0`) — was closed too: enumerating every filesystem getattr
  (`_apfs_vnop_getattr` 0xa6b6e14, `_apfs_snap_vnop_getattr`, `_apfs_fake_vnop_getattr`,
  `_hfs_vnop_getattr` 0x9ad6238, `_lifs_vnop_getattr`, `_tmpfs_getattr`, `_vfs_getattr`,
  …) shows **none** carries a literal 40; `_apfs_vnop_getattr` returns only `{4}` and
  `_hfs_vnop_getattr` only `{2,12}`.

**Therefore a raw 40 is not a compiled constant anywhere on this path.** The
only site that can hand a value >1 to the syscall return is the Sandbox hook.

## 6. Semantic conclusion (EMSGSIZE vs KERN_LOCK_OWNED)

The BSD syscall returns `W0` directly as errno (via `cerror`), so **40 = EMSGSIZE**.
`KERN_LOCK_OWNED == 40` would only appear if a `kern_return_t` leaked; the only
lock-wait in the chain (`sub_83C2DD4`, reached from `vnode_getwithref`) is not a
stable, input-deterministic site, and the probe is stable 6/6 — so EMSGSIZE is the
intended reading.

## 7. Is the check satisfiable for a foreign macOS cache?

RE-confirmed gates on this path and whether our file (root-owned Apple macOS
`dyld_shared_cache_arm64e`, on `/var/mnt/rootfs`, opened `O_RDONLY`) satisfies them:

| gate (address) | emits | foreign cache satisfies? |
|---|---|---|
| mount == rootdir mount or `…/Cryptexes` mount (`sub_8459570`) | EPERM(1) | yes, if cache sits on the chroot rootdir volume |
| `vap.va_uid == 0` | EPERM(1) | yes (Apple file owned by root) |
| `ubc_cs_blob` covers mapped ranges | EINVAL(22) | yes if the cache carries its code-signature blob |
| **AMFI `_file_check_mmap` (`0x92a1a90`)** | **1** | yes (returns ≤1) |
| **Sandbox `hook_file_check_mmap` (`0xa659664`) → `cred_sb_evaluate(op 16)**` | **sandbox errno** | **only if the vnode is flagged `VSHAREDCACHE` (vp+0x54 bit 9)** |

The **only** gate that can return a value other than EPERM/EINVAL is the Sandbox
one, and its allow-shortcut is the `VSHAREDCACHE` vnode flag — which the kernel
sets for a shared cache it *recognises and maps*, never for a foreign macOS cache
loaded simply from a file. Hence a foreign cache cannot satisfy it; the sandbox
falls through to the `file-map-executable` evaluation and returns its (deny)
errno.

## 8. Recommended runtime confirmation (one counter / one gate)

Print, at the points below, the exact value on a chrooted WS run:

- `hook_file_check_mmap` @ `0xa659664` return (and which branch: `!VSHAREDCACHE` @
  `0xa6596a8` vs early `0` @ `0xa6596ac`).
- `cred_sb_evaluate` @ `0xa64bf04` / `sb_evaluate_internal` @ `0xa65bca0` return
  (low 32 = the errno that lands in the syscall return).
- `_file_check_mmap` @ `0x92a1a90` return.
- `vnode_getattr` @ `0x81135e4` `VNOP_GETATTR` return (to close the last opaque
  source).

If `hook_file_check_mmap` returns 40, the chain is closed:
`0xa659664 → 0xa64bf04 → 0xa65bca0 → 0xa65c24c` → `sub_867A738` @ `0x867a7e8`
aggregation → `sub_8459570` `v9=v32` @ `0x8459a08` → `sub_8459134` `v9=v30`
@ `0x8459488` → errno 40.

## 9. Verification status (evidence levels)

- **RE-confirmed (decompile/disasm, this IDB):** every address above; the ops
  tables (`mac_ops` 0x7bab618, sandbox ops 0x7e3fb80, AppleImage4 ops 0x7b70580);
  `ops+0x120` hooks; AMFI `_file_check_mmap` return set `{0,1}`; Sandbox
  `hook_file_check_mmap` gate `(flags&4) && !VSHAREDCACHE` → `cred_sb_evaluate`;
  the aggregation semantics in `sub_867A738`; the absence of any literal-40 on
  the reachable path (sysent/handler/engine/hooks/VNOP all scanned).
- **Runtime-confirmed (handover, exit-probe 6/6):** `#536` returns raw W0 = 40
  (EMSGSIZE) at dyld thin offset 0x35698.
- **Deduced (combination of the two):** since the only value >1 the syscall can
  return is the Sandbox `file-map-executable` evaluation of a non-VSHAREDCACHE
  vnode, **that evaluation is the producer of 40**. The numeric itself is the
  sandbox profile's deny code (compiled data in the sandbox kext), which is why a
  byte scan for `MOV Wn,#0x28` finds nothing: there is no 40 literal.
- **Open (runtime data, not statically derivable):** the exact errno *value* the
  sandbox returns for op 16. `platform_profile` @ `0x7e41568` (the profile
  `sb_evaluate_internal` always evaluates against) is **all-zero in the static
  image**; `get_entry_point_for_operation` (`0xa65c1f0`) reads the op table from
  `*(profile+24)` + `2*op` inside a runtime data buffer `*(profile+32)`. So the
  decision code is compiled profile data, not a kernel constant — this is exactly
  why the `MOV Wn,#0x28` scan is empty. One runtime read of
  `hook_file_check_mmap`/`cred_sb_evaluate`'s return (§8) yields the value.

## 10. Method notes / traps for the next reader

- Use **ida-pro-mcp-Instance2** (port 13338); Instance1 was never touched.
- `idc.get_qword` is fixup-decoded; `mpc_ops` sits at `mac_policy_conf+0x20`, and
  `mpo_file_check_mmap` at `mac_policy_ops+0x120` (both RE-confirmed from
  `sub_867A738` and from the AMFI initializer that fills `mac_ops`).
- Do **not** trust the handover's AMFI ops base `0x7bacbe8`; the real table is
  `mac_ops` @ `0x7bab618`.
- `MOV Wn,#0x28` is overwhelmingly a **size/stride** (40 is `sizeof` of several
  kernel structs), not an errno — filter by "is the value returned" before
  concluding.

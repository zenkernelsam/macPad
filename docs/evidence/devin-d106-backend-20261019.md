# D106 — exact libquarantine getter: request construction and errno propagation

Offline exact-binary audit only. No device, probes, TCC/setters, KRW, source,
build, or Git. Raw disasm artifact:
`analysis/libquarantine-d106/getter-0x18b320b00-helper-0x18b320b2c-stub-0x18b321bc0.txt`
(sha256 `9b9c0ba63de416deb62fb1697490057253511fa05ee11d1f15a7a2c3e6acc068`).

## 1. Image identity — validated

| Field | Value |
|---|---|
| File | `analysis/r005-responsibility-inputs/libquarantine.dylib` (1.35 GB sparse extraction, `__TEXT` intact at fileoff 0) |
| sha256 (re-verified) | `f5989c8f4afc9985e95795bf683679eed629af8b5ae288a0fb87317ba0e844f2` |
| UUID (re-verified) | `A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3` — matches R004's live `dladdr` (chroot base `0x1a6d5f000`, symbol `+0x1b00` → unslid `0x18b320b00`) |
| `__TEXT` | vmaddr `0x18b31f000`, fileoff 0, filesize `0x3000` |
| NOT this image | `analysis/device-leftovers-20261006/…/libquarantine.dylib` is UUID `8CE8DAD0-…` (macOS 15.6, 36 KB) — version-adjacent only, never cited as 22F82 fact |

## 2. Request construction — verbatim

Getter `0x18b320b00` (`responsibility_get_responsible_audit_token_for_audit_token`):

```text
18b320b00  7f2303d5  pacibsp
18b320b10  000440ad  ldp q0,q1,[x0]     ; copy caller's 32-byte audit_token_t
18b320b14  e00700ad  stp q0,q1,[sp]
18b320b18  e0030091  mov x0,sp          ; x0 = local copy of token
18b320b1c  04000094  bl  0x18b320b2c    ; helper
18b320b28  ff0f5fd6  retab              ; returns helper's w0 verbatim
```

Helper `0x18b320b2c` (arg struct built at `sp+0x10`, `x2` → it):

```text
18b320b40  f30302aa  mov x19,x2         ; optional out2 ptr (tccd passes 0)
18b320b70  081440b9  ldr w8,[x0,#0x14]  ; token.val[5] = pid
18b320b74  091c40b9  ldr w9,[x0,#0x1c]  ; token.val[7] = pidversion
18b320b78  e2a300a9  stp x2,x8,[sp,#8]  ; [sp+8]=x2(0), [sp+0x10]=pid
18b320b7c  e98701a9  stp x9,x1,[sp,#0x18] ; [sp+0x18]=pidversion, [sp+0x20]=out-token ptr
18b320b80  e8230091  add x8,sp,#8
18b320b84  e88f02a9  stp x8,x3,[sp,#0x28] ; [sp+0x28]=&sp+8, [sp+0x30]=x3(0)
18b320b88..b8c  adrp+add x0 = 0x18b321e0b   ; "Quarantine" (fileoff 0x2e0b, bytes verified: 51 75 61 72 61 6e 74 69 6e 65 00)
18b320b90  e2430091  add x2,sp,#0x10    ; arg struct
18b320b94  c1168052  mov w1,#0xb6       ; selector 182 — CONFIRMED on exact image
18b320b98  0a040094  bl  0x18b321bc0    ; __auth_stubs policy entry
18b320b9c  930000b4  cbz x19,0x18b320bac ; x2==0 → skip
18b320ba0  60000035  cbnz w0,0x18b320bac ; or error → skip
18b320ba4..ba8  [sp+8] -> *x19          ; unreachable on tccd path (x2=0)
```

Arg-struct layout (at `sp+0x10`, passed as x2 to the stub):

| Offset | Content | Source |
|---|---|---|
| +0x00 | pid (32-bit) | input token `val[5]` @ +0x14 |
| +0x08 | pidversion | input token `val[7]` @ +0x1c |
| +0x10 | out audit_token ptr (32B, caller-owned) | caller's x1 |
| +0x18 | pointer to a zero qword `[sp+8]` | x2 copied into `[sp+8]` first |
| +0x20 | x3 | caller's x3 (tccd: 0) |

So the wire request carries **pid + pidversion + out-token slot + flag ptr +
x3** — the kernel sees extracted scalar fields, not the raw 32-byte input
token; the output token is filled through the caller-owned pointer on success
(R004 observed all-zero on failure).

## 3. Return / errno path — propagation, not synthesis (on this path)

- Getter and helper return the stub's `w0` **unchanged**; there is **no
  `__error` call and no errno write anywhere in `0x18b320b00–0x18b320bb8`**.
- Full `__TEXT` sweep: **zero `mov w*,#0x67` (103) in the entire image**. The
  only locally synthesized errnos are `0x22` (ERANGE), `0x16` (EINVAL), `0xc`
  (ENOMEM-class) in other wrappers.
- Synthesis machinery does exist in-image — adjacent
  `responsibility_get_responsible_for_pid` `0x18b320bbc` has a null-check arm
  doing `bl 0x18b321ba0` (`__error` stub) → `mov w8,#0x22` → `str w8,[x0]` →
  `w0=-1` — but the `0xb6` getter path contains no such arm.
- **Therefore**: tccd's observed `errno=103` is produced *inside the
  unresolved stub provider* (or synthesized in the same provider's
  error-return path), then propagated verbatim through helper → getter →
  tccd. libquarantine contributes nothing to the value.

## 4. Stub/provider identity — UNRESOLVED (by construction)

`0x18b321bc0` is an authenticated GOT indirect branch:

```text
18b321bc0  f02a5eb1  adrp x17, ...
18b321bc4  913e2231  add  x17,x17,#0xf88
18b321bc8  f9400230  ldr  x16,[x17]
18b321bcc  d71f0a11  braa x16,x17
```

GOT slot `0x1dfef8f88` lies **outside every sparse segment** of this
extraction (`__TEXT` 0x18b31f000+0x3000; `__DATA_CONST` 0x1d41ac958+80;
`__AUTH_CONST` 0x1dd78c558+272; `__DATA` 0x1d8ca4fe0+16). No
`LC_DYLD_INFO`/`LC_DYLD_CHAINED_FIXUPS`; symtab/dysymtab empty. The provider
name **cannot be resolved from this image**. "It is `__mac_syscall`" remains
version-adjacent THEORY only (15.6 `8CE8DAD0` shows `___sandbox_ms` aliased to
`___mac_syscall`); not asserted as 22F82 fact. Missing bytes (8-byte slot +
fixup record) are not guessed.

## 5. Kernel-branch uncertainty — preserved

Even accepting the `__mac_syscall` theory, xnu (adjacent version) shows three
distinct ENOPOLICY producers: (a) `mac_proc_check_mac_syscall` precheck;
(b) post-scan `error = ENOPOLICY` default when no `mpc_name` matches;
(c) a matched `mpo_policy_syscall` returning ENOPOLICY itself. The observed
`#103` does **not** select among them. The v3 `mac_policy_list` snapshot
(frozen, hash `8bb90399…`, not executed) remains the only planned
discriminator for (b); (a)/(c) would additionally need kernel-side witnesses
not currently available.

D105's historical causality is **preserved, not expanded**: msgids
`48891.17`/`.19` (WS 48891 → tccd 48750, post-remap) → `from PID[target]:
(#103)` → `count=0` → `sls_rc=1002` is msgID-correlated fact for those
targets. Current-generation (WS 10447) first-miss behavior remains
unobserved; its posts to date are cached denies.

## 6. D105 chronology/provenance — verified

`coordination/artifacts/R003-tccd-key-chains-20261018.txt` is a **74-line
curated excerpt**, not a raw transaction dump — its own header states: "Raw
oslog (13 MB) archived on device:
`/var/mobile/macws_r003_evidence_20261018/r003-oslog.txt`". The raw log is
**not locally available**; lines between the excerpted chains cannot be
locally re-verified. D105's quotations were verbatim from this excerpt (no
invented content); the excerpt's own limits section states pidversion is not
printed and correlation is msgID + target pid + tccd generation PID — both
carried into D105 unchanged.

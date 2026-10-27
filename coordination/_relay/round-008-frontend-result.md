# Round 008 — frontend result: libquarantine getter and the 103 branch

Date: 2026-10-18. Read-only offline analysis; no device/SSH, source/test/build
change, build or Git write. Added only this result, the dated evidence, and
`coordination/_relay/round-008-required-inputs.json`.

Candidate HEAD: `7e5d771`. Backend R004's live generation was untouched.

## Input status

The exact Ventura image is **not local**. `analysis/r004-responsibility`
contains only metadata/probe source; the local
`analysis/device-leftovers…/libquarantine.dylib` is macOS 15.6 UUID
`8CE8DAD0-52D6-3F46-98EF-161E1397FB48` (SHA `fd20c715…`), not UUID
`A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3`. The iOS cache has no libquarantine.
The minimal manifest is `coordination/_relay/round-008-required-inputs.json`.

## What the local 15.6 binary proves (version-adjacent, not 22F82)

- Getter `0x18da94750` copies the 32-byte input token and calls
  `___qtn_syscall_responsibility_get3` at `+0x2c`.
- Helper `0x18da9477c` builds a struct from token pid/pidversion and the out
  pointer, then calls the `__auth_stubs` entry at `0x18da95b2c`. The indirect
  nlist index 116 resolves to **`___sandbox_ms`**; the string at file
  `0x2dd4` is **`Quarantine`**; selector `w1 = 0xb6` (182).
- The helper has no local `ENOPOLICY` assignment; it returns the syscall
  status/errno unchanged. In the same 15.6 libsystem_kernel,
  `___sandbox_ms` and `___mac_syscall` are the **same address**
  `0x1804335e8`.

## The concrete 103 branch (local XNU source)

`analysis/xnu-xnu-8792.81.2/security/mac_base.c` (`__mac_syscall`, lines
1567-1600; device kernel is `xnu-8792.82.2`, adjacent patch version):

```c
error = mac_proc_check_mac_syscall(p, target, uap->call);
if (error) return error;
error = ENOPOLICY;                       /* line 1587 */
for (i = 0; i < mac_policy_list.staticmax; i++) {
    mpc = ...;
    if (mpc == NULL) continue;
    if (strcmp(mpc->mpc_name, target) == 0 &&
        mpc->mpc_ops->mpo_policy_syscall != NULL) {
        error = mpc->mpc_ops->mpo_policy_syscall(p, uap->call, uap->arg);
        goto done;
    }
}
return error;   /* 103 if no matching policy/handler */
```

`ENOPOLICY` is 103; `strerror(103)` is `Policy not found`. The observed 103
therefore means `mac_proc_check_mac_syscall` passed and the **MAC policy
dispatch found no `Quarantine` policy with `mpo_policy_syscall`** — not a
per-process responsibility-relationship lookup. This corrects the prior
“missing relationship” reading; it is a **policy-dispatch contract** failure.

## Exact Ventura portion

The R004 32-byte prologue decodes to the same wrapper shape and `bl 0x1b2c`
(base+0x2c), matching the 15.6 helper offset. The exact Ventura helper body,
selector/struct and `__sandbox_ms` binding are missing; the 15.6
`Quarantine`/`0xb6` must not be written as 22F82 fact. The manifest requests
only that image/range (owner cache, image UUID, mapped ranges/subcache, bytes
needed) — no whole-cache copy.

## Native vs Ventura metadata

R004's `tccd-22F82-arm64e` is the **Ventura** tccd; `(from libSystem)` is the
link-time umbrella, not the implementation. The chroot `dladdr` proves the
Ventura provider is libquarantine UUID A2D17FF6. Native iOS tccd is a
different executable/UUID; its import metadata is not established by this,
and libquarantine is absent from the iOS cache. A non-platform helper's
`dlsym`/flat-import failure is an availability boundary, not proof the native
daemon lacks the function.

## Narrowest correction candidate / experiment

- If the exact Ventura helper also calls `__mac_syscall("Quarantine", …)` and
  the iOS kernel has no such policy handler, this is a **platform policy
  contract gap**; userland cannot register a MAC policy. Use a genuine
  alternative responsibility query if one exists, or change the
  provider/environment. Do not patch libquarantine, fabricate tokens, use a
  setter, or bypass TCC.
- If the exact Ventura selector/struct maps to an **actually registered**
  iOS policy under another name, a narrow ABI-preserving adapter is a
  conditional candidate — only after exact bytes.
- Discriminating experiment (backend): extract the exact image; confirm the
  helper/policy/selector; call all responsibility getters read-only and
  compare raw return/errno/output; check the device MAC policy list for a
  `Quarantine` handler without kernel writes; tie native tccd UUID/hash
  before any native comparison. No entitlement expansion.

## Commands / exits

`otool -l`/`vtool`, `llvm-objdump -d`, Python nlist/string reads, `nm -a` on
15.6 libsystem_kernel, XNU `sed` inspection, `sha256sum` and `git log` all
exited 0. No test/build/package/device command; no commit/reset/stash/push.

Full detail: `docs/evidence/r008-libquarantine-103-branch-20261018.md`.
Required input: `coordination/_relay/round-008-required-inputs.json`.

(handover to codex now)

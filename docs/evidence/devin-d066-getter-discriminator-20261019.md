# D066 — Consolidated getter ABI + safe discriminator design

Date: 2026-10-19. Backend. **Local read-only; zero device operations.** No MAC
syscalls, no getter tests, no TCC, no setters, no scans beyond targeted lookups.

## 0. Corrections (history preserved in prior docs)

- **D065 overclaim retracted**: "global input unreachable by construction/by
  role contract" was wrong. The role analysis only says a *user-domain*
  instance can't serve `isPerSystem` services — the system endpoint **exists
  and is live** (`com.apple.macosbooter.tccd.system`, `active=1`). If the
  provider hypothesis were false, the current route could produce verdicts.
  Correct statement: input/display services are system-role-only AND the only
  live system endpoint answers #103-class empties — under the provider
  hypothesis. Global input is blocked at the attribution stage on an existing
  endpoint, not by role absence.
- **My D064/D065 `__mac_syscall("Sandbox",0xb6)` A/B proposal — withdrawn as
  unsafe**: selector semantics are policy-specific; `0xb6` on a registered
  Sandbox policy is an unknown operation and could be mutating. Same objection
  to probing `"Quarantine"` with alternate selectors (0xb4–0xbb): safe only if
  the policy is absent — circular. Neither is justifiable as read-only.

## 1. Consolidated getter ABI — verified evidence (R004 + R009)

Exact Ventura libquarantine (UUID `A2D17FF6`, `__TEXT` vmaddr `0x18b31f000`):

- tccd callsite `0x10004fb48`: `bl` getter; `x0`=&sender audit_token,
  `x1`=&out token (32B), `x2=x3=0`; returns int; errno via `__error`
  (`tccd 0x100051a54` logs `errno+strerror` → `(#103) Policy not found`).
- Getter `0x18b320b00` → helper `0x18b320b2c`: packs syscall arg
  `{pid@+0, pidversion@+8, out-token-ptr@+0x10, ptr-to-zero@+0x18, x3@+0x20}`,
  `x0`=&`"Quarantine"` (`0x18b321e0b`), `w1=0xb6`, `bl` import stub
  `0x18b321bc0`. Helper returns stub `w0` unchanged; no ENOPOLICY constant in
  userspace.
- **Stub provider name is NOT resolvable from the slice** — GOT slot
  `0x1dfef8f88` outside all extracted segments (R009 §4). `__mac_syscall`
  (= `__sandbox_ms`, aliased on 15.6) is version-adjacent THEORY.
- R004 runtime (preserved JSON): chroot helper ran the real getter on
  (a) its own chroot token → `rc=-1 errno=103 "Policy not found"`;
  (b) a **live native process's** audit token (pid 51948, alive during call)
  → `errno=103`, out all-zero. Both identities fail identically —
  chroot-namespace effects are excluded as the producer; launchd-native
  identity does not help.

## 2. The three 103 producers and what separates them

`__mac_syscall` (xnu `mac_base.c:1567`, source 8792.81.2; running 8792.82.2 —
drift caveat): `copyinstr` → `mac_proc_check_mac_syscall` (MAC veto, any
errno) → preset `ENOPOLICY` → name-strcmp + non-NULL `mpo_policy_syscall`
scan → handler.

| Arm | Produces 103 when | Existing evidence |
|---|---|---|
| Precheck | a registered policy's `mpo_proc_check_syscall_mac` returns ENOPOLICY | none separating it |
| No-match | no policy named `"Quarantine"` with a syscall hook | image string absence (artifact-scoped) |
| Handler | Quarantine policy exists; handler itself denies | indistinguishable from outside |

R004's both-103 result is consistent with **all three** arms — it rules out
only caller-side/namespace causes, not the kernel arm.

## 3. New observations that actually discriminate

**A. KRW enumeration of `mac_policy_list` (preferred, zero policy syscalls).**
Dopamine's `libjailbreak.dylib` already exposes `kread64/kread32/
task_get_ipc_port_kobject` (established pattern: `misc/agx_iogpu_probe.c`).
Reading `mac_policy_list.entries[*].mpc->mpc_name` + `mpc_ops->
mpo_policy_syscall` directly answers: registered names, whether "Quarantine"
is among them, and whether its hook is NULL.
- Proves at runtime: registered-policy set; Quarantine-name presence; hook
  NULL-ness — covering all three arms at once.
- Missing inputs: `mac_policy_list` static address in the image (targeted,
  bounded: two `"mac_policy_register failed"` format strings at file offsets
  `0x34ef3e`/`0x832f84` in `__PRELINK_TEXT`, plus syscall-381's handler via
  `sysent`, give xref anchors — feasible but not yet done; per task, broad
  scans avoided) + authorization to run a KRW reader helper.

**B. Calibrated no-match control (single syscall, provably no handler can
run).** `__mac_syscall("<name that cannot be registered>", 0, NULL)` — e.g. a
name outside any possible policy set — can only traverse copyinstr → precheck
→ preset → return. It **cannot** invoke a handler by construction.
- Result 103 ⇒ the default arm returns 103; combined with Quarantine→103,
  consistent with no-match. Result ≠103 ⇒ Quarantine→103 implies
  match/precheck involvement → policy plausibly present.
- Cannot separate precheck-vs-default arms if both return 103 (residual).
- Missing input: a helper deploy authorization; zero TCC, no setter, no state
  change — it exercises kernel string-scan only.

**C. Rejected designs** (recorded so they aren't re-proposed): Sandbox-name
or alternate-Quarantine-selector probes (unknown op semantics → potential
writes); repeated getter calls with more token kinds (R004 already covers
native+chroot — no new information); procinfo/`p_responsible_pid` reads
(no userspace flavor — D061/D064).

## 4. Missing inputs to close

1. `mac_policy_list` virtual address in `kc_raw` (targeted RE; anchors above).
2. Authorization for a read-only KRW helper (A) or nonsense-name probe (B).
3. Exact 22F82 stub-provider name — needs the GOT/bind bytes at
   `0x1dfef8f88` (cache segment not extracted).
4. Live confirmation the running kernel's list matches the image — provided
   free by (A).

## 5. Hygiene

No device contact this round; desktop untouched. All artifacts local.

(handover to codex now)

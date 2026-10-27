# D068 — Policy-probe readiness: syscall/precheck offsets verified on exact image

Date: 2026-10-19. Backend. **Local-only.** No KRW, no deployment, no MAC
syscalls, no device contact. Extends D067 (which left the ops offsets
missing); both are now derived from the same `kc_raw_16.3_T8112.bin`
(T8103/M1, xnu-8792.82.2~1; deployed-image hash per D064).

## 1. `mac_syscall` located and decoded (FACT — instruction-level)

**`0xfffffe000865db70`** in `__TEXT_EXEC`. Shape matches xnu `mac_syscall`:

```
dbb8  ldr  x0,[x1]            ; uap.policy (user ptr)
dbc0  mov  w2,#0x20           ; 32-byte name buffer
dbc8  bl   copyinstr          ; _copyinstr @0xfffffe0008094b7c
dbd4  ldr  w22,[x20,#8]       ; uap.call
dbdc  ldr  w8,[x23,#0x75c]    ; staticmax (fast path) — precheck loop bound
dc08  ldr  x8,[x24,#0x768]    ; entries[i].mpc
dc14  ldr  x8,[x8,#0x20]      ; mpc_ops
dc18  ldr  x8,[x8,#0x388]     ; mpo_proc_check_syscall_mac  <=== PRECHECK
dc30  blraa (disc 0x6827)     ; op(proc, name, call)
dc34-… aggregated-error ladder: 0xb,0x16,3,2,0xd,1 priorities; last-nonzero wins
dd84  cbnz w21 → return       ; precheck error = immediate return
dd90  ldr  w21,[+0x75c]       ; dispatch loop bound = staticmax
ddb0  ldr  x10,[x9,x8,lsl#3]  ; entries[i].mpc
ddbc-… inline strcmp(mpc->mpc_name[+0], sp+0x10)
dddc  ldr  x10,[x10,#0x20]    ; mpc_ops
dde0  ldr  x10,[x10,#0x3a8]   ; mpo_policy_syscall          <=== DISPATCH
dde4  cbz  x10 → next         ; NULL op = skip (keeps preset)
dde8  ldr  w1,[x20,#8]        ; uap.call
ddec  ldr  x2,[x20,#0x10]     ; uap.arg
ddf8  blraa (disc 0xfee3)     ; result → w21 → return
de80  mov  w21,#0x67          ; <=== ENOPOLICY=103 preset site
```

Two copies of each loop exist: **staticmax bound** (no lock) when
`numloaded <= staticmax`, and **maxindex bound under `mac_policy_busy`**
(cas-lock + busy++ on `0xaa8cff0/0xaa8cff8`) when dynamic policies exist —
the compiled form of `mac_policy_list_conditional_busy` semantics.

### Verified ops offsets (this image)

| field | offset | evidence |
|---|---|---|
| `mpo_proc_check_syscall_mac` | **+0x388** | precheck loop `blraa` |
| `mpo_policy_init` | +0x398 | register's post-insert call |
| `mpo_policy_initbsd` | +0x3a0 | register's post-insert call |
| `mpo_policy_syscall` | **+0x3a8** | dispatch `blraa` |

(Source-count values 0x630/0x650/0x660/0x670 were wrong for this build —
layout drift vs 8792.81.2 tree; binary wins.)

### Error-arm semantics now fully pinned

1. `copyinstr` fault → EFAULT (not 103).
2. Precheck: any registered policy's `+0x388` hook can veto; aggregated
   non-zero return propagates (a hook CAN produce ENOPOLICY — formally open
   arm stays open unless we also inspect `+0x388` non-NULL-ness per policy).
3. Dispatch preset `w21=0x67` survives iff no entry's name strcmp-matches
   AND has non-NULL `+0x3a8` — the literal no-match/handler-skipped site.

## 2. Slide and pointer contracts

- **Slide**: `live_va = image_va − 0xfffffe0007004000 + kernbase_live`;
  `kernbase_live` comes from the Dopamine primitive set
  (`libjailbreak.dylib` `kread64`/`task_get_ipc_port_kobject` chain already
  used by `misc/agx_iogpu_probe.c`). Image base VA is the `__TEXT` vmaddr.
- **Ops-table pointers are PAC-signed** (stores via `pacia`, calls via
  `blraa` with diversifiers `0x6827` precheck / `0xfee3` syscall / `0x2abe`
  init). For the walker's NULL-vs-registered test, mask low 56 bits —
  a signed non-NULL stays non-zero; an unwritten slot reads 0. Decoding a
  slot to a concrete VA for identification is optional and must be
  range-checked against kernel text after stripping (formula not asserted
  as verified; record raw value, treat decode as best-effort).
- Image storage at `mac_policy_list` (fo `0x1657750`) is **zeroed** —
  runtime-initialized by `mac_policy_init`; the table can only be read
  live. This also means a file-side mock cannot substitute for the
  observation.

## 3. Bounded read-only probe — verified design + mock

Final contract (all offsets now image-verified):

```
hdr = read LIST twice; require identical {numloaded,max,maxindex,
       staticmax,chunks,freehint,entries} else retry (≤3)
bounds: numloaded ≤ max ≤ 0x400; entries 8-aligned ≠ 0
for i in [0, min(maxindex+1, 0x40)):
    e = entries[i]; skip if 0
    name = bounded str read at [e] (cap 0x80, printable-only)
    ops  = [e+0x20]; skip if 0
    has_precheck = (ops[0x388] & low56) != 0
    has_syscall  = (ops[0x3a8] & low56) != 0
```

Mock test `analysis/devin-d068/walker_mock.py` (image-faithful synthetic
layout): enumerates 5 fake policies correctly, detects per-policy
precheck/syscall presence, aborts on `numloaded` garbage, fails closed on
unmapped conf. **PASS.**

## 4. What the single snapshot can and cannot prove

- Proves (at snapshot instant): registered names; per-policy
  precheck/syscall hook presence.
- If `"Quarantine"` absent ⇒ for any *subsequent* call the handler arm is
  closed; producer ∈ {no-match preset, foreign precheck veto — itself
  bounded by which policies carry `+0x388`}.
- **Cannot prove permanent or historical absence** — only the instant
  observed. `mac_late=1` post-boot makes later registration impossible
  (verified EPERM path), so per-generation stability holds; past instants
  remain unobservable.
- Cannot show what a hook *returns* — only that it exists.

## 5. Recommended ONE smallest causal observation

Run the §3 walker once via the existing Dopamine KRW primitive — ~6 reads
for the header + ~3 per policy. It is strictly the minimal observation
that closes the provider question; the dedicated WS gate observer sees
only the userspace denial path (rc=1002/empty replies) and cannot read
kernel registration state at all — it cannot substitute for this, and
this does not disturb it. No MAC syscall, no TCC, no endpoint or setter.

(handover to codex now)

# D072 — backend: approval packet, claim audit, minimal live witnesses

Read-only. No device, KRW, syscalls, deploy, build, credentials, source
or Git changes. Everything below cites existing artifacts.

## 1. Approval packet — bounded responsibility/TCC discriminator

### 1a. Can an existing artifact answer it without a new read? — Partially.

- **What existing artifacts already narrow**: the decompressed
  kernelcache (byte-identical to the device boot image, D064 SHA
  `78959dd9…`) contains **zero** NUL-terminated `"Quarantine"` strings
  (D062/D063 scan). Since `mac_policy_conf.mpc_name` is a `const char*`
  into kernel-image memory, no *static* registration of `"Quarantine"`
  can come from any code in this image. No post-boot kexts can load
  (`/System/Library/Extensions` absent on-device; `mac_late=1` blocks
  late registration — D067/D068 verified).
- **Residual gap (verified this packet)**: enumerating
  `mac_policy_register` (`0xfffffe000865b770`) callers to exclude the
  exotic dynamic-name case is **blocked by image format**: zero direct
  `b/bl` sites exist across `__TEXT_EXEC`+`__PRELINK_TEXT` (callers
  route through kext stub triples `adrp/ldr/braa`), and the GOT entries
  that would resolve those stubs are PAC/fixup-encoded — masking to
  low-56 does not recover them (D067's earlier finding, re-confirmed:
  zero `adrp` sites materialize even the *verified* AMFI conf VA
  `0xfffffe0007bac090`, and no `__DATA_CONST` slot holds it
  unmangled). Caller-enumeration therefore needs a PAC-aware fixup
  decoder — a larger RE task, not a quick read.
- **Conclusion**: offline artifacts reduce the live question to one
  row — "is a `Quarantine`-named policy registered at run time by any
  means" — which only the bounded snapshot answers completely. The
  snapshot remains necessary and is now the *only* missing input.

### 1b. Approval packet

**Objective**: read `mac_policy_list` once; report names + the two ops
slots' presence. Answers only whether a `Quarantine` handler *can*
exist this boot epoch (per D069 frontend contract §3).

**Preconditions** (all must hold before any read):

| # | Precondition | Evidence it must produce |
|---|---|---|
| P1 | Explicit authorization naming this packet | task file |
| P2 | SSH with `SSHPASS` env only — never argv | transcript shows env form |
| P3 | Executor = procursus `python3` + ctypes → `libjailbreak.dylib` — **no new binary, no trustcache add** | `which python3`; `ldid -e` shows platform ents |
| P4 | Pre-read witness: WS pid/lstart, tccd pids, `macws_final_composite.state` seq | captured lines |
| P5 | Kernel identity: sysinfo dict `kernelConstant.base == staticBase(0xfffffe0007004000)+slide`; `kread64(base)&0xffffffff == 0xfeedfacf`; `kread64(0xfffffe0007e95ee5+slide)` == literal `Darwin Kernel Version 22.3.0 … RELEASE_ARM64_T8103` | pass/fail line per check |

**Arm sequence**: `jbclient_initialize_primitives()` →
`jbclient_root_get_physrw()` (rc must be 0) → `kread64` usable.
Entitlement uncertainty: P3/P5 depend on jailbreakd granting physrw to
a python-entitlement caller — unverified; if rc≠0 or reads return
0/garbage → **STOP, report "missing input: primitives unarmed"**.

**Read set (only)**: header double-read (tear → STOP); caps
`numloaded≤max≤0x400`, `maxindex≤0x40`; per entry: `mpc` pointer
low56/range, name ≤0x80 printable+NUL, `ops+0x388` and `ops+0x3a8`
presence via low-56 mask. Nothing else read.

**Exact sanitized output format** (frontend §4 — no raw KASLR):

```
kident: magic=ok version=ok slide-consistency=ok
hdr: numloaded=N max=N maxindex=N staticmax=N (double-read: match)
[i] name="<name>" precheck=<0|1> syscall=<0|1>
post: ws_pid same=1 tccd same=1 seq delta=<n> state=ready
```

**Stop conditions** (each → halt, emit check name only): any P-row
fails; torn header; bounds violation; non-printable/non-NUL name;
pointer out of kernel range; `get_physrw` rc≠0; python primitives
unarmed. **Post-run**: cleanup files, verify generation unchanged
(same pids + seq advancing), then write evidence.

**Interpretation bounds** (from the contract): `Quarantine` absent +
`mac_late=1` ⇒ handler arm closed for this boot epoch only; precheck
veto arm (Sandbox has `ops+0x388`) stays open; #103's historical
producer is *not* identified by presence data alone.

## 2. Claim audit — IconServices/F11H and provider/F1

Checked R005/R007/R009/R010/R012/R014/R027/D041+ docs for duplicates
and contradictions:

- **R005 vs supplier recipe**: R005 excludes uid/gid/cache/trust as the
  F11H cause; the supplier UID501/GID20 recipe is labeled untested
  THEORY there — consistent, not contradictory, but worth restating so
  nobody treats the recipe as pending validation.
- **R014 vs R027**: R014 captured `Unknown attribute kind (71)`; an
  intermediate ZeroExt attribution was **rejected** by R027's
  versioned wire audit (71=nocallback). Any doc implying ZeroExt is
  stale — R027 is authoritative.
- **R010 `F1-VERDICT=PASS` vs "live F1 route witness missing"**:
  superficially contradictory but actually two scopes — isolated
  disposable-probe call vs live-WS route firing. Retained correctly in
  R010's own table; D071 carried the same distinction. No doc found
  claiming live F1 firing.
- **AGENTS.md `iPad13,6` vs live `iPad13,11`** (D063): platform matrix
  entry is the sibling unit; not a code bug but a labeling landmine —
  worth a one-line correction in a future doc pass.
- **D069**: run output (`numloaded=3 …`) exists in transcripts but is
  administratively inadmissible; any future doc must not cite it as
  evidence. Recorded as process contradiction.
- No duplicated contradictory claims found elsewhere; the
  "installed ≠ firing" boundary is stated consistently across R010,
  D071, and the D069 frontend contract.

## 3. Smallest read-only evidence for F1 firing + Host pixel witness

**F1 route firing** (live WS, current generation):

- Predicate: an impl/dispatch record or counter emitted by the
  *installed* F1 hook when a qualifying drawable publish occurs —
  `replacement_called`/`via-hook` semantics already defined by the
  probe telemetry.
- Smallest evidence, all existing channels: (a) WS stderr /
  `macws_runtime_diagnostics` log lines from the F1 site during one
  ordinary window publish; (b) the sender-observation rings already
  deployed (gate/dispatch/impl) — an impl-site record appearing is the
  firing witness. Requires only *reading* existing log/ring surfaces;
  if the diagnostics marker file must be created to unmute stderr,
  that is a one-file presence change needing its own authorization —
  **missing input if so**.
- Acceptance: record present + producer identity matches qualifying
  drawable; absence is NOT proof of non-firing (D042 correction:
  absent records can mean disarmed/dropped) — a negative needs the
  hook-install witness co-recorded.

**Host pixel witness**:

- Predicate: MacWSHost actually presented an IOSurface this frame.
- Smallest evidence: read existing surfaces — (a) final-composite
  state file seq advancing (already passing, but that's producer-side);
  (b) Host-side presentation evidence: the host app's drawable-present
  count/IOSurface ID handoff log or `CATransaction` counters if the
  host emits them; (c) pixel hash change on the observer path (the
  D037-era pixel hash already exists as a channel).
- If the host emits no presentation telemetry today, the missing input
  is one bounded read-only counter/log addition — a frontend-side
  packet, not a reopen of closed experiments.

**Neither requires**: new hooks, share-package code, TCC/assert
bypasses, or generation restart — only reading channels that already
exist, plus (if needed) a separately-authorized unmute marker.

## Provenance

Artifacts: kc_raw caller-stub scan (this packet, offline only);
kcgen symtab layout D067; kernel identity chain D063/D064; provider/F1
R007/R010/R012; IconServices R005 excerpt + R009/R014/R024/R027
compiler line; acceptance contract D069-frontend; checklist
`share-lessons-active-checklist-20261018.md`.

# D068 frontend — single causal WS observation: readiness design

Design only — nothing executed. No device ops, no KRW, no MAC syscalls,
no TCC requests, no deployment, no restart. All items below are
prepared against already-verified local evidence.

## 0. Corrections applied (history preserved)

Three prior claims need repair before any plan can be executed:

1. **D042 §4 direction inversion.** The disasm at
   `SkyLight_22F82:0x185425da0` is unambiguous: `blraaz` →
   `cmp w0,#0; cset w0,eq` — the impl returns `(preflight_rc == 0)`.
   So `impl_rc != 0` means **preflight returned 0 = allowed**;
   `impl_rc == 0` means preflight returned nonzero **or** the symbol
   was never resolved (`cbz x0` leg at `0x185425df0` falls through with
   `w0=0`). D042's rows "impl_rc≠0 → deny authored by preflight" and
   "impl_rc=0 → TCC passed" are backwards. Corrected below.
2. **Absent impl records do not prove absent calls.** A missing
   `SITE_IMPL` record can mean (a) the function never ran, (b) the hook
   is not installed in the running WS generation, (c) the emit was
   dropped (seq budget / write failure / disarmed), or (d) the call
   ran before arming. Distinguishing (a) from (b–d) requires the
   install proof + completeness accounting in §3–§4 — which is why
   this observation is a single *causal* run, not a scan for a missing
   line.
3. **`impl_rc=0` is not an explicit policy verdict.** It conflates
   three producers: (a) `PreflightWithAuditToken` unresolved
   (`lazy_post==0`), (b) the call ran and returned nonzero from an
   **evaluation failure** (e.g. tccd empty reply / `#103` path), and
   (c) an actual evaluated denial. `lazy_post` separates (a) from
   (b∪c); splitting (b) from (c) requires the correlated tccd-side
   witness (filled verdict dict vs `count=0` empty reply, and the
   ERROR line) — which is why §4 pairs the impl record with the
   tccd-system stderr window.

## 1. Frozen identities (pin before any run)

| Artifact | Identity |
|---|---|
| SkyLight | `analysis/SkyLight_22F82`, UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880` — hook offsets `+0x318464` gate, `+0x3407ec` disp, `+0x340da0` impl; globals `+0x53AC11B8` audit slot, `+0x4ECE66C8` rightDesc, `+0x53AC1B50` lazy checker — all byte-verified prologues in `MacWSSenderObservation.c` |
| tccd | `tccd_arm64e` UUID `9845D17F` (live pid fingerprint `42c388e8…` from D063-era) |
| libmachook | sha256 of the exact deployed `.dylib` — must be read at plan time and pinned against the repo build artifact (permit contract already pins UUID+offset+prologue live, but the file identity belongs in the record) |
| probe | sha256 of `/usr/local/bin/macos_ws_event_probe` as deployed |
| generation | WS pid + `state` file seq at plan time (current witness: `state=ready`, seq ~5145 lineage) |

## 2. Hook installation proof (precondition, not assumption)

The three hooks arm only inside `loadImageCallback` during **WS
launch**, gated on the sentinel existing *at that moment*. Therefore:

- Missing input #1: does the **current** WS generation's stderr carry
  `#### SENDEROBS-INSTALL disp=… impl=… permitDisp=0 permitImpl=0`?
  This is a read-only log check (`WindowServer.err` +
  `CGXSYNTH-INSTALL`). If `permitDisp=0 permitImpl=0` is present for
  this pid lineage → hooks resident, plan proceeds without restart.
- If absent/failed: installing requires a WS restart — the heavyweight
  production operation (full session/rollback protocol). That branch
  is documented but explicitly **not** part of the single-observation
  proposal; a restart-bound plan is a different authorization.
- Secondary proof during the run itself: `MACWSHook`-permit lines and,
  on the armed window, `g_obs_snaps` advancing = captures happening.

## 3. The single observation (bounded)

Ordered steps, each independently bounded:

1. **Pre-witness**: `cat /private/var/mnt/rootfs/private/tmp/
   macws_final_composite.state` (state+seq), record WS pid + tccd pid;
   capture a composite pixel hash / VNC frame hash as before-image.
2. **Verify counters baseline**: dump current
   `g_obs_site_calls[1..3]`, `seq`, `dropped`, `snaps` (via the obs
   ring/log as it exists — note: counters are WS-process-local; the
   reader is the on-disk log tail + record seqs, so baseline = "last
   emitted seq" not the counters themselves unless a reader path
   exists — flag as design caveat).
3. **Arm**: `touch /var/mnt/rootfs/tmp/macws_sender_observation`
   (chroot `/tmp/…` spelling). Confirm absence of the
   `macws_runtime_diagnostics` sentinel in BOTH namespaces — the
   dedicated sentinel must be the only armed switch, since the
   umbrella also enables an alternate display producer (would
   confound causality).
4. **Window-open**: note `WindowServer.err` + `tccd-system.err` byte
   offsets for delta reads.
5. **The post**: one bounded owned post —
   `launchdchrootexec 0 0 /var/mnt/rootfs
   /usr/local/bin/macos_ws_event_probe post` (D041 canonical shape;
   `postmin` variant exists as the lighter form used by R008). Exactly
   one invocation; probe exit rc recorded.
6. **Read**: `macws_sender_observation.log` records (D049 decoder:
   magic/version/seq integrity, SEQ_GAPS, DUP_SEQ, pointer
   plausibility, partial-tail flag), the two stderr deltas, and the
   state file again.
7. **Disarm + cleanup**: `rm` the sentinel (live disarm — D046
   verified `snaps` stays flat after disarm), copy log off, `rm` log.
   Hooks remain resident but inert; no other state touched.

Authorization subtlety recorded from D066: the probe itself makes no
TCC call, but SkyLight's genuine gate may transitively emit a
WS→tccd-system request — that induced traffic is the *intended*
observation target and needs the explicit signoff as a distinct item.

## 4. Completeness + decision matrix (corrected)

Accounting reads (all from the emitted log + stderr):

- `seq` span vs record count → SEQ_GAPS = reserved-but-absent emits
  (drops). `snaps` flat ⇒ captures didn't run (disarmed or
  hook-absent). `site_calls` (if a live reader exists) vs records →
  un-emitted calls.
- `invocation` chains correlate gate/disp/impl records of one attempt;
  `F_STANDALONE` flags out-of-chain impl calls.

| Observation | Reading |
|---|---|
| hooks proven installed, zero drops, gate record only | deny inside dispatcher early paths — TCC not on this attempt's causal path |
| impl record, `lazy_post==0`, `rc=0` | deny = `PreflightWithAuditToken` unresolved in WS's chroot — fail-closed leg, a *local fixable* producer independent of kernel policy |
| impl record, `lazy_post!=0`, `rc=0`, tccd stderr shows ERROR/`count=0` in window | TCC was asked and the answer was an **evaluation failure** (provider hypothesis on the causal path) |
| impl record, `lazy_post!=0`, `rc=0`, tccd reply populated with deny fields | genuine policy denial — distinct producer |
| impl record, `rc!=0`, gate `rc=0` | TCC **allowed**; deny authored downstream in dispatcher cache-fold |
| no impl record AND (install unproven OR drops>0) | **inconclusive — absent records ≠ absent calls** |

## 5. Reversibility + watchdog awareness

- Sentinel removal is the full disarm (uncached `access(F_OK)` per
  emit — next call unarmed); log copy+rm leaves no device state.
- Records are WS-process-local; a watchdog/jetsam restart of WS would
  lose the ring — and since hooks arm at launch, a restart *with* the
  sentinel still in place silently re-arms observation in the new
  generation. Plan removes the sentinel before any such risk window;
  restarts are otherwise watched via the state file's producer pid.
- Nothing here writes binaries, plists, or shared state while WS runs.
- Rollback cost if the not-installed branch were ever authorized:
  removing the sentinel + a WS restart under the full session protocol
  (composite state capture, producer witness, both-namespace sentinel
  sweep).

## 6. Missing inputs (to close before execution)

1. Current-generation `SENDEROBS-INSTALL`/`permitImpl` status (stderr
   read) — decides restart-free vs restart-bound branch.
2. Deployed libmachook + probe sha256 pins (read-only).
3. The TCC-transitive signoff (§3 step-5 note).
4. A live-reader path for `site_calls/dropped/snaps` if counter-level
   accounting is wanted; the on-disk log alone bounds it via SEQ_GAPS.

## Provenance

`SkyLight_22F82` `96676A53`; `MacWSSenderObservation.{h,c}` +
`mac_hooks.m:5940–6214`; D049 decoder (`obs_decode.py`, 144-byte
records, seq/gap/plausibility checks); canonical post artifact
`r004b-canonical-post.json` + `R008-postmin-recovered-20261018.json`.
No device access this round; desktop untouched.

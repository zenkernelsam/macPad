# D138 — manual frame-wait vs service-message latency: saved-evidence audit

Date 2026-10-19. Packet `coordination/_relay/devin-138-frontend-task.md`.
Read-only. Audited the D101/D102/D107/D109/D129/D130/D135 evidence chain
for same-generation, same-thread bracketed timing data. No device, no
probes, no edits/builds.

## 1. Verdict: no such data exists — nothing brackets the wait

**Zero records** pair `pace enter/exit` with SkyLight
receive/dequeue/dispatch/reply timestamps in any generation:

- **Pace side**: the only instrumentation ever written is the D107/D109
  *candidate patch* (`analysis/d107-pacing-candidate/`) — host-tested,
  `git apply --check`-clean, **never applied, built, or deployed**. No
  device has ever emitted a `pace` record.
- **SkyLight side**: the timing points are pinned-by-design only (D129:
  recv `0x185421978/9a8`, dispatch `0x185421f5c`, demux `0x1851eb584`,
  reply-enqueue `0x1854213a4`, send `0x185421554/0x18547cc6c`). Verified:
  the candidate patch contains **zero** SkyLight hooks — no
  `post_port_data`/`rendezvous`/`CGXPostReply` coverage. The service-half
  instrumentation was never written; it would need new permit-registered
  hooks at those VAs.
- **D102's gen-50574 stacks** are *placement* proof (paced update runs
  inside the services pass on `ws_main_thread`), not timing — no
  enter/exit or recv/dispatch timestamps.
- **Composite sequence advance** (D130: 141→622; D135: 954→966) measures
  cadence/throughput only — per packet, not inferable to per-message
  latency.

Therefore D131's acceptance test cannot even be scored: 0 samples per
arm (needs ≥30), no `exit_ns`-clustered dequeue records, no median
excess computable, no services-tid identity in the current generation.
Producer wait vs queue wait have never been separated by data.

## 2. Exactly which fields are absent

Per the D129 schema — all absent for **every** generation:

`pace enter_ns, lock_ns, start_ns, exit_ns, slept_us, iters, wakes,
reason, tid, seq, arm window`; SkyLight `recv_ns, dequeue_ns,
dispatch_ns, reply_enqueue_ns, reply_send_ns`; per-record `tid`
(current-generation services-tid identity remains MISSING — D129
carried it as an open requirement); correlation `request id`/`msgid`;
per-record `generation`/SkyLight UUID stamp; the `no-wait` baseline arm.

## 3. Minimum safe one-variable measurement

Both halves are required; only one half exists even as a candidate:

1. Pace records: deploy the D107/D109 candidate (diagnostic build —
   sentinel-gated, off by default).
2. SkyLight-side records: **not yet implemented** — new
   permit-registered record-only hooks at the D129-pinned VAs, same 32-B
   record schema, same ring/flush discipline. This is new code, not a
   deploy of anything existing.
3. One variable = wait behavior: `no-wait baseline` vs `one bounded
   interval`, everything else fixed (same WS UUID 22F82
   `96676A53…`, same workload/endpoint, same thermal state, read-only
   `0x74a7` probe sends ≥30/arm).
4. Acceptance (per D131/D129): exit_ns-clustered dequeue +
   median excess ≥ max(8 ms, 0.5×interval), replicated on
   services-tid — else current behavior stands.
5. The kernel-arrival bound stays explicitly unobservable (send phase +
   recv-edge clustering are the proxies — D129's honesty note).

## 4. Production-safety of the existing instrumentation (as designed)

The D107/D109 candidate is **safe-when-disarmed and bounded-when-armed**,
with named residuals:

- Default-off double gate: dedicated sentinel absent → only the cached
  `access()` arm-check runs (≥1 s epoch cache, CAS-owner, ≤1 stat/4 s
  per process) — production impact when disarmed ≈ one cached stat.
- Armed: fixed 32-B records, bounded ring, publish-after-memcpy,
  `O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC` single-fd emit, bounded retries,
  errno preservation, 8192-line cap, TLS-seq + pid tags — all verified
  by host tests (22/22 parser, 13/13 concurrent-arm ×25 clean).
- Residuals that keep it a *diagnostic*, not production code: never
  compiled for arm64 (host test was x86_64 thunk + syntax-only probe);
  adds clock reads + a mutex + ring writes inside the measured region
  (inherent observer cost); sentinel file must live in the chroot tmp
  namespace; per AGENTS.md it must ship labeled diagnostic and cannot be
  the permanent mechanism.
- Deploying it requires a libmachook build+install — out of all current
  bounds; and alone it **cannot** answer the causal question (pace half
  only, §3.2).

## 5. D130/D135 frame-wait measurement: none — confirmed independent

- D130: layered acceptance only (hashes, REFUSED lines, state seq, F1
  route) — zero timing fields.
- D135: one native composite pixel witness (CGDisplayCreateImage,
  2732×2048, 343 K unique colors, real Aqua desktop matching the
  workspace graph; iPad-side panel pixels still unwitnessed) + the
  reopen-freshness audit (D130 did NOT exercise it — confirmed). Its
  only numbers are sequence values 954→966 — cadence, not latency.
- So the frame-wait causality question remains entirely unmeasured;
  D135's pixel witness closes the *pixel* half of L5 but says nothing
  about wait-vs-queue.

(handover to codex now)

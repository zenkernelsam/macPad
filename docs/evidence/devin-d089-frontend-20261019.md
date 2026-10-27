# D089 — final static consistency check of the D088 pacing plan

Offline audit only. Nothing applied; no execution implied.

Subject: `docs/evidence/devin-d088-frontend-20261019.md`.

## Checked — no defect

- **C ordering / atomics.** `last_check_ns=0` ⇒ first call always
  rechecks (`now - 0 ≥ 1 s`). Store-`last` before exchange-`cached`
  bounds staleness at the declared ≤1 s window; a reader between the
  two sees at most one stale interval. `atomic_exchange` on
  `cached_value` serializes the 0→1 edge — exactly one
  `sentinel-armed` line per arm window under concurrent callers
  (a second racer sees `prior=1`, no emit). Unsigned wrap on a
  backward clock read fails toward recheck; `clock_gettime` failure
  returns the cached value (default 0 = off). `MIN` already in use
  (:27391); `NSEC_PER_SEC`/`NSEC_PER_MSEC` family in use (:274xx).
- **Reachability.** `macws_frame_pace_write`/`_enabled`/`_trace` sit
  in the :9076 helper region — textually before both sites
  (:20056, :27395); `g_macws_iomfb_coexist_swap_cancel` (:1503) is in
  scope for the `arm=` read. The extra gate call inside `_trace` is a
  second atomic load — free.
- **Output bounds.** Ack ≤1 line per arm window; site-1 payload stays
  inside `<=4||%600` (:20060), site-2 inside `prior != mode`
  (:27400); one `write(2)` per record; `server=%p` dropped; the
  umbrella `fprintf` paths are untouched — no `####` scope widening.
- **Cleanup/rollback.** `ROOTFS=/var/mnt/rootfs` (`macos_gui.sh:45`)
  confirms the host spelling `/var/mnt/rootfs/tmp/…` matches the
  `clear_diagnostic_state` `$ROOTFS$path` sweep; pre-arm
  rm-trace→touch order, ordinary retrieve→rm (both spellings),
  registered-flag sweep backstop, pinned-dylib revert + WS restart —
  complete and consistent.
- **Freshness chain.** `rm` before `touch` is load-bearing (a skipped
  rm + appended stale file could pass mtime/first-line checks only by
  protocol violation, which voids the run); mtime ≥ lstart + first
  line `sentinel-armed` are the two checks. Optional non-blocking
  hardening: add `pid=%d` (WS pid) to the ack line to bind it to the
  process instance against a stale-trace protocol violation.

## Verdict

**No concrete defect remains.** The plan is internally consistent and
within the codebase's conventions; ready for implementation under a
separately authorized rebuild → redeploy (sign/trustcache) → WS
restart → post-launch sentinel-lifecycle grant. Not applied here;
no execution approved or implied.

# D088 — final frame-pacing diagnostic plan (D086 + D087 findings)

Plan only — **not applied**. Consolidates the uncached-helper,
edge-ack, and cleanup corrections into one spec. All line numbers
verified against the current checkout (2026-10-19).

## Design — rate-limited sentinel check + edge-emit ack

Replaces both the D084 cached-flag deadlock and the D086 per-frame
`access(2)` regression (R1):

```c
static void macws_frame_pace_write(const char *line, size_t n) {
    /* open("/tmp/macws_frame_pace.trace", O_WRONLY|O_CREAT|O_APPEND|
       O_CLOEXEC, 0600) → single write(2) → close; verbatim shape of
       macws_keyboard_latency_trace's emit block (:9082–:9097) */
}

static BOOL macws_frame_pace_diagnostics_enabled(void) {
    static _Atomic uint64_t last_check_ns;          /* 0 = unchecked */
    static _Atomic int cached_value;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return atomic_load_explicit(&cached_value,
                                  memory_order_acquire) != 0;
    uint64_t now = (uint64_t)ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
    if (now - atomic_load_explicit(&last_check_ns,
                                   memory_order_acquire)
            < NSEC_PER_SEC)                          /* ≤1 Hz re-stat */
        return cached_value != 0;
    int value = access("/tmp/macws_frame_pace_diagnostics",
                       F_OK) == 0;
    atomic_store_explicit(&last_check_ns, now, memory_order_release);
    int prior = atomic_exchange_explicit(&cached_value, value,
                                         memory_order_acq_rel);
    if (value && !prior) {                           /* 0→1 armed edge */
        char ack[96];
        int n = snprintf(ack, sizeof(ack),
            "COEXIST-PACE sentinel-armed arm=%d\n",
            atomic_load_explicit(&g_macws_iomfb_coexist_swap_cancel,
                                 memory_order_acquire));
        if (n > 0)
            macws_frame_pace_write(ack, (size_t)MIN(n, sizeof(ack)-1));
    }
    return value != 0;
}

static void macws_frame_pace_trace(const char *format, ...) {
    if (!macws_frame_pace_diagnostics_enabled() || !format) return;
    /* vsnprintf+clamp → macws_frame_pace_write(line, bytes) */
}
```

Properties:

- **Per-frame cost**: after the first check, one atomic load + compare
  per gate evaluation; the `access(2)` runs at most once per second —
  satisfies the :343–:349 hot-path rule the naive uncached version
  violated. (R1 fixed.)
- **Post-launch arming preserved**: a post-launch `touch` is seen on
  the next gate evaluation after ≤1 s — the only workable window, per
  D085-Finding-A (`enable_experimental_if_requested` :5725 sweeps all
  sentinels at launch; `production_preflight` :3768 rejects
  registered survivors).
- **Live disarm preserved**: sentinel `rm` takes effect within ≤1 s.
- **Edge-triggered ack**: `atomic_exchange` on `cached_value`
  serializes the 0→1 transition — exactly one `sentinel-armed` line
  per arm window, race-safe; disarm→re-arm produces a fresh ack
  (replaces the process-latched `armed_acked`, which would miss
  re-arms — R3/R4 fixed). The ack is emitted on the check edge, not at
  first bounded emit, so "armed but bounds never hit" still yields the
  ack line — the four-state table now fully works:

  | trace content | meaning |
  |---|---|
  | `sentinel-armed arm=1` + pace lines | armed, bounds reached |
  | `sentinel-armed arm=1` only | armed, emit bounds not hit |
  | `sentinel-armed arm=0` | sentinel seen; coexist translation not armed this generation |
  | empty/absent trace | sentinel never observed |

  `arm=` reads `g_macws_iomfb_coexist_swap_cancel` (:1503), in scope
  at the helper's :9076-area placement.

## Sites (unchanged contract, conversions only)

- **Site 1** (:20056 gate inside `:20050` coexist-flag check, inside
  per-frame `begin_skylight_update` :20048): `umbrella || dedicated`
  around the existing counter; inside the existing `<=4||%600` bound
  (:20060) — `if (umbrella) fprintf(stderr, …server=%p…)` untouched,
  `if (dedicated) macws_frame_pace_trace(…)` without `%p`.
- **Site 2** (:27395 gate, mode-transition bound :27400): same shape;
  fields carry no pointers.
- No emit inside `macws_filtered_fprintf` (:333–:341); no other
  `####` line is affected.

## Sentinel lifecycle (corrected contract)

- **Registration**: one `flag`/`off` row in
  `docs/runtime-switches.tsv` → regenerate
  `macws_diagnostic_flags.sh` → `misc/audit_runtime_switches.py` is
  the declared post-edit check (`flag`-kind rows build the sweep list
  at audit :92). Registered ⇒ `clear_diagnostic_state` (:3531) sweeps
  both namespace spellings at every subsequent launch; preflight
  rejects a survivor → leftover arms can't silently persist.
- **Pre-arm (granted operator step)**: `rm` the `.trace` file first,
  then `touch` the sentinel — host spelling
  `/var/mnt/rootfs/tmp/…` for both (chroot path is `/tmp/…`); this
  ordering prevents a stale trace from satisfying acceptance (R-
  stale-trace finding).
- **Post-measurement (new — ordinary cleanup)**: retrieve the trace,
  then `rm` trace **and** sentinel, both spellings. The `.trace` is
  unregistered (never auto-swept); the sentinel's next-launch sweep
  is only a backstop, not the cleanup path.
- **Generation freshness**: trace mtime ≥ target generation's WS
  lstart; first line must be `COEXIST-PACE sentinel-armed …`; accept
  pace lines only after `state=ready` + seq **advancing** (delta=0
  flagged anomalous).

## Rollback (restated, D082 §5)

Revert to the prior hash-pinned signed libmachook dylib (retained
deployment artifact) + WindowServer restart under the same
session/rollback/watchdog protocol; `rm` sentinel + `.trace` in both
namespace spellings; registered-sweep backstop at next launch.

## Honest size

3 files, no launcher change: `mac_hooks.m` ≈45 lines across 3 regions
(write primitive + rate-limited check/edge-ack + trace wrapper, ~25;
two site conversions, ~20), `docs/runtime-switches.tsv` +1 row,
regenerated `macws_diagnostic_flags.sh`. `macos_gui.sh` untouched.

## Prerequisites (unchanged)

Rebuild + redeploy (sign/trustcache) + WS restart under a separately
granted session/rollback protocol; post-launch sentinel/trace
lifecycle as its own granted device mutation (D041 precedent).
Current generation unmeasurable; umbrella cached-once + confounded.

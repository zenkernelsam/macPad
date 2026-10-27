# D084 — revised frame-pacing diagnostic plan (D082 + D083 backend findings)

Plan only — **not applied**. Each D083-backend finding is addressed
explicitly; line numbers verified against the current checkout
(`libmachook/mac_hooks.m`, `layout/usr/macOS/bin/macos_gui.sh`,
`macws_diagnostic_flags.sh`, `docs/runtime-switches.tsv`).

## Finding 1 resolution — emit channel: `write(2)` record file (option b)

`mac_hooks.m:335-343` (`macws_filtered_fprintf`) + the `#define fprintf`
at :344 route **every** fprintf through the umbrella check; stderr emits
return 0 when `macws_runtime_diagnostics_enabled()` is false. The D082
plan's OR-into-two-gates would therefore emit **zero bytes** under the
dedicated sentinel — confirmed, and the D082 claim "same stderr channel
as `#### CGXSYNTH`" was itself only ever observed *under* the umbrella.

**Choice: a narrow trace helper**, modeled verbatim on the D041-proven
precedent `macws_keyboard_latency_trace` (`mac_hooks.m:9081-9096`):

```c
static void macws_frame_pace_trace(const char *format, ...) {
    if (!macws_frame_pace_diagnostics_enabled() || !format) return;
    char line[512];
    /* vsnprintf + clamp, identical to :9085-:9091 */
    int fd = open("/tmp/macws_frame_pace.trace",
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) return;
    (void)write(fd, line, bytes);
    close(fd);
}
```

Why (b) over (a): OR-ing into the filter at :335 un-mutes **every**
`####` fprintf in the file whenever the sentinel exists — install
traces, `:27586` SwapCancel emit, and every launch-time line — in WS
*and* every chroot app loading libmachook (Terminal shells inherit it).
That is a larger confound than the umbrella itself. The `write(2)`
record path is also the documented reliability fix (`:9080` comment:
stderr "not a reliable timing witness … under launchd/chroot"), and a
file artifact is better evidence than stderr scraping — hashable,
diffable, survives stdio replacement. Distinct path
(`/tmp/macws_frame_pace.trace`) so channels never interleave.

Emit contract per site: keep the existing `if (umbrella) fprintf`
**unchanged** (umbrella output channel and counter semantics
untouched); add a parallel dedicated branch. Site 1:

```c
if (macws_runtime_diagnostics_enabled() ||
    macws_frame_pace_diagnostics_enabled()) {
    static _Atomic unsigned long paced_updates;
    unsigned long sequence = atomic_fetch_add_explicit(...) + 1;
    if (sequence <= 4 || (sequence % 600) == 0) {
        if (macws_runtime_diagnostics_enabled())
            fprintf(stderr, "#### COEXIST pre-lock completion pace #%lu: "
                    "interval=%u us slept=%u us server=%p\n", ...);
        if (macws_frame_pace_diagnostics_enabled())
            macws_frame_pace_trace("#### COEXIST pre-lock completion pace #%lu: "
                                   "interval=%u us slept=%u us\n", ...);  /* no %p */
    }
}
```

Site 2 identical in shape (mode-transition bound at `:27400` retained).
The dedicated trace drops `server=%p` (pointer in a retrievable file).

## Finding 2 resolution — coexist arm precondition + self-witness

Site 1 sits inside `if (g_macws_iomfb_coexist_swap_cancel)` (`:20050`),
set only when the kern_SwapEnd BL callsite verifies at WS launch
(`:6476-:6481`, itself gated by real-WindowServer + backboardd). So
silence is three-way ambiguous: sentinel unseen / bound not reached /
translation not armed this generation. **Fix: a third emit site** — one
trace line at the arming decision, in both branches:

- after `:6479` armed fprintf → `trace("COEXIST-PACE arm=1")`
- in the `:6483` mismatch branch → `trace("COEXIST-PACE arm=0 mismatch=%#x")`

This makes the diagnostic self-witnessing: `arm=1` + pace lines ⇒ all
three legs live; `arm=1` + no pace lines ⇒ bounds not reached (idle);
`arm=0` ⇒ translation not armed this generation; empty trace ⇒
sentinel unseen at check time. (The existing `:6479` stderr line is
filtered without umbrella — the trace line is the non-umbrella
witness; that is the point.)

## Finding 3 resolution — conform to the process-start contract

`:345-:351` states the convention: sentinels are created/removed by
`macos_gui.sh` before launch; `production_preflight` sweeps survivors
via `clear_diagnostic_state` (`:3531`, rm's every registered path in
**both** namespace spellings `$ROOTFS$path` and `$path`). The registry
is generated from `docs/runtime-switches.tsv` →
`macws_diagnostic_flags.sh`; `audit_runtime_switches.py` fails when a
source sentinel is unregistered.

Since activation is **restart-bound anyway** (Finding from D082: the
code must be rebuilt/redeployed/restarted to exist at all), live
mid-session re-stat buys nothing. Replace the uncached helper with the
file's own macro:

```c
MACWS_DEFINE_STARTUP_FLAG(macws_frame_pace_diagnostics_enabled,
                          "/tmp/macws_frame_pace_diagnostics")
```

That conforms to the stated convention (cached at first use, zero
per-call `access(2)` on frame paths), and removes the need to justify
an exception. Registration: one `flag` row in
`docs/runtime-switches.tsv` (`production=off`), regenerate
`macws_diagnostic_flags.sh`; `clear_diagnostic_state` then sweeps it in
both namespaces automatically — the cleanup story is free. The
`.trace` output file follows the keyboard-latency precedent: not a
sentinel, not registered; operator retrieves, runbook rm's.

Honest patch size: **3 files** — `mac_hooks.m` (≈28 lines across 4
regions: startup-flag helper, trace helper, arm emit pair, 2 site
conversions), `docs/runtime-switches.tsv` (1 row), regenerated
`macws_diagnostic_flags.sh`. Not "≈7 lines".

## Finding 4 — corrected umbrella count

`:279-:290`: **2 env vars + 12 `access()` sentinels**
(runtime_diagnostics, submit_diag, submit_ring, submit_fast_ring,
iogpu_error_diag, command_error_diag, queue_qos_diag,
submit_forward_progress_bridge, kcmd_field_a4/_5e3/_6bc,
stray_subtype3). D082's "11" was wrong; corrected.

## Finding 5 — post-restart acceptance predicate (before arming)

After the authorized rebuild→deploy→WS-restart, before creating the
sentinel on the *next* launch, require for the target generation:

1. `macws_final_composite` state `ready` with seq **advancing**
   (nonzero delta over the pre-window — D070: this desktop composites
   continuously; delta=0 is anomalous, not benign);
2. sentinel present at launch ⇒ trace file contains
   `COEXIST-PACE arm=1` (proves Finding-2 precondition);
3. only then interpret pace lines. A measurement on a generation
   failing 1–2 is invalid, not negative.

## Finding 6 — interpretation rule

`interval=` = selected pace floor (mode-dependent); `slept=` = actual
wait in `macws_coexist_wait_for_completion_slot`. Reading:

- `slept ≈ interval` — a full pace window was slept ⇒ artificial floor
  dominated that update boundary;
- `slept ≈ 0` — completion slot already due ⇒ **no** artificial wait
  that call;
- `0 < slept < interval` — partial alignment; with interaction wakes,
  this is the early-wake signature (message arrived mid-slot);
- mode transitions (`mode=`/`interactionAgeMS`/`renderAgeMS` on site 2)
  tie each reading to which path selected the pace.

That is the share-lesson question ("artificial wait vs service-message
availability") answered directly.

## Unchanged prerequisites

Activation still requires rebuild + redeploy (sign/trustcache) + WS
restart under session/rollback protocol + launcher-side sentinel
creation — a separate named grant. The current generation remains
unmeasurable (deployed binary lacks the helper; umbrella is cached and
confounded). One `access(2)` cost note for future ports: this pattern
is acceptable only because both sites are throttle-bounded; do not
reuse it on unbounded hot paths.

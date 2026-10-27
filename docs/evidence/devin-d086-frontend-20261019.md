# D086 — corrected frame-pacing diagnostic plan (D084 + D085 backend findings)

Plan only — **not applied**. Supersedes the D084 revision's Finding-3
design, which the backend showed deadlocks arming. All line numbers
re-pinned against the current checkout (Finding D).

## Finding A — revert to the uncached helper; keep registration

D084's `MACWS_DEFINE_STARTUP_FLAG` choice was wrong *for this sentinel
class*. Verified launcher mechanics: `enable_experimental_if_requested`
(`macos_gui.sh:5725`) `rm -f`s every diagnostic sentinel, re-`touch`ing
them only under `--diagnostics` (:5752–5758), which itself requires
`--experimental` (:5689) — i.e., arms the umbrella = the confound.
`production_preflight` (:3768; first call `clear_diagnostic_state`
:3771) sweeps all registered survivors at :5959/:6043. So a registered
sentinel cannot exist at WS launch: created before → swept/rejected;
created after → a *cached* flag has already latched false. No arming
window.

The established pattern for this class is **registered AND uncached** —
exactly `macws_keyboard_latency_diagnostics_enabled` (`tsv:362` +
`mac_hooks.m:9074`): registration buys `clear_diagnostic_state` sweep in
both namespaces at next launch; uncached `access(F_OK)` buys the only
real arming window — post-launch creation under grant (D041 precedent).

```c
static BOOL macws_frame_pace_diagnostics_enabled(void) {
    return access("/tmp/macws_frame_pace_diagnostics", F_OK) == 0;
}
```

Per-call `access(2)` is justified solely by the existing throttle
bounds (site-1 `<=4 || %600`, site-2 mode transitions) — restated as a
porting caveat. No `macos_gui.sh` edit is needed under this design
(the D084-required 4th file disappears); the launcher is untouched.

## Finding B — arm witness moves to first-use

D084's emits at :6479/:6483 fire at WS **launch**, before any
post-launch sentinel exists — under the uncached helper they would
early-return and never write. Move the witness into the trace helper:

```c
static void macws_frame_pace_trace(const char *format, ...) {
    if (!macws_frame_pace_diagnostics_enabled() || !format) return;
    static _Atomic int armed_acked = 0;
    if (!atomic_exchange_explicit(&armed_acked, 1, memory_order_acq_rel)) {
        /* first gated call after arming — one line, ever */
        char ack[96];
        int n = snprintf(ack, sizeof(ack),
            "COEXIST-PACE sentinel-armed arm=%d\n",
            atomic_load_explicit(&g_macws_iomfb_coexist_swap_cancel,
                                 memory_order_acquire));
        /* same open/write/close block as below */
    }
    /* vsnprintf+clamp, open /tmp/macws_frame_pace.trace O_APPEND,
       single write(2), close — verbatim macws_keyboard_latency_trace
       shape (:9082–:9097) */
}
```

`g_macws_iomfb_coexist_swap_cancel` (:1503) is file-scope and in scope
at the helper's :9076 placement. Four-state disambiguation at whatever
instant the operator arms:

| trace content | meaning |
|---|---|
| `sentinel-armed arm=1` + pace lines | armed, bounds reached |
| `sentinel-armed arm=1`, no pace lines | armed, bounds not hit / sites not run |
| `sentinel-armed arm=0` | coexist translation not armed this generation |
| empty/absent trace | sentinel never observed |

## Finding C — pre-arm trace removal + generation freshness

`/tmp/macws_frame_pace.trace` is `O_APPEND`, unregistered
(keyboard-latency precedent), never swept → a prior generation's
`arm=1` line could falsely satisfy acceptance. Protocol fix (grant
text): the pre-arm step is `rm /var/mnt/rootfs/tmp/macws_frame_pace.trace`
**then** `touch …/macws_frame_pace_diagnostics` (host spelling; the
chroot path is `/tmp/…`). Acceptance requires: trace file exists,
mtime ≥ target generation's WS lstart, first line =
`COEXIST-PACE sentinel-armed …`. Same class as the D041 lifecycle
runbook gap — stated explicitly here.

## Finding D — re-pinned line numbers (current checkout)

| Item | Line(s) |
|---|---|
| `macws_runtime_diagnostics_enabled` (cached umbrella) | :273–:292 |
| `macws_filtered_fprintf` decl/impl | :331–:332 / :333–:340 |
| `#define fprintf macws_filtered_fprintf` | :341 |
| startup-sentinel convention comment | :343–:349 |
| `g_macws_iomfb_coexist_swap_cancel` | :1503 |
| arm store + armed fprintf | :6477–:6480 |
| mismatch fprintf | :6483–:6485 |
| `macws_keyboard_latency_diagnostics_enabled` | :9074–:9075 |
| `macws_keyboard_latency_trace` | :9082–:9097 |
| site-1 coexist flag check | :20050–:20051 |
| site-1 diagnostic gate | :20056 (bound `<=4‖%600` :20060) |
| site-2 diagnostic gate | :27395 (mode-transition bound :27400) |
| `enable_experimental_if_requested` | `macos_gui.sh:5725` |
| `--diagnostics requires --experimental` | `macos_gui.sh:5689` |
| `production_preflight` def / calls | `macos_gui.sh:3768` / :5959, :6043 |
| `clear_diagnostic_state` | `macos_gui.sh:3531` (calls :3771, :3976) |

Umbrella count stands corrected: 2 env + **12** `access()` sentinels
(:279–:290).

## Finding E — audit + rollback restated

- **Audit check**: after the tsv row is added and
  `macws_diagnostic_flags.sh` regenerated, run
  `misc/audit_runtime_switches.py` — the declared check that no source
  sentinel is unregistered. The unregistered `.trace` literal is
  tolerated (keyboard-latency precedent).
- **Rollback (D082 §5 restated)**: revert to the prior hash-pinned
  signed libmachook dylib (retained deployment artifact) + WindowServer
  restart under the same session/rollback/watchdog protocol; `rm` the
  sentinel and `.trace` in both namespace spellings — plus the
  registered-sweep backstop at next launch since the flag is
  registered.

## Patch size (honest count)

3 files, no launcher change: `mac_hooks.m` ≈33 lines across 3 regions
(uncached helper :9076 area; trace helper + first-arm ack; site-1 +
site-2 conversions keeping the umbrella `fprintf` untouched);
`docs/runtime-switches.tsv` +1 `flag` row (`production=off`);
regenerated `macws_diagnostic_flags.sh`.

## Unchanged prerequisites

Activation still requires rebuild + redeploy (sign/trustcache) + WS
restart under separately granted session/rollback protocol, then
post-launch sentinel creation + trace removal as its own granted
device mutation (D041 precedent). Current generation remains
unmeasurable; umbrella stays cached-once and confounded.

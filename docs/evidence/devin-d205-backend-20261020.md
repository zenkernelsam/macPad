# D205 — minimal WS diagnostic observer: concrete review packet

Task: `coordination/_relay/devin-205-backend-task.md`. Read-only.
Scope = the D198 smallest milestone: ONE edge pair — `CGXRunOneServicesPass`
PASS_ENTER/PASS_EXIT — driving the accepted D141 ring. D200's ABI audit
is active; this packet uses only frozen D140/D142/D198 sources and the
accepted commits `cd3bc6d`/`f33b5e2`/`b451c03`/`633f34f`. **Every
build/deploy/device command below is PROPOSED only** — this packet
authorizes nothing.

## Proposed product edits (exact)

### `libmachook/mac_hooks.m` — one new gated block
Adjacent to the SENDEROBS block (~`:6116`), inside the SkyLight
`loadImageCallback` arm — same `isWS` executable check:

```c
/* proposed only — D205 observer arm site */
static const uint8_t skylight_22f82_uuid[16] = {
    0x96,0x67,0x6a,0x53,0xb1,0xe0,0x3d,0x7e,
    0xb9,0x8b,0xb7,0x38,0x73,0xcd,0x18,0x80 };   /* already exists */
static const uint32_t kPassPrologue[4] = {       /* NEW constant */
    0xd503237f, 0x6db63bef, 0x6d0133ed, 0x6d022beb };
/* bytes at SkyLight+0x33C63C (D140 frozen):
 * pacibsp / stp d15,d14,[sp,#-0xa0]! / stp d13,d12,[sp,#0x10] /
 * stp d11,d10,[sp,#0x20] */

BOOL svcObs = isWS && macws_svctime_observer_enabled();  /* NEW sentinel */
if (svcObs && permitFn) {
    /* metadata FIRST (C3 order), then arm, then permit+hook */
    d141_set_generation(getpid(), skylight_22f82_uuid);
    if (d141_gate_init(SVC_OBS_SENTINEL) == 1 &&
        !macws_mach_msg_all_enabled()) {       /* D8 exclusivity */
        int p = permitFn(header, skylight_22f82_uuid, 0x33C63C,
                         kPassPrologue);
        if (p == 0)
            MSHookFunction((void *)((uintptr_t)header + 0x33C63C),
                           (void *)macws_svc_pass_wrap,
                           (void **)&macws_svc_pass_orig);
    }
}
```

Wrapper (new static, real signature `int f(int, double)` per D140/D200
caller idiom — verify against D200's final signature line):

```c
static int (*macws_svc_pass_orig)(int, double);
static int macws_svc_pass_wrap(int x0, double d0) {
    d141_emit(D141K_PASS_ENTER, 0, 0);   /* lock-free; no I/O (D9)  */
    int r = macws_svc_pass_orig(x0, d0);
    d141_emit(D141K_PASS_EXIT, 0, 0);
    return r;                           /* return value untouched  */
}
```

(`d141_wrap_pass`'s `(uintptr_t(*)(void*),void*)` signature does not
match the 2-arg target — a dedicated wrapper is required glue.)

### `libmachook/Makefile` — link accepted sources
Add `../analysis/d141-svctime-candidate/d141_svctime.c` (and, only if
the in-process drain below is accepted, `d202_drainser.c` +
`d169_svcdecode.c` — or vendor copies; decision flagged).

### Sentinel (new, dedicated)
`/private/tmp/macws_svctime_observer` inside the chroot — NOT the
runtime-diagnostics umbrella and NOT the senderObs sentinel (D043
discipline: dedicated switch so the observation is causal).
`d141_gate_init` probes it once at init; absent → `DISARMED` → emits
no-op → **zero output by construction**.

### D8 enforcement
`macws_mach_msg_all_enabled()` is env-gated and read once; the arm
site refuses arming when it is set. Symmetric direction (MACH_MSG_ALL
refusing while armed) is unnecessary — env is fixed at process start.

### Bounded collection without per-frame I/O
Ring emit is lock-free CAS+copy (D9) — no write/alloc/blocking.
Proposal: a sentinel-gated drain thread calls the committed
`d202_collect`+`d202_write_stream` on a bounded interval and rotates
v3 files under a size-capped diag dir; simplest alternative is a
single drain-and-write at observer stop. Either is **missing glue**;
the APIs exist.

## Existing analysis APIs vs missing production glue

**Existing (accepted, unchanged):**
- `d141_set_generation/get_uuid/gate_init/emit/drain` + counters —
  `cd3bc6d` (first-set latch, full-UUID accessor)
- `d202_collect`/`d202_write_stream` bounded batch + exact v3 file —
  `633f34f`
- `d169_stream_load`/`d169_decode` offline — `f33b5e2`
- permit contract + provider `MACWSHookPermitV2`, 16-byte trampoline,
  isWS+sentinel gate pattern — in production already (SENDEROBS)

**Missing glue (all new, all diagnostic-only):**
1. The wrapper fn + orig slot + permit constant `kPassPrologue`
2. New dedicated sentinel name + `macws_svctime_observer_enabled()`
3. Arm site ordering: `set_generation` → `gate_init` → permit → hook
   (C3), plus `getpid()` pid and the pinned uuid constant
4. D8 arm-time refusal when `MACWS_MACH_MSG_ALL` is set
5. Drain consumer (thread or at-stop) + output path under a bounded
   dir — using committed `d202_*` APIs
6. Build wiring (vendored source or linked candidate)

## Bounded device acceptance protocol (all proposed only)

Preserves the healthy generation: first run is sentinel-off and must
produce zero observer artifacts; no restart/install rights assumed
beyond whatever channel already exists.

1. (proposed) build libmachook slice with the observer, deploy via
   existing postinst path — sentinel ABSENT.
2. (proposed) normal WS start → witness: healthy desktop
   (final-composite advancing), **zero** diag-dir files, no stderr
   install lines.
3. (proposed) create sentinel, next WS start → run ≥2 min idle +
   one Dock interaction; collect drain output.
4. (proposed) offline acceptance on collected v3 streams:
   `uuid_valid=1`, uuid = `96676A53…`; records PASS_ENTER/EXIT
   paired on ONE dominant tid (services tid); monotonic `ts_ns`;
   drops=0 at idle; decode scores only when paired with a probe
   stream — observer alone emits no timing claim.
5. (proposed) sentinel-off cold start → repeat step 2 (inert revert
   witnessed before any further work).

## Unresolved ABI items awaiting D200 (not blocking this packet)

- `x0` flag semantics (poll/nonblocking?) and `d0` deadline semantics —
  inferred only; irrelevant to an entry/exit wrapper (passthrough).
- Return-contract verification against WS-side state — passthrough
  makes this inert for the hook.
- Which port the internal `mach_msg*` receives on — matters only for
  leg interpretation, not bracket correctness.
- Whether WS-internal `mach_msg` legs nesting inside the PASS bracket
  should emit `MACH_CALL/RET` records at all — needs an explicit
  decision (recording them via existing interposes is possible but
  changes the stream's tid population; NOT assumed here).

## Non-actionable conditions

Packet is void if: D200 revises the site (not a clean pacibsp
function-start, relocatability fails, or the single-`retab` funnel
finding changes); the prologue bytes differ in the shipped image;
provider trampoline cannot hold the detour; permit registration
semantics change; or any acceptance step requires a device channel
that does not exist.

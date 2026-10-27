# D090 — errata: optional pid binding for the `sentinel-armed` ack

Offline errata to `docs/evidence/devin-d088-frontend-20261019.md` only.
No plan structure change; nothing applied; no execution implied.

## Amendment (in scope — one field on an already-planned line)

The armed-edge acknowledgement carries the emitting process id:

```
COEXIST-PACE sentinel-armed arm=%d pid=%d\n
        g_macws_iomfb_coexist_swap_cancel value , getpid()
```

`getpid()` is already used 37× in `mac_hooks.m`; the field rides the
existing single `snprintf`+`write(2)` ack — no new site, file, helper,
or dependency. Still ≤1 line per arm window.

## Why it helps

Binds the ack to the WS process instance, closing the stale-trace
false-pass: a prior generation's trace left behind by a skipped
pre-arm `rm` (protocol violation) would carry the old pid — detectably
different from the current generation's lstart-bound pid — instead of
silently satisfying the first-line check. The `rm`-before-`touch`
ordering and mtime ≥ lstart checks remain the primary freshness
guarantee; `pid=` is the belt-and-suspenders witness.

## Status

Optional, recommended. Plan otherwise unchanged — same 3 files,
≈45 lines / 3 `mac_hooks.m` regions, same registration, audit,
rollback, and authorization prerequisites. Acceptance predicate
gains one optional check: ack `pid=` equals the WS pid from the
pre/post `pid+lstart` witness set.

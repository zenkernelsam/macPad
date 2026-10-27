# D098 — pixel artifact inventory and freshness boundary

Offline inventory only; no device access or capture.

## Saved current-generation artifacts

There are seven saved pixel images associated with WindowServer generation
10447:

| Boundary | Artifact(s) | Dimensions | Sequence evidence |
|---|---|---:|---|
| iPad panel via Host/CARenderServer | Six D041 `frame-*.png` files under `docs/evidence/devin-d041-input-boundary-20261019/` | 2732×2048 | D041 records producer 10447 and final-composite seq 695→1176 during its bounded input test |
| VNC RFB framebuffer | `docs/evidence/devin-d094-frame-20261019.png` | 1366×1024 | Same PID/lstart; state brackets capture at seq 12526→12530 |

No later panel-boundary image exists in the saved tree. The later ready/sequence
reads prove continued composite delivery to displayd, not panel rasterization.
Therefore the accepted freshness statement remains: panel pixels were
witnessed at seq~1176; panel-boundary freshness after that point is unproven.
One repeat using the already accepted `macws_ipados_capture` path, correlated
to a later sequence, closes that freshness gap.

## Reopen contract

No new path is required. Reuse `macwshostd/main.m`'s existing exact-PID reopen
flow: snapshot prior generation, issue reopen, then require a strictly newer
metrics generation and a visible-window entry. D094/D041 did not exercise a
new reopen runtime witness.

Evidence source: D041, D094, and the saved image inventory; hashes are
recorded in D037/D041/D094 and in the D094 layered-acceptance ledger.

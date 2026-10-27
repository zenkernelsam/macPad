# devin-206-backend-result

Status: **PENDING reviewer acceptance** — typed PASS wrapper contract
candidate under `analysis/d206-pass-wrapper-candidate/` only; nothing
staged, no commit.

## Delivered

`d206_wrap_pass(int(*)(int,double), int, double)` — PASS_ENTER via
accepted `d141_emit`, orig called exactly once on its typed signature
(no wrong-type cast), PASS_EXIT, int result verbatim. errno isolated
both directions (emit is not errno-neutral). No locks/alloc/write/
block around orig; non-returning orig → no EXIT (documented).

## Tests

w1: `-0.0`/int bits verbatim, return/errno preserved both ways, called
once, same tid, adjacent seq, monotonic ts, gen/pid. w2: finite
double + zero return. w3: disarmed → orig runs, zero records. w4: two
threads → 2 tids, per-tid ENTER→EXIT. w5: errno-isolation proof.

## Exact results

`bash analysis/d206-pass-wrapper-candidate/run_tests.sh`:
ASan+UBSan **33/0**; TSan **33/0** (real two-thread w4); arm64
`Mach-O 64-bit object arm64`.

No hooks/permits/WS/SkyLight/sender/device/product integration.
Evidence `docs/evidence/devin-d206-backend-20261020.md`.

(handover to codex now)

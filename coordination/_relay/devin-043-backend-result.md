# devin-043-backend-result — record-only sender observation candidate

Packet: `devin-043-backend-task.md`. Scope honored: **source + local
builds/tests only** — no deploy, no restart, no marker/device change,
no commit/package/install. Live desktop untouched (never contacted for
mutation this round; composite still `ready producer=10447`).

## ABI/polarity audit — done before design (capstone, SkyLight-22F82)

- Gate `+0x318464`: ignores a0/a1; `gCurMsgAudit` NULL or size<0x34 →
  ret 0; token = container+0x14 (32B); rightDesc @`0x1d3dcb6c8` (40B);
  `paciza`-signs impl; returns dispatcher's w0.
- Dispatcher `+0x3407ec`: `(audit*, desc*, impl)` — pid==0→`desc[0x20]!=0`
  (auid); pid==self→1; cache known-bits→`cached&desc[0x10]`; miss→
  `impl(audit, desc[0])` with `impl_rc!=0`→allow else deny bits.
- Impl `+0x340da0`: `(audit*, service)` — lazyChecker cell
  `@0x1d8ba6b50` NULL→resolve→still NULL→**ret 0**; else
  `checker(service,&tok,0)`, `cset eq` → **`impl = (checker_rc==0)`**.
  `impl_rc==0` = {unresolved checker} ∪ {checker denial} — split only by
  lazy_pre/post, which the record carries.
- Async: resolved symbol is synchronous
  `TCCAccessPreflightWithAuditToken`; no async callback at this
  boundary. Raw checker rc collapsed by `cset eq` — not observable at
  SkyLight boundary (documented limit, not a hook).

## Implementation (candidate diff, not deployed)

- `libmachook/MacWSSenderObservation.{h,c}`: dedicated sentinel
  `/tmp/macws_sender_observation` (NOT the runtime-diagnostics
  umbrella — no display-producer coupling), live-disarm per record,
  256×136B static ring + raw `write()` log, site-call/drop counters,
  errno save/restore, byte-verified shared prologue pins.
- `mac_hooks.m` (+236/−19): gate wrapper emits gate record (existing
  `#### CGXSYNTH` fprintf now explicitly conditioned on
  runtime-diagnostics only — identical observable behavior); new
  dispatcher + impl wrappers call orig first, record after, return
  verbatim; installs require `isWS` + sentinel + per-site permitV2
  (uuid `96676A53…` + offsets `318464/3407ec/340da0` + pins).
- `libmachook/Makefile`: +1 file. `misc/test_sender_observation.c`:
  host contract test.

## Tests — ALL PASS (25 host checks)

default-off stores/emits nothing (calls still counted); armed →
136B records byte-verbatim ring+log; budget → 256 cap + 46 overflow
drops; live disarm → dropped, calls continue; errno preserved; all
three pins byte-equal the image; corrupted pin provably mismatches.
Not host-coverable (D036/D037 precedent): real MSHook install,
PAC-signed orig, live UUID check.

## Build artifacts (local build object — unsigned-deployable hashes differ)

`.theos/obj/libmachook.dylib` sha256 `135f3c96…b035c6`; uuids
`F65A7DE0…` (arm64) / `0B695A35…` (arm64e); 3 exported pins + 12 obs
symbols present. Source sha256 prefixes in the evidence doc.

## Discrimination matrix (future bounded arm → producing layer)

gate-rc=0/no-inner+audit-absent → early exit; disp-rc=0+no impl+
token.pid==0 → auid `desc[0x20]` deny; disp-rc=0+no impl+pid≠0/self →
cached deny bits; impl-rc=0+lazy_post==0 → resolver failed (TCC never
consulted); impl-rc=0+lazy_post≠0 → checker returned ≠0 — only that row
promotes `responsibility103` to the next variable. No row infers cause
from counts alone; bytes in the same record are required. Plan for a
future authorized arm is in the doc §6 (sentinel before WS launch, one
canonical post, log read, disarm; display permits untouched).

Files: `docs/evidence/devin-d043-sender-observation-candidate-20261019.md`.
Tree: 2 modified (mac_hooks.m, Makefile), 3 new files; `git diff
--check` clean; no commit.

(handover to codex now)

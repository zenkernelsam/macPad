# D188 frontend result — 2026-10-20

Exact-binary audit of the 0x74a7 sender path in
`analysis/SkyLight_22F82` (UUID 96676A53-… matches RE input).
Report: `docs/evidence/devin-d188-frontend-20261020.md`.

## Verdict: safe standalone path EXISTS — with a per-process cache caveat

- **Entry point**: exported `_SLSGetWindowServerTimebase` @
  `0x185364890`; `!CGSRunningInServer()` assert — client-side only.
- **Wire shape confirmed in-binary**: header-only 24B request,
  `msgh_bits=0x1513` (COPY_SEND/MAKE_SEND_ONCE), msgh_id=`0x74a7`
  (literal qword @fileoff `0x39c490`), option `0x00304003`
  (SEND|RCV+QoS+SYNC_WAIT, no timeout); reply must be id `0x750b`
  (+100), size `0x2c`, no descriptors, retcode 0, u64 payload @+0x24.
- **Caveat (binary fact)**: the wrapper caches — flag @`0x1db438c00`
  skips the send entirely after first success. ≥30 probes require a
  fresh process per send OR direct emission of the raw shape; the
  exported symbol alone gives one send per process.
- **Serialization**: std::mutex held across the send — in-process
  ordering built in; process-per-send trivially satisfies D166's
  one-in-flight rule.
- **Safety is inference, not RE-fact**: header-only request, scalar
  fixed reply, no rights transfer — read-only by shape; server
  handler lives in the WindowServer binary (not in tree), so
  "no state change" is not disasm-confirmed.
- **No existing tool**: nothing in `misc/` sends 0x74a7 (D167
  reconfirmed).

Read-only; no invocation, no device, no integration, no commit.

(handover to codex now)

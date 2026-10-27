# D167 — serialize-one-probe enforceability audit

Frozen inputs: `misc/macos_ws_event_probe.c` (793 lines), D100–D138
probe-msgid history, D164+D166 amendments. Source facts cited;
runtime behavior labeled as such.

## 1. The current probe does not send `0x74a7` — source fact

- `macos_ws_event_probe` invokes `SLSPostEventRecord` via
  `dlsym(RTLD_DEFAULT, …)` (:159, :575) — the `_XPostEventRecord`
  path that returns WS's `rc=1002` (:10, :592).
- `0x74a7` is a *different* routine: `_XGetWindowServerTimebase`
  (D100 :15/:176; D129 :82) — no in-tree tool sends it.
  Serialize-one-probe is therefore **vacuously unenforceable today**:
  the enforcing harness would have nothing to serialize.

## 2. What the existing harness *would* enforce, if a 0x74a7 sender existed

- Probe modes (`inventory`/`post`/`postmin`/`mutate`, :103-109) each
  perform exactly ONE `SLSPostEventRecord` then print rc — a
  synchronous call is one-in-flight per thread **by construction**
  (source fact: no threading, no async send, no queue).
- The `usleep` loops (:540, :557) are settle-waits inside inventory
  phases, not request pacing.
- ≥30 samples via sequential process invocations (shell loop) gives
  the strictest possible serialization — process exit precedes the
  next send. Each invocation mints a fresh connection/sender, which
  is harmless to the rule (serialization is temporal, not
  identity-based).

## 3. Retries / timeouts — invisible to the tool, visible only on-record

- The probe surfaces only the stub's final `rc` — SkyLight client
  stubs hide `mach_msg` internals; an internal timeout-retry emits a
  duplicate same-msgid CALL the CLI **cannot** report (source-level
  reasoning; stub internals not disassembled here).
- Detection must therefore be record-side: any window containing
  >1 `CALL(0x74a7)` is contaminated — void that sample span, flag in
  report (never "pair the first and discard the rest" — ordinal
  alignment is unprovable after a duplicate).
- A timed-out trap surfaces as `RET(0, MACH_RCV_TIMED_OUT)` —
  excluded from pairing by the D162 predicate; the CALL stays
  unmatched → the sample fails closed, not silently.

## 4. Fail-closed scoring rule for ≥30 serial samples (proposed)

Per invocation i:

1. Emit exactly one `0x74a7` via the synchronous client call; the
   record window must contain **exactly one** `CALL(0x74a7)` —
   otherwise void the sample AND its span (duplicate/retry
   contamination).
2. Require the ordered span `CALL → HANDLER_BEGIN(0x74a7) →
   REPLY_ENQUEUE → RET` on seq-contiguous records; any `record.seq`
   gap inside the span voids it (D166 rule).
3. A RET with `aux != MACH_MSG_SUCCESS` fails the sample closed
   (unpaired by predicate; CALL remains unmatched — expected, not
   corrupt).
4. Ambient-sender control: quiesced workload is the arm control
   (D165); a second `CALL(0x74a7)` from any source inside the window
   is indistinguishable (no port field) — quiescence is load-bearing,
   state it as a scoring precondition, not an assumption.
5. ≥30 valid samples required per arm; invalid samples are reported
   with counts, never silently dropped from the report.

## 5. Cross-process note (integration gap, not today's question)

MACH_CALL/RET legs live in the probe's own ring; HANDLER_BEGIN in
WS's ring — separate `g_ring` instances per process. The join needs
a shared sink or timestamp-merge of per-process drains
(`CLOCK_MONOTONIC` is a shared domain — merge is feasible; it is a
missing integration piece already listed in D164 §9).

## Verdict

Serialization is trivially enforceable *in principle* — synchronous
single-post semantics give one-in-flight for free — but no tool
sends `0x74a7` today, so enforcement has no object. The blocking
item is a `0x74a7` sender (client stub or raw MIG emit), not the
serialization rule. Retry/duplicate contamination is detectable only
on-record — the fail-closed rules above cover it.

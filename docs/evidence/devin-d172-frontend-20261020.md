# D172 — independent acceptance review: D171 svcdecode vs D170 G1–G12

Verdict: **CHANGES_REQUESTED** — two substantive findings, four minors.
Exact source: `analysis/d169-svcdecode-candidate/` @ uncommitted
candidate; gen encoding verified at `d141_svctime.c:66-69`.

## F1 — gen identity: no shared-image verification; gen_filter structurally unusable (G2 PARTIAL-FAIL)

`gen = (pid<<32) ^ uuid_lo64` — `uuid_lo64` = first 8 bytes of the
image UUID, shared across processes; `gen_lo32` = uuid-derived and
image-stable, `gen_hi32` = pid ^ uuid_hi32. Probe and WS pids differ,
so probe.gen ≠ ws.gen **for every legitimate pair**.

- `stream_gen_check` (:80-94) enforces within-stream uniformity and
  full-gen equality to `opts->gen_filter` (:168-169). Any nonzero
  filter compares full 64-bit gen → exactly one of the two streams
  must fail E_GEN_MISMATCH — the filter **cannot** express
  "same image, different pid". t18 (:423-426) asserts
  `gen_filter=WSGEN` rejects the probe stream as *correct* — the
  fixture blesses the defect.
- With `gen_filter=0` there is no cross-stream identity check at all:
  two streams from different SkyLight images decode together
  silently. Fixtures use unrelated gen constants
  (`PGEN=0xB6B6CAFE`, `WSGEN=0xA5A5DEAD` :23-24) — no shared
  uuid component exists to test against.

Required (minimal):
1. Cross-stream image check computable without knowing the uuid:
   `probe.gen_lo32 == ws.gen_lo32` AND
   `(probe.gen_hi32 ^ probe_rec.pid) == (ws.gen_hi32 ^ ws_rec.pid)`
   (records carry `.pid` — this recovers uuid_hi32 per stream).
   Mismatch → new error class (e.g. `E_IMAGE_MISMATCH`), fail closed.
2. Redefine `gen_filter` to apply to the image component
   (`gen & 0xFFFFFFFF`), or add `image_lo32` option — the current
   full-gen filter is dead code for its purpose.
3. Fixtures with REALISTIC gens: `(pidA<<32)^U` vs `(pidB<<32)^U`
   sharing `U` → accept; differing `U` → reject; filter-by-image →
   correct stream selection.

## F2 — false seq-gap void under nonmonotonic ts (G6 PARTIAL-FAIL)

`win_has_gap` (:58-63) voids when footprint `[min..max]` isn't covered
by in-window seqs — but the footprint is collected in merged-ts order
only while the span is open (:212-213). A record whose seq lies inside
that range but whose `ts_ns` sorts outside the span (before CALL, or
after the closing RET — claim order ≠ ts order, C4-legal) is never
added → false hole → `gap_voided`.

Reachable construction: ws seqs `1:HANDLER(1700), 2:foreign(ts=50),
3:REPLY(1800)` — footprint `{1,3}` voids at seq 2, which exists
in-stream. t12 (:253-266) is the closest fixture but its out-of-order
record lands inside the window — the case is not covered.

Fix: evaluate the footprint's seq interval against the whole stream's
sorted seq set (already built in `stream_seq_stats`) — presence
anywhere in-stream disproves the hole; absence = true in-span drop.
Add fixture: in-range-seq record ts-sorted outside the span → span
must survive.

## Minors

- F3 `call_absent` (:357) conflates "no MACH_CALL records"
  (uninstrumented client) with "CALLs but none msgid=PM" — C2's
  explicit-fail choice is right; split the flag for diagnosis.
- F4 no end marker in format — exact `hdr+n*64` is strictly stronger
  than G1's marker row (truncated+trailing both distinct classes);
  README/format spec should state "no end marker; exact-length is the
  boundary" or a marker-appending producer hits E_TRAILING.
- F10 `D169_PENDING_CALLS`/`D169_MAX_PROBES` overflow: excess CALLs/
  spans degrade to uncounted bookkeeping — document or counter it.
- F11 `resolve_svc_tid` 64-tid cap silently skips (:132) — bounded,
  document.
- F12 `stream_seq_stats` malloc failure yields `gaps=0` (:103-104) —
  reconcile_flag can false-fire when dropped>0; set a stats-unknown
  bit instead.

## Verified-correct rows

- G1/G8 parser: exact-boundary + seven distinct failure classes
  (t16 covers all).
- G3 tid argmax + AMBIGUOUS_TIE + INSUFFICIENT (t9 both arms).
- G4/G5 merge + second-CALL ambiguity, both sides excluded (t1/t6).
- G7 non-success RET consumes nothing; delayed success pairs (t2);
  open-at-EOF reported (t10).
- G9 counters verbatim; `gaps < dropped` reconcile direction correct
  (t8).
- G10 median + `max(8ms, wait/2)` cluster bound (:369-370), scored
  samples only — honest accounting.
- G11 batch bracket, msgid-bound pairing, wrong-tid/outside-bracket
  flags (t11/t15); C7 reply-id cross-check (t13).
- C5 open spans reported never auto-closed; poisoned-span double
  exclusion consistent.

## Required for acceptance

F1 and F2 fixes + fixtures; F3–F12 as documentation or small
counters at backend discretion (F3 recommended). Then re-review.

# D166 — request→handler correlation uniqueness audit (amended D164)

Inputs: amended D164 §§3/6, accepted record schema
(`d141_svctime.h:59-60` — `msgid`, `aux` only; **no port field**),
D142 cautions, D140 edges.

## 1. The record cannot carry the full D142 key

D142's join key was `(msgh_id − 100, local_port)`. The fixed 64-byte
`d141_record` has `msgid` and `aux` (aux = option verbatim on CALL,
kr verbatim on RET — neither is free for a port tag). `local_port` is
**not recordable** on this schema. What remains: `msgid`, `tid`,
`gen`, and claim-order position.

## 2. (msgid, tid, gen) is a group key, not a unique key

With ≥30 identical `0x74a7` probes in one arm, every probe produces
identical `(msgid, tid, gen)` tuples. Two coexisting identical-keyed
requests are indistinguishable — and that's reachable, not just
theoretical:

- retries after a client-side timeout emit a duplicate CALL with the
  same msgid;
- pipelined probes (≥1 in flight per sender) coexist by design;
- a second sender reusing msgid values collides outright, and the
  absent `local_port` means the schema cannot separate senders.

## 3. What is directly measured vs inferred

- **Measured**: per-record kind/msgid/tid/ts/order — all verbatim.
- **Inferred (decoder)**: that the nth CALL(0x74a7) on tid T
  corresponds to the nth HANDLER_BEGIN(0x74a7) on tid T. Ordinal
  pairing is sound **iff** (a) the span between them is seq-contiguous
  (a gap could be a dropped CALL or a dropped HANDLER — the gap
  carries no kind, so ordinal alignment silently shifts) and (b) only
  one same-key request is in flight at a time per tid.

## 4. Minimum sufficient rule — serialize-one-probe

Honest discriminator without schema change:

> At most ONE `0x74a7` probe is in flight per measurement window.
> The open span CALL→HANDLER_BEGIN(→REPLY_ENQUEUE)→RET is then
> unambiguous by construction — identical keys never coexist.
> Throughput is bounded by probe latency; ≥30 probes stays feasible
  serially.

Fallback for queued batches (weaker): ordinal pairing inside
seq-contiguous same-key runs only; any `record.seq` gap inside the
span voids pairing for that window (reported, not guessed). Sender
uniqueness (single probe sender) is an arm-level control that
substitutes for the missing `local_port` — state it as such.

## 5. Amendment appended to D164

§3 reply-correlation line rewritten to mark `local_port` as
schema-absent (decoder may use it only if the integration layer
records it elsewhere); §6 gains the serialize-one-probe rule and the
seq-contiguous-span requirement. Design scope unchanged.

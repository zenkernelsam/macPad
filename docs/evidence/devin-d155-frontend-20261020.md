# D155 — D150b Mach RET aux/API consistency check (exact source)

Frozen inputs: `d141_svctime.{c,h}`, `d141_svctime_test.c`, D152 packet
I5. Narrow scope; nothing else audited.

## 1. Exact current API

```c
void d141_mach_call(uint64_t msgid, uint64_t option);   // h:103
void d141_mach_ret (uint64_t msgid, uint64_t kr);       // h:104
```

Implementation (:154-159): `mach_call` emits `aux = option`;
`mach_ret` emits `aux = kr`. **RET cannot carry both `kr` and option
bits under the existing API** — one `aux` u64, one value passed.

## 2. D152 I5 wording vs API — inconsistent

The I5 contract line "RET leg … `aux` carries `kr` + option bits"
describes a capability the candidate API does not provide. Two
consistent resolutions:

- **(a) preferred, zero API delta**: drop "option bits" from the RET
  line. The option bits are already on the paired CALL record's aux
  (verbatim — T8 asserts `aux == 0x3000903`), and CALL→RET joins by
  same-tid nearest-unmatched — the decoder recovers option from the
  CALL leg. Revised wording: "RET leg: msgid = recv id iff
  `kr==SUCCESS && RCV`, else 0; `aux` = `kr` verbatim."
- **(b) alternative**: pack `aux = ((uint64_t)option << 32) | kr`
  (kern_return_t and mach_msg_option_t both fit 32 bits) — requires one
  documented packing line, no signature change; adds a packing-op to
  the emit path (still nonblocking, but D9's enumeration would need a
  one-word amendment — "pack kr+option").

Either is acceptable; (a) is the smaller contract. What must NOT
remain is the current line — it claims unimplemented behavior.

## 3. Test coverage for the existing API

- Packing/preservation: T8 asserts CALL `aux == 0x3000903` and RET
  `aux == 0x10000002` verbatim — covered for the current API shape.
- errno per-leg: NOT covered — `mach_call`/`mach_ret` go through
  `d141_emit`'s save/restore envelope (:124,:137) structurally, but no
  test asserts it per leg. D154 already added the per-leg errno
  requirement to the I5 test spec — consistent.

## 4. Model-vs-production boundary (stated, not guessed)

The candidate API is a **model** — production call sites live inside
`mach_msg_new`/`mach_msg_overwrite_new`/`mach_msg2_internal_new`,
which all hold `option`, `result`/`kr`, and the message buffer as
locals. Whatever the candidate signature chooses, the real wrappers can
supply both values; this API decision constrains only the record
contract, not the wrapper ABI. Distinguish accordingly in the task
packet — which already says legs are integration-scope (:I5 last
bullet).

## Verdict

One contract-line inconsistency found; two fixes offered — recommend
option (a), zero API delta: `aux` = `kr` verbatim, option bits
recovered from the paired CALL.

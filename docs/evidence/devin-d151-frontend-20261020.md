# D151 — source-verified Mach wrapper contract + independent CAP arithmetic

Frozen inputs: `libmachook/mac_hooks.m` wrappers (:13217/:13495/:13559),
D141/D143 candidate, D149/D150 packets. Source-only; no tests run.

## 1. Pre-call valid header fields — per wrapper

| wrapper | send fields pre-call | recv fields pre-call |
|---|---|---|
| `mach_msg` :13217 | valid when `option & MACH_SEND_MSG`: `msgh_id/bits/remote/local` in `message` (wrapper snapshots these pre-call as `preMessageID…` — required, see §3) | recv side has NO header — only `receive_name`/`receive_limit` scalars; `message` recv region is meaningless |
| `mach_msg_overwrite` :13495 | same in `message` | `receive_name`/`receive_limit`/`receive_buffer_size`; `receive_buffer` contents undefined |
| `mach_msg2_internal` :13559 | reconstructed from register packing: bits=lo32(arg2), remote=lo32(arg3), local=hi32(arg3), id=hi32(arg4), send_size=hi32(arg2) (:13564-13575; ABI RE-confirmed per in-source comment xnu-8792 :13544-13549) | `rcv_name`=hi32(arg5), `rcv_size`=lo32(arg6); `data` not a header yet |

## 2. Post-call valid fields — by result

| result | what is valid |
|---|---|
| `MACH_MSG_SUCCESS` + RCV | recv header valid — `message` (mach_msg, buffer overwritten), `receive_buffer` (overwrite, :13510 guard), `data` (msg2, :13586-13589). `msgh_local_port` = actual member port on portset receives |
| SUCCESS, no RCV (send-only) | only `result`; send buffer untouched (overwrite) / unchanged unless kernel modified (mach_msg combined-region caveat doesn't apply — nothing received) |
| `MACH_RCV_TOO_LARGE` | `msgh_size` is defined to carry the actual size; full header validity beyond that is **not quoted by in-source comments** — conservative rule below |
| `MACH_RCV_TIMED_OUT` | buffer contents unspecified — msgid untrusted |
| `MACH_SEND_*` / other errors | recv header invalid; only `result` meaningful |

**Corrected contract**: `d141_mach_ret` msgid = recv header id when
`kr == MACH_MSG_SUCCESS && (option & MACH_RCV_MSG)`; otherwise 0.
TOO_LARGE: record msgid=0 (conservative) — optionally a dedicated aux
flag marking truncated-recv where msgh_size is meaningful. This
replaces my D150 suggestion of "send id on send-only RETs" — adjacency
joins make that redundant; uniform 0-on-non-SUCCESS is simpler and
matches what each wrapper can actually trust.

## 3. Shared-buffer hazard confirmed in source

`mach_msg` uses ONE buffer for send+recv — a combined call's post-call
`message->msgh_id` is the **received** id. The send id must be captured
pre-call (the wrapper already does, :preMessageID at ~13326+). For the
services pass specifically: in a combined trap, CALL.msgid = reply id
(request+100, send side), RET.msgid = next request id (recv side).
The timing legs must follow the same snapshot discipline — the D150
contract line stands with this added clause.

## 4. Wrapper symmetry

All three have umbrella-gated send pre-call + recv post-call trace
legs → timing legs can sit at symmetric positions in all three.
Gaps to close in D150b scope: (a) the msg2 send-leg reconstructs a
header from registers — the timing CALL must do the same unpack;
(b) `mach_msg_overwrite`'s recv id lives in `receive_buffer` (distinct
from `message`) — RET leg must read that pointer, not `message`;
(c) the 40000–41000 debug filter (:13516/:13590) is trace-only — the
timing legs must NOT inherit it (all msgids required).

## 5. LIFO pairing — downgraded to sequential + signal-hazard

Checked whether mach traps can nest on one tid: the interposed calls
are kernel traps — no user code runs between a CALL and its RET inside
the wrapper, and the record path itself must not call mach (D10). So
within one tid, calls are strictly CALL→RET sequential — a stack
discipline is vacuous, "nearest unmatched same-tid CALL" degenerates to
adjacency.

**The real interleaving source**: a signal handler (or equivalent
async context) on the same tid calling mach_msg between a wrapper's
pre-call leg and post-call leg — genuine CALL(C)CALL(S)RET(S)RET(C)
interleaving, so same-tid nearest-unmatched IS still required, but
reframed: it's needed for async reentrancy, not handler-nesting.
The D6 test should simulate exactly that (emit legs in the interleaved
order) — which is what the original nested test did; the justification
changes, the test stands.

## 6. Independent CAP-boundary recalculation (exact rule `c − tail ≥ CAP → drop`)

```
fill 255 commits        claims 0..254,  tail=0,  emitted=255
park producer claim 255 (post-claim hook)
drain                   n=255, uncommitted=1, tail=255  (limit=256,
                        i=0..254 commit, i=255 breaks)
emit ×255               claims 256..510 → c−tail = 1..255 < CAP → all land
                        head=511, emitted=510, dropped=0
emit ×1                 c=511: 511−255=256 ≥ CAP → dropped=1
                        (511 = 255+256 = the c+CAP attempt — refused)
release park → commit   emitted=511
drain                   limit=min(511−255,256)=256 → 256 records,
                        order [DEADBEEF, followers-by-claim]; tail=511
```

**Corrected totals** (D150 §defect-2 confirmed against the rule):
`emitted==511`, `dropped==1`, `drained total==511`,
`attempts==512`, conservation `emitted+dropped==attempts` holds,
`uncommitted==1` cumulative, mid-window drain `==0`. The D149 figure
`emitted==CAP` remains superseded; D150's correction was right.

## 7. Exact unknowns for D150b

1. `MACH_RCV_TOO_LARGE` header validity beyond `msgh_size` — in-tree
   comments don't cover it; contract uses the conservative msgid=0 rule
   unless an xnu reference is produced.
2. Whether `CGXRunOneServicesPass` exposes a handled-count —
   unverified; the stream-derived count needs no such field.
3. Whether the production wrappers actually call timing legs at all —
   integration task scope; prototype only models positions.
4. TOO_LARGE/timeout numeric values on this platform for aux tagging —
   read from `<mach/message.h>` at integration time, not hardcoded.

## 8. Corrected contract lines for D150b (replaces D149 §item-5)

- CALL leg: msgid = send-header id if `option&MACH_SEND_MSG` (msg2:
  unpacked from registers), else 0. Send fields captured pre-call —
   mandatory in mach_msg (shared buffer).
- RET leg: msgid = recv id iff `kr==SUCCESS && option&MACH_RCV_MSG`,
  else 0; aux carries `kr` verbatim + option bits.
- Pairing: nearest unmatched same-tid CALL (required for async
  reentrancy; strictly sequential in the common case).
- Reply correlation `id−100` stays decoder-side.

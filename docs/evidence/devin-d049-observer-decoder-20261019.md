# D049 — Strict Observer Decoder + Evidence Matrix (independent)

**Scope (devin-049-frontend-task.md):** disposable/local strict decoder +
corruption matrix under `analysis/devin-d049-frontend`; correct "helper
validated" wording → host/local; no production edits, no device, no Git.

**Wording correction applied:** D048 doc + result now say "host/local
level fully validated … nothing here speaks for on-device results" —
the backend's arm64e/PAC helper is still running, not accepted.

**[CORRECTED D051:]** the arm64e helper result landed 2026-10-19 —
`misc/macws_sender_obs_probe.c` (`f435057a`) ran **15/15 PASS on
device** (helper-level only: real `*_invoke` bodies + mock originals,
PAC fn-ptr chain end-to-end). That is still NOT real-WindowServer
hook-install acceptance — that remains coordinator-elected review only.
See `devin-d048-isolated-observer-validation-20261019.md`.

---

## 1. ABI ground truth — derived from compiled fixture, not the header

`abi_fixture.c` includes the real `MacWSSenderObservation.h` and prints
`sizeof`/`offsetof` — the decoder's layout is checked against THIS,
never hand-derived:

```
sizeof=144   magic@0 u32 | version@4 u16 | site@6 u8 | flags@7 u8
seq@8 | tid@16 | invocation@24 (u64s) | rc@32 i32 | audit_size@36 u32
audit_ptr@40 | lazy_pre@48 | lazy_post@56 | arg_ptr@64 (u64s)
token@72[32] | desc@104[40]
MAGIC=0x4d534f42 VERSION=2 SITES gate=1 disp=2 impl=3
FLAGS audit=0x1 lpre=0x2 lpost=0x4 desc=0x8 root=0x10 stnd=0x20
```

Decoder `struct.Struct('<I H B B Q Q Q i I Q Q Q Q 32s 40s')` — explicit
little-endian, `F.size == 144` asserted at load.

## 2. Strict semantics (default = fail closed)

- every 144-B frame must carry magic + version 2 + site ∈{1,2,3} +
  flags ⊆0x3f + site/flag coherence (ROOT only on GATE, STANDALONE only
  on non-GATE, LAZY flags only on IMPL, no AUDIT_PTR without F_AUDIT)
- **interior** bad frame → `CORRUPT_FRAME` at offset, verdict=CORRUPT,
  halt — a torn write mid-file is not assumed tail-only
- **final** chunk <144 → `INCOMPLETE` with exact `have`/`missing` bytes —
  never silently dropped
- pointer fields (`audit_ptr`, `lazy_*`, `arg_ptr`) reject only
  **VA-space violations** (≥2^44 — above any plausible arm64e user VA);
  this is the honest bound: small synthetic values (test sentinels
  0x55) are legal, and *torn garbage landing inside the VA range is not
  decidable at this layer* — recorded as a limit, not smoothed over.
- `seq` file-order ↔ reservation-order: DUP_SEQ / OUT_OF_ORDER_SEQ /
  SEQ_GAPS are **integrity anomalies → verdict=ANOMALOUS, exit≠0**;
  BUDGET_EDGE (seq hits 255) is informational — the producer's 256-drop
  budget may be exhausted, making *further calls invisible* in the log.

## 3. Recovery mode (--recover) — explicitly marked, never silent

On a bad interior frame: byte-scan forward for the next magic; the
skipped span is logged `SKIPPED_BYTES(off,len,first_err)`; every record
parsed at/after resync is `untrusted: True`; every chain containing an
untrusted member is flagged `untrusted` — a torn chain is never
silently shortened to its trusted prefix. Verdict `RECOVERED_PARTIAL`
(exit≠0): recovery never upgrades to clean.

## 4. Zero-call inference guard

The decoder prints on every run:
`stored-record count is NOT a call count — absent records never prove
zero calls (drops/tears/budget)`. Completeness requires producer-side
`site_calls/dropped/snaps` counters (not in the log). SEQ_GAPS are
reported as *loss evidence*, never as missing calls.

## 5. Outcome matrix (the discrimination the 3 sites afford)

| class | meaning |
|---|---|
| `GATE_ARM_A_CONTAINER_NULL` | gate rc=0, no F_AUDIT — deny before creds |
| `GATE_ARM_B_UNDERSIZED` | gate rc=0, size<0x34 |
| `GATE_DENY_NO_DISP` | gate rc=0, no dispatcher record |
| `DISPATCHER_DENY(cache\|bits\|identity)` | disp rc=0 — cached-right verdict |
| `RESOLVER_FAIL(lazy never set)` | impl rc=0, lazy_post=0 → dlopen/dlsym failed, preflight never ran |
| `EVALUATED_NONALLOW collapsed{raw1|raw2}` | impl rc=0, lazy set — **NOT explicit policy denial** (API default collapse, D045/D046) |
| `EVALUATED_ALLOW(raw0)` | impl rc=1 |
| `STANDALONE_INNER` / `UNCHAINED_EMIT` | inner-site call outside a gate chain / direct emit |

Chain shapes (`CHAIN_FULL`, `GATE_ONLY`, `DISP_STANDALONE`, `NO_IMPL_ARM`,
`CHAIN_PARTIAL`) report missing sites as information, not corruption.

## 6. Test matrix — 21 checks, ALL PASS

`test_decode.py` generates every fixture: valid nested/standalone chains;
bad magic interior; wrong version; site 0/4/255; unknown flag bit;
truncated tail (INCOMPLETE missing=44); interior torn (strict→CORRUPT;
recover→SKIPPED_BYTES+UNTRUSTED chain); DUP_SEQ; OUT_OF_ORDER_SEQ;
SEQ_GAPS; gate-only chain (legitimate missing sites); resolver-fail;
evaluated-non-allow; budget edge; root-on-non-gate; lazy-on-gate;
empty file; **the real 256-record host fixture** (frozen host-test
output, `host_fixture.bin` sha256 `7e8bbfbf…`).

Two decoder bugs were caught by the matrix itself and fixed: anomaly
kinds that didn't escalate the verdict (integrity findings silently
CLEAN), and chains built only from trusted records (untrusted chains
never flagged). Plus one boundary correction: pointer-plausibility
initially rejected small synthetic values (0x55 test sentinel) — the
correct bound is VA-ceiling-only, documented.

## 7. Real fixture result (frozen host-test log, 36864 B = 256 recs)

`verdict=CLEAN records=256` + `BUDGET_EDGE` flag (live proof the
completeness caveat matters — the producer emitted more than 256 emits,
invisible in the log). Chain reconstruction: inv1/inv2 full
gate→disp→impl chains, inv3/inv7 standalone disp, inv5/inv6 gate-only,
inv8 impl, 245 unchained emits — exactly matching the test's emit
topology. Token decode shows val[1]=euid/val[5]=pid fields land where
the audit_token ABI puts them (synthetic values in the fixture).

## 8. Artifacts + hashes

```
analysis/devin-d049-frontend/
  obs_decode.py      sha256 ca076ef78ea97dc9… (decoder)
  test_decode.py     sha256 473af12a06cab97e… (21-check matrix)
  abi_fixture.c      sha256 59f4ee14cd33d876… (compiled ABI truth)
  abi_fixture        (built fixture binary)
  host_fixture.bin   sha256 7e8bbfbf601de33f… (256 real records)
  fixtures/*.bin     (19 generated corruption cases)
```

No backend device log exists yet — if one lands, decode it with
explicit scope (its token bytes are live identity material; decode
fields, don't dump raw). No 4th hook, no deploy, no device access.

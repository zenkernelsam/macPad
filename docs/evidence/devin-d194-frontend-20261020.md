# D194 — frozen D190 main/UUID audit

Frozen hashes (sha256, reviewed snapshot):
- `d190_main.c`  `d176774a164565acddc8634cc82d2a1ebbc566ad7a95307aa5fcff3a55d969fc`
- `d190_probe.c` `189bc7870638b3b0761e7206d65e4727db20ff2c1d8987234836b7bb4c039255`
- `d190_probe.h` `c85bd624d63dd0cd8a1d3185b6bf9c9cb94410ad2ce179b851494a115396f839`
- `README.md`    `13f6364d9a0bc348c24817f8e917cb0fec33c52651a1c227711cf6f6341467f1`

Cleanup lines (d190_probe.c:156-157,176-180) are under backend's
D192 repair — out of judging scope.

## F1 — BLOCKER: dlsym name has the nlist underscore

`d190_main.c:44` calls `dlsym(sl, "_SLSServerPort")`. Darwin `dlsym`
expects the **C** name — man dlsym(3): "to find the address of
function foo(), you would pass 'foo'... unlike the older dyld APIs
which required a leading underscore." The passed name searches for
`__SLSServerPort` → NULL → exit 3 on every run, including the exact
22F82 image. The symbol IS exported (`_SLSServerPort` T @0x1853c3fc8)
— the spelling is the bug. Fix: `dlsym(sl, "SLSServerPort")`.
The test suite cannot catch this: it exercises send_once/uuid pieces
directly, never main's resolve path (d190_probe_test.c:73 accepts
either gate rc by environment).

## F2 — UUID gate doesn't bind the resolved address

`d190_check_skylight_uuid` (:55-68) proves "exactly one loaded image
has LC_UUID == target" — count-not-first-match is correct for that
claim. But `slp` is resolved independently (dlopen+dlsym) and never
bound to the matched image. The gate's intent — *the called function
lives in the UUID-verified image* — is unproven: an interposed or
differently-versioned SkyLight could supply `slp` while the gate
counts the real image. Fix: `dladdr((void*)slp,&info)` →
`d190_image_lc_uuid(info.dli_fbase)` == target (plus the exactly-one
invariant if retained).

## F3 — LC_UUID walk unbounded

`d190_image_lc_uuid` (:39-53) walks `mh->ncmds` commands advancing by
`lc->cmdsize` with no sanity check: `cmdsize==0` → infinite loop;
oversized/corrupt `cmdsize` → reads past `sizeofcmds` → OOB on the
mapped image. Live dyld headers are well-formed, but the function is
also a contract scanner (tests feed synthetic buffers). Fix: require
`cmdsize >= sizeof(struct load_command)` and cap cumulative advance
at `mh->sizeofcmds`.

## Verified correct (frozen snapshot)

- Request verbatim (:70-79): bits `0x1513`, size `0x18`, voucher 0,
  id `0x74a7`, memset elsewhere.
- Reply layout (h:51-56): packed 44B, payload @+0x24; classify order
  (complex before id) differs from stub (id first) — reporting-order
  only, every failure converges on destroy; extra `size<24` TRUNC
  row is a safe addition.
- Records (:106-119): CALL seq0 `0x74a7`/aux option; RET seq1 msgid =
  **actual received id** iff kr==SUCCESS else 0, aux=kr verbatim —
  D150b/D191 faithful (records what arrived, not a constant).
- Stream (:121-151): v3 header exact — `n_records=2`, `emitted=2`,
  `reserved=0`, `uuid_valid=1`, gen=`(pid<<32)^uuid_lo64` matching
  `d141_set_generation`; loader self-consistent.
- One-shot: single `mach_msg` (:165-167), no loop/retry; wrapper
  `_SLSGetWindowServerTimebase` never referenced (G6).
- Gate fail-closed exactly-one, out zeroed on failure, no env bypass;
  G15 verbatim in main header (:8-13) and probe.h (:11-15); nonzero
  exit on every failure class; records filled before classify so the
  failure stream still carries both legs (G13).
- Committed d141/d169 headers included (probe.h:25-26) — D191.
- Reply `msgh_local_port` unchecked — matches stub (which also
  doesn't validate it).

## Answers to the task questions

- Can main resolve the intended symbol? **No** — F1.
- Does the UUID match belong to the called function? **Not proven**
  — F2 (needs dladdr binding).
- Cleanup: under backend repair (D192 classes) — not judged here.

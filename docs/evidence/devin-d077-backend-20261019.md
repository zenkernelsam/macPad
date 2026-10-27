# D077 — independent static verification of the D076 pinned runner

Offline only. Verdict: **verification NOT clean — two functional
defects** (both fail-closed, i.e., they would make the run produce
`STOP`/traceback rather than wrong data, but the packet as pinned
cannot succeed). No approval is implied; a corrected runner needs a
new hash and a new authorization cycle.

## What verified clean

- **Byte identity**: re-extracted fenced block from
  `devin-d076-backend-20261019.md`; `sha256(content + "\n")` =
  `96e47c3b530684d8af1243c19faa34bce351a4d417c868174a26ba705b0f5da5` —
  reproduces the manifest exactly.
- **Syntax**: block `compile()`s cleanly as Python.
- **Read order is fail-closed**: sym resolution → init-prims →
  physrw → sysinfo rc → dict type → base/slide consistency → magic →
  version literal → header double-read → bounds → per-entry checks;
  every failure path prints `STOP <name>` and exits before any
  subsequent read. No write calls exist anywhere in the script
  (kread64/kread32 are the only kernel-touching primitives used).
- **Sanitization**: no raw `base`, `slide`, `entries`, `conf`, `ops`,
  or name pointers are emitted; `entries_off` is kernbase-relative;
  output is booleans + counters + name strings only — matches the
  frontend §4 contract.
- **Bounds**: header double-read tear check; `numloaded≤max≤0x400`;
  `maxindex≤0x40` walk cap; name cap 0x80 with printable+NUL
  enforcement (the `for/else` correctly stops when no NUL precedes the
  cap); PAC presence via low-56 mask; kernel-range gate on every
  dereferenced pointer.
- **`_xpc_type_dictionary`** is a valid libxpc export (verified on the
  host image: resolves via `in_dll`).

## Discrepancy 1 — `CDLL("libxpc.dylib")` does not resolve

`dlopen` with a leaf name does not match libxpc's install name
(`/usr/lib/system/libxpc.dylib`) on dyld-cache systems — verified on
the host: `CDLL("libxpc.dylib")` raises `OSError … not in dyld cache`,
while `CDLL("/usr/lib/system/libxpc.dylib")` loads and resolves
`_xpc_type_dictionary`. On device the runner would die at that line
with a traceback (still no data emitted, but not a named STOP).
**Fix**: `ctypes.CDLL("/usr/lib/system/libxpc.dylib")`, or
`CDLL(None)` once libxpc is confirmed already mapped.

## Discrepancy 2 — unaligned `kread64` on 4-byte header fields

`k32 = lambda a: kread64(a) & 0xffffffff` is applied at offsets
`+0x04, +0x0c, +0x14` — addresses that are 4- but not 8-byte aligned.
The physrw read path requires natural alignment; an unaligned read
returns garbage or zero → `h1`/`h2` mismatch or `bad-header` → the run
stops cleanly but produces nothing. **Fix**: use the exported
`kread32` (present in the dylib export list) for the six u32 header
fields — this is exactly how the D069 C helper successfully read the
header (`numloaded=3 max=8 …` came back correct via `kread32`).

## Minor observations (non-blocking)

- `ctypes.CDLL` of `libjailbreak.dylib` itself is uncaught — a missing
  dylib raises a traceback rather than `STOP`. Cosmetic; no data can
  be emitted before the identity checks pass, so fail-closed behavior
  is preserved.
- `jbclient_initialize_primitives`/`jbclient_root_get_sysinfo`
  signatures are inferred from disassembly; the D069 device run
  empirically confirmed both callable this way (`init=0`, `sysinfo
  rc=0`, dict returned).

## Consequence

Because the manifest binds authorization to the exact bytes, **any fix
produces a different sha256** → the packet requires re-pinning and
re-authorization; this audit does not edit the approved-in-waiting
document. Suggested revision name: runner v2 fixing D1/D2; expected
hash will differ.

## Provenance

Hash/syntax/logic audit of `devin-d076-backend-20261019.md` fenced
block; symbol+dlopen checks against host `/usr/lib/system/libxpc.dylib`;
alignment constraint from the libjailbreak physrw path plus the D069
empirical header read (kread32 path). No device contact.

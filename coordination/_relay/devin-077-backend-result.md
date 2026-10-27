# devin-077 — backend result

Independent verification of the D076 runner. Offline only — no device,
credentials, syscalls, deploy, build, trustcache, source, or Git.

**Verdict: NOT clean — two functional defects, both fail-closed.**

Verified clean: byte identity reproduces the manifest sha256 exactly;
Python syntax valid; read order fail-closed (every failure emits
`STOP <name>` before further reads); emitted fields fully sanitized
(slide-relative + booleans only, no raw KASLR); all bounds and the
low-56 PAC presence check present; no write primitives used.

Discrepancies (would abort the run before any list data):

1. `CDLL("libxpc.dylib")` — leaf name doesn't resolve on dyld-cache
   systems (confirmed on host: OSError). Fix: absolute path
   `/usr/lib/system/libxpc.dylib` (verified resolving) or `CDLL(None)`.
2. `kread64` at 4-byte-aligned header offsets (`+4/+0xc/+0x14`) —
   unaligned for the physrw path; must use `kread32` for the six u32
   fields (the approach that empirically worked on-device in D069).

Because authorization binds to exact bytes, either fix changes the
hash → re-pin + re-authorize as runner v2. Packet remains NOT APPROVED.

Evidence: `docs/evidence/devin-d077-backend-20261019.md`

(handover to codex now)

# D106 — exact libquarantine getter wrapper and errno propagation

Offline exact-binary work only. D105 establishes historical WS PostEvent
msgids 48891.17/.19 -> P0 getter #103 -> empty reply -> 1002. It does NOT
establish Quarantine no-match: keep foreign precheck/matched handler sources
open and do not assume current WS10447 first-miss behavior is captured.

Inspect the already saved exact Ventura libquarantine sparse extraction
(R013/R014 artifact inventory). Locate the responsibility getter used by
tccd, validate image identity/hash, and preserve raw disassembly for the
entire request construction and return/error path. Establish literal policy
name, selector (0xb6 if confirmed), size/layout/input/output-token fields,
wrapper errno synthesis versus propagation, and limits from missing GOT
provider/cache fixups. Do not guess imported stub identity or provider code.

Also verify D105 chronology/provenance: source log file is an excerpt, not
an invented full raw transaction; quote it accurately and label omission.
No device/KRW/TCC probes/setters/token changes/source/build/Git operations.
Save D106 evidence/result and explicit (handover to codex now).

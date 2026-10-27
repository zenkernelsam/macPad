# D108 — complete offline IMG4-to-kernel payload provenance

Continue D107 on frozen inputs. Produce the missing local byte-linkage:
extract the IM4P compressed payload from pinned IMG4, identify its exact
compression format from magic/headers, and use an available validated local
decoder (or isolated task-local decoder dependency) to decompress once into
analysis/kc-d108/. Verify size/hash against kc_raw_16.3_T8112.bin; preserve
exact commands, raw identities and mismatch/no-decoder failure. No device,
network download of private firmware, kernel execution or mutation. Do not
guess decompressor algorithm from a tool name or merely build-string match.
No production edits or Git. If no usable decoder exists, stop with concrete
library/API missing-input evidence rather than a new prose-only summary.

Also correct D107 scope: zero literal Quarantine in one image does not prove
no computed/externally supplied name registrant. No currently observed kext
directory cannot alone prove absence across the boot epoch. Snapshot yields
instant policy state; epoch history requires additional loader/unload facts.
Check these limits, do not infer current registered state.
Save D108 evidence/results and explicit (handover to codex now).

# Frontend R014 — prepare minimal read-only 22F82 responsibility evidence capture

Read R013 and inspect only existing repository tooling/source for dyld cache
mapping, subcache metadata, chained fixups, PAC pointers and read-only LLDB
capture. Produce a precise, bounded collection plan for the next backend-owned
device window to resolve the `Quarantine` selector `0xb6` stub at
`0x18b321bc0` / GOT `0x1dfef8f88`.

Check whether `misc/dsc_extract_image.py`, `misc/dsc_cache_subset.py`,
`misc/macpad_ssh.py`, existing LLDB helpers and the available dyld headers are
suitable; distinguish Ventura 22F82-compatible format facts from the local
dyld 1286 source version. The plan must identify the minimum exact identity,
mapping, subcache, page, fixup and target-image data to preserve; include true
exit/status checks and an explicit stop condition if a cache copy is too large
or the target format cannot be validated. Prefer metadata + a bounded page /
fixup read over copying an entire shared cache. Compare the cache-side route
with a runtime read of the slid arm64e slot, but do not assume the candidate
target image until evidence identifies it.

State clearly that resolving the stub name alone does not assign errno 103;
list the additional evidence required to distinguish mac_proc precheck,
no-match default and matched policy handler. Do not invent LLDB breakpoints,
parse offsets as Ventura facts without exact header support, write a helper,
SSH/device access, modify source/tests/build inputs, install dependencies or
perform Git writes. Write only your dated evidence note and
`coordination/_relay/round-014-frontend-result.md`; final line exactly
`(handover to codex now)`.

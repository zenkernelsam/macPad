# D197 — final frozen D195 main/scanner review

Frozen hashes (sha256):
- `d190_main.c`      `3b1175ab53a553f41e049b80894eda9b6dcc402c0f3ebc73779c45fc0260ef1f`
- `d190_probe.c`     `5afd7971a31963f60986042cf1a0122ac8bbe0a1921ed9f0ed457d06a60b6017`
- `d190_probe.h`     `3929cbf9023e72a82a4c9eb79e00ecf54b7cb56c2c3cc6add7e95946d3299562`
- `d190_probe_test.c` `1c9a451678411b1b16518a101e7d37a5b18321d9a5b02552a60b41c823f40fef`
- `README.md`        `7826f3cfe9bf249a74fdcde9e9685a12e9507fe6a06efe5600cb179c5abfcbb8`
- `run_tests.sh`     `659dda99cedfc6e7e67d34829f4d8973be609e87e27323184f7ecbe1e39a4e17`

Verdict: **ACCEPT** — F1/F2/F3 all resolved; D193 cleanup preserved.

## F1 — C-name resolved correctly

`dlsym(sl, "SLSServerPort")` (main:46) — nlist prefix dropped.
p8 (:372-375) proves the mechanism both ways on a harmless fixture:
C-name resolves, `"_d190_fixture_serverport"` returns NULL — the
exact bug shape reproduced deterministically without SkyLight.

## F2 — address bound to owning image

`d190_addr_image_match` (probe.c:87-91): `dladdr` → `dli_fbase` →
`d190_image_lc_uuid` → memcmp vs the gated uuid. Called at main:66 —
**after** resolve (:46) and the loaded-image gate (:56), **before**
the first `slp()` invocation (:71). Ordering requirement satisfied:
`_SLSServerPort` is never invoked until both UUID checks pass —
the dladdr query is resolution-only, not invocation. p8 covers
positive (self image), negative (target uuid), and foreign-image
(fixture resolves to a *different* LC_UUID) cases.

## F3 — bounded LC scanner

`d190_image_lc_uuid_bounded` (probe.c:40-68): region =
min(declared `sizeofcmds`, `hdr_bytes`); per-command sanity —
`off+8 ≤ limit` before reading the command header, `cmdsize ≥
sizeof(load_command)` (kills the zero-cmdsize hang), `cmdsize ≤
limit−off` (kills the OOB leap); duplicate LC_UUID → −1; uuid
payload `cmdsize < sizeof(uuid_command)` → −1. Live-dyld wrapper
(:70-76) passes `sizeof(mh)+sizeofcmds` — honest-header assumption
for mapped images, stated in comment; the explicit-buffer variant is
the contract scanner. p9 (:392-443) covers every vector: valid,
cmdsize=0, short hdr_bytes, truncated uuid payload, ncmds overrun,
cmdsize 0x1000 leap, duplicate LC_UUID, sub-header buffer.

## Cleanup preserved

`d190_cleanup_class` (:191-198) and the four-arm switch with
in-arm counters (:236-257) unchanged in semantics from the
D196-accepted snapshot — verified by reading, no reruns needed.
Records/wire/stream sections untouched.

## Minor (non-blocking)

`self_uuid` identifies the test binary by name match "d190_test" —
self-checking (failure breaks :377); acceptable for a test helper.

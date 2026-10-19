# D195 — D190 symbol resolution, image binding, bounded scanner repair

Task: `coordination/_relay/devin-195-backend-task.md`. Fixes D194's
F1–F3 in `analysis/d190-timebase-probe/` only; host tests only; no
main/sender invocation, device, product contact, or commit.

## Fixes

- **F1 C-name**: `dlsym(sl, "SLSServerPort")` — Darwin dlsym prepends
  the nlist underscore itself; `"_SLSServerPort"` searched for
  `__SLSServerPort` and always returned NULL (exit 3 on every run).
- **F2 address bound**: `d190_addr_image_match(slp, uuid)` — `dladdr`
  on the resolved pointer → `dli_fbase` → owning image LC_UUID must
  equal the pinned 22F82 UUID. The exactly-one loaded-image gate is
  retained; an interposed/foreign `SLSServerPort` can no longer
  satisfy the check by coexistence.
- **F3 bounded scanner**: `d190_image_lc_uuid_bounded(mh, hdr_bytes,
  out)` — bound = min(`sizeofcmds`, `hdr_bytes − sizeof(header)`);
  requires `cmdsize ≥ sizeof(load_command)`, no advance past the
  bound, `LC_UUID cmdsize ≥ sizeof(uuid_command)`, exactly one
  LC_UUID (duplicates reject), every declared `ncmds` inside the
  region, `hdr_bytes ≥ sizeof(*mh)`. Live `d190_image_lc_uuid` wraps
  it with the dyld-mapped `sizeofcmds` bound.

## New host tests

- **p8** — F1 mechanism on harmless `d190_fixture.dylib`:
  `dlsym(h,"d190_fixture_serverport")` resolves,
  `dlsym(h,"_d190_fixture_serverport")` does not — the exact bug
  shape, no SkyLight. F2: own-function address matches the test
  binary's LC_UUID; fixture symbol owns a *different* image; both
  mismatch the pinned UUID.
- **p9** — F3 synthetic buffers: valid, `cmdsize=0` (reject, no hang),
  buffer smaller than declared `sizeofcmds` (bounded reject, no OOB),
  `LC_UUID cmdsize < uuid_command`, `ncmds` past region, oversized
  `cmdsize`, duplicate `LC_UUID`, `hdr_bytes < header`.
- D193 cleanup tests fully preserved (p6 hook counters, p7 table);
  one-send structure and verbatim kr records unchanged.

## Exact results

`bash analysis/d190-timebase-probe/run_tests.sh`:

- ASan+UBSan: `PASS: 97 checks, 0 failures` (9 scenarios)
- TSan: `PASS: 97 checks, 0 failures`
- arm64: objects `Mach-O 64-bit object arm64`; binary
  `Mach-O 64-bit executable arm64` — never executed

## Source hashes (sha256)

- `d190_main.c` `3b1175ab53a553f41e049b80894eda9b6dcc402c0f3ebc73779c45fc0260ef1f`
- `d190_probe.c` `5afd7971a31963f60986042cf1a0122ac8bbe0a1921ed9f0ed457d06a60b6017`
- `d190_probe.h` `3929cbf9023e72a82a4c9eb79e00ecf54b7cb56c2c3cc6add7e95946d3299562`
- `d190_probe_test.c` `1c9a451678411b1b16518a101e7d37a5b18321d9a5b02552a60b41c823f40fef`
- `d190_fixture.c` `8865aa5ab39187c5f430c47cd2b262fcb9d5c8fbc0fa52c7ad31f3df918f84d9`
- `run_tests.sh` `659dda99cedfc6e7e67d34829f4d8973be609e87e27323184f7ecbe1e39a4e17`
- `README.md` `7826f3cfe9bf249a74fdcde9e9685a12e9507fe6a06efe5600cb179c5abfcbb8`

Nothing staged; unrelated WIP preserved.

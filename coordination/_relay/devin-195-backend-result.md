# devin-195-backend-result

Status: **PENDING reviewer acceptance** — D194 F1–F3 repaired in the
uncommitted D190 candidate; nothing staged, no commit.

## Fixes

- **F1**: `dlsym(sl, "SLSServerPort")` — C name (the underscored
  spelling searched `__SLSServerPort`, always NULL).
- **F2**: `d190_addr_image_match(slp, uuid)` — `dladdr` binds the
  resolved address to its owning image's LC_UUID; equals the pinned
  22F82 UUID required. Exactly-one gate retained.
- **F3**: `d190_image_lc_uuid_bounded(mh, hdr_bytes, out)` — bound
  `min(sizeofcmds, buffer)`: `cmdsize ≥ sizeof(load_command)`, no
  bound-past advance, `LC_UUID cmdsize ≥ sizeof(uuid_command)`,
  exactly one LC_UUID, all `ncmds` inside the region. Live wrapper
  uses the dyld `sizeofcmds` bound.

## Tests

p8: F1 mechanism via harmless fixture dylib (C-name resolves,
`_x` doesn't); F2 own-image match + fixture-symbol different-image
mismatch. p9: all F3 malformed classes incl. `cmdsize=0` no-hang,
buffer<declared, truncated/duplicate `LC_UUID`. D193 cleanup tests
preserved (p6 counters, p7); one-send + verbatim kr intact.

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

No main/sender invocation, no SkyLight/WS/device/product contact,
no install, no commit. Evidence
`docs/evidence/devin-d195-backend-20261020.md`.

(handover to codex now)

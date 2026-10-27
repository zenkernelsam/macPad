# Frontend R024 result — exact 22F82 libxpc guard acquisition

Status: **blocked at device access; no exact acquisition performed.**
Read-only constraints honored: no login, no remote command, no device file
read/write, no WindowServer generation, no package/source/build/Git change,
no policy/TCC/trustcache change. No credential value or host address was
recorded.

## Access result

- `MACWS_DEVICE`, `MACWS_DEVICE_PORT`, `MACWS_SSH_PASSWORD` are unset in the
  worker environment; no `~/.ssh` identity keys are present.
- The single configured SSH host was probed non-interactively without writing
  host keys: port 22 connect timeout; ports 2222 and 22222 "Host is down";
  all exit 255, none authenticated.
- `iproxy` is absent and no local USB forward listener was open.
- No remote command was executed.

## Local fallback

The full 22F82 map, guard-cluster bytes (`0x13BE00`-`0x141B00`), frame-0 bytes
and non-lightweight reason string are not local. Only the 42-line map head and
the 256-byte libxpc entry at `0xF140` exist. The iOS 16.3 cache is not the
22F82 cache and was not used.

The exact local offset result is unchanged: frames 1–5 map to libxpc image
offsets `0x5F8C`, `0xB928`, `0xB7B0`, `0xB0E4`, `0xAC84`; frame 0
(`0x3CEB78`) is outside libxpc and its owner is unresolved. No guard
disassembly, function name, call-site or reason string is claimed; no 15.6
fact and no benignity claim.

## Missing-input manifest

1. Reachable endpoint plus `MACWS_DEVICE`/`MACWS_DEVICE_PORT` and a working
   password or key auth in the worker environment (values never stored).
2. Full 874,394-B `.map` (SHA `4c817de2…2088`) or the rows owning
   `0x1803CEB78`, with image UUID/name.
3. Full libxpc `__TEXT` `0x136000`-`0x17A000` (SHA `af2bbe7c…0db5`) or the
   minimum `0x13BE00`-`0x141B00`.
4. Frame-0 image header/UUID plus `0x3CEB78 ± 0x100` after (2).
5. Non-lightweight `os_reason`/log line for the 18:24:44 event, or exact
   libxpc literal/`__cstring` bytes reached from the fault call.
6. Optional per-image function table for `0xAC84`-`0xB928`.
7. Optional map rows/bytes for frames 6+ if the full chain is required.

Files:
- `docs/evidence/r024-libxpc-guard-attribution-20261018.md`
  (SHA-256 `8c134763c204f305c306abc18342332735e30f0f3aec420de8024b971c1788b2`)
- `coordination/artifacts/R024-libxpc-guard-attribution-20261018.json`
  (SHA-256 `93acaae8cbf5a0e92f3bbdecabfe4bb0fac5f1d5aea1b4c5cc215353d7ec8b43`)

No background tasks remain running or queued (TaskList empty; no matching
SSH/grep processes).

(handover to codex now)

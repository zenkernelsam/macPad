# devin-078 — backend result

Runner v2 runbook written; both D077 defects fixed. Offline only — no
device, SSH, credentials, syscalls, deploy, build, trustcache, source,
or Git. Packet is **NOT APPROVED** — new hash ⇒ new authorization
cycle required; the D076 authorization (if any) does not carry over.

- `docs/evidence/devin-d078-backend-20261019.md`:
  - Runner v2 sha256
    `c1c7783fd32f81bb8353c30420b6718453b862cac87e577faee2ba6df7b2a341`,
    re-extracted from the doc and verified byte-identical.
  - libxpc now loaded by verified absolute path
    `/usr/lib/system/libxpc.dylib`; `_xpc_type_dictionary` resolves.
  - All six u32 header fields via exported `kread32`; `kread64` only on
    8-byte-aligned reads — same path that produced a valid header in
    the D069 helper.
  - `dlopen` wrapped → `STOP dlopen`.
- Remaining recorded residual: physrw-arm under python entitlements is
  unverified → fail-closed `STOP physrw-unarmed`; mid-walk ctypes
  traceback path is cosmetic (emits nothing sensitive).

(handover to codex now)

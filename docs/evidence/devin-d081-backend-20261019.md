# D081 — final packet review of D080 runner v3

Offline review only. No execution, no device, no approval granted or
implied by this memo.

## Verdict

**Ready to present for user authorization.** The packet is complete
against every contract element from D074–D080; the only open item is a
runtime question that can only be answered by an authorized run, and
it has a defined fail-closed terminal.

## Element-by-element

| Contract element | Status | Evidence |
|---|---|---|
| Hash binding | ✓ | sha256 `8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`, convention "fenced content + trailing newline" declared; D080 re-extraction byte-identical |
| Authorization scope | ✓ | NOT APPROVED banner; v3 hash distinct from v1/v2 ⇒ third authorization cycle; must be authorized by name |
| Sanitized output | ✓ | emits only STOP names, `*-ok` flags, header counts, `entries_off` (slide-relative), indices, policy names, presence flags. No raw VAs, audit tokens, task ports, or unrelated memory |
| Fail-closed gates | ✓ | 16 distinct STOP terminals: dlopen, sym-*, init-prims, physrw-unarmed, sysinfo, sysinfo-type, base-slide, magic, version, torn-header, bad-header, entry-i, nameptr-i, name-i, name-nul-i, ops-i. Exit 9 on all |
| Bounds | ✓ | `mx ≤ 0x400`, index cap `0x40`, name cap `0x80`, KV range check on every dereferenced pointer, double-read header, printable+NUL names |
| Identity before reads | ✓ | `staticBase`, `base = SB+slide`, magic `0xfeedfacf`, version string all verified before the first policy read |
| Read-only | ✓ | only `kread32`/`kread64`; no setters, no writes, no syscalls, no handler invocation (ops slots read as pointers, never called) |
| kread32/kread64 split | ✓ | `kread32` for the six u32 header fields (`+0..+0x14`), `kread64` only at 8-aligned `+0x18`, entries, conf, name, ops |
| DICT_T fix | ✓ | `addressof(c_void_p.in_dll(...))` — host-verified equal to `xpc_get_type(d)` (D079/D080) |

## Review notes (non-blocking, recorded for the operator)

1. **`jbinfo_initialize_boot_constants` is correctly absent** — the
   sysinfo dict's `kernelConstant.*` values are daemon-sourced
   (jailbreakd RPC), not local globals; `ibc` only armed the local
   globals `physrw_kvtouaddr` needed, which v3 does not use. D069
   obtained `kernelConstant.base` without it.
2. **Zero-return ambiguity fails safe** — `kread64` returning 0 is
   indistinguishable between "mapped zero" and "read failed". For
   entry/name/ops pointers NULL is legitimate (holes/nullable), so a
   silent read failure would report "absent" rather than fabricate
   presence — the conservative direction for this discriminator.
3. **Minor hardening not applied (hash-frozen)**: `nl <= midx+1`
   cross-check and name-read batching (≤0x80 one-byte `kread64` calls
   per name — bounded, merely slow). Any change requires v4 + new
   authorization; current form is correct, just not maximally strict.
4. **Exception paths** (`in_dll`, attribute typos on `xpc.*`) raise
   Python tracebacks rather than STOP names — emits nothing sensitive;
   cosmetic.

## Runtime unknowns (unresolvable offline, all fail-closed)

- **Physrw arm under procursus python entitlements** — the single open
  link. If `jbclient_root_get_physrw` ≠ 0, `STOP physrw-unarmed`; the
  runbook then forbids the helper/trustcache fallback →
  `STOP missing-authorization`. No offline evidence can settle this.
- Whether a `Quarantine` policy is registered at snapshot instant —
  that is the question the run exists to answer.

## Presentation recommendation

Present the v3 packet for authorization as: "one bounded read-only
`snapshot` of `mac_policy_list` via existing Dopamine primitives,
pinned at sha256 `8bb90399…`, emitting only sanitized fields, with
witness/cleanup contract per D076–D080." Authorization must name v3 /
this exact hash.

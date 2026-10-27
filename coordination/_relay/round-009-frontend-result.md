# Round 009 — frontend result: exact libquarantine boundary

Date: 2026-10-18. Read-only offline analysis; no device/SSH, source/test/build
change, build or Git write. Added only this result and the dated evidence.

Candidate HEAD: `7e5d771`. Backend R005's generation was untouched.

## Exact input verified

`analysis/r005-responsibility-inputs/libquarantine.dylib`, UUID
`A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3`, SHA
`f5989c8f4afc9985e95795bf683679eed629af8b5ae288a0fb87317ba0e844f2`,
LC_BUILD_VERSION macOS 13.4, `__TEXT` vmaddr `0x18b31f000` fileoff 0 size
0x3000. Getter `0x18b320b00`; helper `0x18b320b2c`; both disassemble cleanly
from the full image. The 32KB slice is malformed for `llvm-objdump` (section
offset past end); it is not the code source.

## Getter/helper (FACT, exact 22F82)

- Getter copies the 32-byte input token and calls helper at +0x2c.
- Helper reads token pid (`+0x14`) and pidversion (`+0x1c`), builds a
  syscall arg struct at `sp+0x10` (`pid`, `pidversion`, caller-owned output
  token pointer from x1, pointer to a zero header, x3=0), then calls the
  shared `__auth_stubs` entry `0x18b321bc0` with:
  - x0 = string `Quarantine` at `0x18b321e0b` (file 0x2e0b)
  - w1 = **0xb6 (182)**
  - x2 = struct; x3 remains 0.
- Return is the stub's `w0`; the getter returns it unchanged. The helper
  neither allocates nor retains the output token; on success the kernel
  writes via x1. The local x19 copy path (8 bytes) is only for a nonzero
  caller x2 and is not taken by tccd.
- Other Quarantine selectors in the same exact image: 0xb4, 0xb5, 0xb7,
  0xb8, 0xb9, 0xba, 0xbb, plus 0x53/0x58/0x59. All use the same stub and
  return w0/errno; no local 103 assignment exists (`#0x67` occurrences are
  `cmp`, not `mov`; local branches use 0x16/0x22/0xc error values, not 103).

## 103 attribution correction

- The libquarantine getter/helper does **not** produce 103 locally; it passes
  through the stub’s w0/errno.
- The stub provider is **unresolved for 22F82**: the `__auth_stubs` GOT slot
  `0x1dfef8f88` is not covered by any extracted segment; `LC_SYMTAB`/
  `LC_DYSYMTAB` are all zero, no `LC_DYLD_INFO`/`LC_DYLD_CHAINED_FIXUPS`,
  and `otool -Iv` reports the indirect table truncated. The 15.6 exact image
  resolves the same helper shape to `___sandbox_ms` (which aliases
  `___mac_syscall` in 15.6 libsystem_kernel), but that is version-adjacent
  THEORY only.
- Adjacent XNU `__mac_syscall` (security/mac_base.c:1567-1600) can return
  103 from (a) `mac_proc_check_mac_syscall` precheck, (b) the no-match
  default, or (c) a matched `mpo_policy_syscall` handler. Therefore **103
  does not uniquely prove “no handler”**; the R008 claim is withdrawn. Exact
  device-kernel execution is not proven by the adjacent source.
- Missing exact range: GOT slot `0x1dfef8f88` (8 bytes) plus its
  bind/chained-fixup record and covering cache segment/mapping. No guessed
  bytes; the existing alternative is the exact full image for `__TEXT`, and
  15.6 only for the stub name (labeled).

## Native vs Ventura metadata

Backend R005: Ventura tccd imports the getter; native iOS tccd
(`67669ed7…`, UUID `0120CE08…`, CDHash `1ba18ff5…`) has **no responsibility
imports**. R004’s “native import contract” was actually the Ventura tccd;
the native helper’s dlsym/flat-import failure is a helper-availability fact,
not proof of native daemon gating. Ventura tccd’s `from libSystem` is the
link-time umbrella; `dladdr` identifies the real provider as libquarantine
UUID `A2D17FF6…`.

## One evidence-supported next control

Owned-generation, read-only observation at the actual `__mac_syscall` /
`Quarantine` policy boundary for one probe audit-token getter: record caller
PID, policy `Quarantine`, selector `0xb6`, `mac_proc_check_mac_syscall`
return, whether an `mpc_name` matched and its `mpo_policy_syscall` return,
and the caller errno/output-token write. This distinguishes precheck vs
no-match vs matched-handler 103. If a debugger is unavailable, fall back to
an ABI-preserving read-only helper calling the exact-image selectors 0xb4,
0xb6, 0xb7 and comparing raw return/errno/output. No setter, kernel write,
fake token/session or TCC bypass.

Remaining unknowns: exact stub provider; device policy list/handler; actual
103 branch; success-path output ownership/partial-write; whether any
selector has a genuine alternative.

## Commands / exits

`shasum -a 256`, `otool -l`, `llvm-objdump -d` (full image), Python string/
segment parsing, `grep`, XNU `sed`, and `git log` exited 0. The 32KB slice
`llvm-objdump` exited 1 (section offset past end). No test/build/package/
device command; no commit/reset/stash/push.

Full detail: `docs/evidence/r009-exact-libquarantine-boundary-20261018.md`.

(handover to codex now)

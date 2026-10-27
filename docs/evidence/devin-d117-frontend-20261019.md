# D117 — offline responsibility/TCC upstream fault-closure map (frontend)

Task: `coordination/_relay/devin-117-frontend-task.md`. Read-only audit using
only in-tree artifacts. No device/SSH/KRW/setters/kernel snapshot/build/Git.
Did not touch the backend's moving D116 transport files.

## 1. The pipeline — which edges are now FACT vs open

```
tccd 9845D17F 0x10004fb48 bl getter          [FACT: callsite+ret only, R006]
  → libquarantine A2D17FF6 getter 0x18b320b00  [FACT: exact image]
    → helper 0x18b320b2c: arg-struct {pid=val[5], pidversion=val[7],
       out-token*, flag*, x3} at sp+0x10; x0="Quarantine" @0x18b321e0b
       (fileoff 0x2e0b, bytes re-verified "Quarantine\0");
       movz w1,#0xb6 @0x18b320b94 (0x528016c1, re-verified =182);
       bl 0x18b321bc0 (0x9400040a, re-verified)   [FACT: exact image]
    → auth-stub 0x18b321bc0 → slot VA 0x1dfef8f88   [FACT: recomputed]
    → PROVIDER ???                                 [OPEN — the narrowest edge]
    → kernel ENOPOLICY source: (a)/(b)/(c)         [OPEN, conditional]
  → getter/helper return w0 verbatim; errno=103 NOT synthesized in-image
    (own sweep: zero movz w*,#0x67 in entire __TEXT 0x3000)  [FACT]
  → tccd error arm → pre-created empty reply dict (count=0) → sls_rc=1002
    [FACT: msgID-correlated, D105; historical WS 48891→tccd 48750 only]
```

## 2. New fact this audit adds (not in D106)

D106 quoted slot `0x1dfef8f88` "outside every sparse segment". Decoding all
30 `__auth_stubs` entries (`__auth_stubs` va `0x18b321ba0` size `0x1e0`):

- Every stub's slot lands in VA range **`0x1dfef8???`–`0x1dfef9b10`** — a
  permuted shared-cache auth-pointer region, **not** the image's nominal
  `__auth_got` section at `0x1dd78c578` (whose page is a zero hole; the
  section's span `0x5046d578+0xf0` also extends past the extraction's
  `0x5046d000` EOF — doubly absent).
- Consequence: the missing bytes are the **shared cache's bind-applied auth
  table**, not an image-local GOT. Re-extracting "libquarantine's auth_got"
  cannot close the edge; the needed artifact is the **8 bytes at cache VA
  `0x1dfef8f88`** (or the fixup/bind record for that auth-stub entry) from
  the source cache `/var/mnt/rootfs/System/Cryptexes/OS/System/Library/dyld/
  dyld_shared_cache_arm64e` (+subcaches) recorded in
  `analysis/r005-responsibility-inputs/metadata.json`.

## 3. Theory ledger

| Theory | Status | Falsifier / confirmation |
|---|---|---|
| Stub provider = libsystem_kernel `___sandbox_ms`/`__mac_syscall` | THEORY (15.6-adjacent only: 15.6 shows `___sandbox_ms`≡`___mac_syscall` @`0x1804335e8`) | Slot low bits pointing outside 22F82 kernel `__TEXT` `0x1803bf000+0x39fe8` falsifies; inside → disasm that image (device full copy exists per metadata `device_full`; local slice is 32 KB, symtab zeroed, early text only) |
| Provider = another image (libsystem_sandbox/dedup veneer) | THEORY, indistinguishable until slot bytes exist | same artifact |
| Kernel 103 from (b) no "Quarantine" policy registered | THEORY | frozen `macws-policy-snapshot-v3` packet (hash `8bb90399…`, not executed) reads `mac_policy_list` — discriminates (b) only |
| 103 from (a) `mac_proc_check_mac_syscall` hook return | THEORY | hook return is policy-defined; needs kernel-side witness the snapshot cannot give |
| 103 from (c) matched "Quarantine" `mpo_policy_syscall` returning ENOPOLICY for selector `0xb6` | THEORY | needs policy-list presence AND handler behavior; kernel witness |
| "launchd hosting alone fixes it" | **REJECTED** (R006: launchd-hosted probe 49211 + ISA 48579 both #103) | — |
| audit-token interpose rewrites the input token | **REJECTED** (R005 §3.2: by-value params; `sender_auid=501` is accessor return, not token mutation) | — |
| Historical #103 = current WS-10447 transaction | **REJECTED as identity** — preserved: no tccd window for gen-10447; its posts are cached denies | — |

## 4. Preserved distinctions (per task)

- precheck vs no-match vs matched-handler: all three remain live producers
  of the observed 103; the provider-level `rc=-1 errno=103` R004 controls
  (chroot + live-native tokens) fire before any relationship query.
- `#103` empty-dict reply ≠ TCC policy denial; `result` key presence never
  proven (R007/D105).
- GOT/provider resolution OPEN — now bounded to exactly 8 bytes at
  `0x1dfef8f88` + a fixup record, with the extraction method corrected
  (image-local auth_got re-extraction would still miss it).

## 5. Minimal next step

The single missing artifact: **the 8-byte slot value at cache VA
`0x1dfef8f88`** from the Ventura cache (+ its fixup record if obtainable).
Low bits then name the provider image/VA; disasm of that provider (device
copies already staged per metadata `device_full` paths) settles the
`__mac_syscall` theory or reroutes the edge one hop.

## 6. Commands / exits

| Command | Exit | Result |
|---|---|---:|
| LC_SEGMENT parse of `analysis/r005-responsibility-inputs/libquarantine.dylib` | 0 | 4 segments only; slot outside all |
| `metadata.json` read | 0 | dep slices + UUIDs incl. kernel `34A49B54` |
| slice symtab/dysymtab dump (libquarantine + libsystem_kernel) | 0 | all zeroed — no bind resolution |
| own decode of all 30 `__auth_stubs` | 0 | slots in shared `0x1dfef8xxx–0x1dfef9xxx` |
| re-verify `0x528016c1`=movz w1,#182; `Quarantine\0`@0x2e0b; `bl 0x9400040a`→`0x18b321bc0` | 0 | all confirmed |
| full-`__TEXT` movz #0x67 sweep | 0 | zero hits |
| xnu `mac_base.c` :1535–1600 re-read | 0 | precheck is hook-return, not literal ENOPOLICY |

No source edits, no builds, no device access, no Git.

(handover to codex now)

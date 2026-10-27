# D043 — independent sender-observation ABI/polarity audit + candidate review

Date: 2026-10-19 · Devin frontend 9589 · read-only/local-only.
Task: `coordination/_relay/devin-043-frontend-task.md`. No device/build/
source/Git changes. FACT = disasm of `analysis/SkyLight_22F82` (LC_UUID
`96676A53-B1E0-3D7E-B98B-B73873CD1880`, __TEXT va `0x1850e5000`) and
`analysis/TCC_22F82`, or verified source read. THEORY marked.

## 1. Exact ABI of the three boundaries (disasm-verified)

### 1a. Gate `CGXSenderCanSynthesizeEvents` @+0x318464 (va 0x1853fd464)

```
pacibsp; sub sp,#0x70; stp x29,x30,[sp,#0x60]; add x29,sp,#0x60
   → words d503237f d101c3ff a9067bfd 910183fd   (pin VERIFIED)
canary load
ldr x8,[0x1d8ba61b8]            ; gCurMsgAudit container ptr (WS __DATA)
cbz x8        → mov w0,#0 → ret          ARM A: container NULL → deny
ldr w9,[x8,#4]; cmp #0x34; b.lo → w0,#0  ARM B: size<52 → deny
ldp q0,q1,[x8,#0x14] → sp+0x30           ; copy 32B sender audit token
ldp q0,q1,[0x1d3dcb6c8]→sp; ldr [..+0x20]→sp+0x20  ; copy 40B rightDesc
paciza x16(=+0x340da0); x2=x16           ; IA-sign impl pointer
x0=sp+0x30 (token copy); x1=sp (desc copy)
bl  +0x3407ec  → w0 propagated through epilogue → retab
```

**Signature:** effectively `int f(void)` — x0/x1 unused on entry (never
read before being overwritten). Wrapper `(void*,void*)` forwarding is
harmless. **Return:** w0: 0=deny, 1=allow → `cbz` at
_XPostEventRecord+0x388f0c selects 1002 vs dispatch.
**Container ABI:** ptr+4 = u32 size (≥0x34), +0x14..+0x33 = 32B
audit_token_t {auid,euid,egid,ruid,rgid,pid,asid,pidver}. The head
+0..+0x13 (20B incl. the size word) is never read by the gate itself.

### 1b. Dispatcher `WSAuditTokenHoldsCachedProcessRight` @+0x3407ec

```
pacibsp; sub sp,#0xb0; stp x26,x25,[sp,#0x60]; stp x24,x23,[sp,#0x70]
   → d503237f d102c3ff a90667fa a9075ff8   (pin VERIFIED)
x23=x0 token-copy; x22=x1 desc-copy; x21=x2 signed-impl
w20 = audit_token_to_pid(token)          (stub +0x39751c)
x24 = audit_token_to_pidversion(token)   (stub +0x39752c)
if (w20==0) return [x22+0x20] != 0                       ARM D0: pid==0 → flag byte
if (w20==getpid()) return 1                              ARM D1: self fast-path
key = w20 | (x24<<32)          bfi verified — pid|pidver<<32
dispatch_once(getProcessCredentialCache + notify_register_mach_port
              "com.apple.tcc.access.changed")            (Credentials.c)
cached = cacheDict ? CFDictionaryGetValue(dict,key) : 0
if (cached & [x22+0x18])          decided-mask hit
    return (cached & [x22+0x10]) != 0                    ARM D2: cached verdict
impl_rc = blraaz x21  (x0=&token-copy2, x1=[x22]=rightDesc[0])
newbits = impl_rc ? [x22+0x10] : [x22+8]                 csel verified
if (cacheDict==0) return impl_rc                          no-cache raw return
merged = newbits | cached
if (CFDictionaryContainsKey) CFDictionarySetValue(key, merged)
else { build block {pacda ctx, pacia fn +0x340a60, pid, key}
       +0x185425504(0x5000000000, block)                 sync invoke under
       pthread_mutex → invoke runs +0x340a60:
       task_name_for_pid(gport, pid, &out)               resolve sender task port
       if (out.ok) { CFDictionarySetValue(key,merged);
                     +0x185422fec(out, paciza(+0x340a84), key, 0) }
       __Block_object_dispose }
return impl_rc
```

**Signature:** `int f(void *token32, void *desc40, void *signed_impl)`.
Args are **caller-stack copies**, valid for call duration only.
**Return:** w0 ∈ {flag-byte, 1, (cached&mask)!=0, impl_rc} — always
synchronous; the block-invoke for the insert path does NOT delay the
verdict (w21=impl_rc fixed at +0x340918 before submission). My earlier
"async TCC request may be pending" reading is **withdrawn**: the PAC-
signed block is invoked synchronously by +0x185425504 under a mutex to
resolve the sender's task port; `+0x185422fec` then registers the
keyed `+0x185425a84` trampoline — semantics consistent with a
**process-death invalidation watch** (THEORY: keyed pid|pidver,
self-re-signing trampoline into +0x185422e2c registry; not an async
TCC request). The cache's other invalidator is FACT: notify handler
+0x185425bd0 on `com.apple.tcc.access.changed` sweeps entries.
**rightDesc ABI (40B stack copy of static @0x1d3dcb6c8):**
`[0]`=descriptor ptr passed to preflight (service/right identity);
`[8]`=deny-bit mask; `[0x10]`=allow-bit mask; `[0x18]`=decided-bit mask;
`[0x20]`=u8 flag for the pid==0 arm. Field roles are disasm-derived
(use-site verified), not proximity guesses. ~~The literal service name
remains unreadable~~ — **RESOLVED D045:** the ctx bytes are inside this
slice (`__DATA_CONST,__const` file+0x438248); `ctx[0]` masked to
`0x1dd4ab240` lands in `__cfstring` → `strptr` → `__cstring` =
literal **`kTCCServicePostEvent`** (assertion-verified).

### 1c. Impl `audit_token_check_tcc_access` @+0x340da0

```
pacibsp; sub sp,#0x60; stp x22,x21,[sp,#0x30]; stp x20,x19,[sp,#0x40]
   → d503237f d10183ff a90357f6 a9044ff4   (pin VERIFIED)
x20=x0 token-copy-ptr; x19=x1 descriptor(rightDesc[0])
if ([0x1d8ba6b50]==0) {                     lazyChecker cell (WS __DATA)
    sym = _tcc_lookup_symbol("TCCAccessPreflightWithAuditToken")
        = dlopen("/System/Library/PrivateFrameworks/TCC.framework/TCC",5)
          → dlsym ; store cell; if (sym==0) return 0   ARM I0: resolve-fail → 0
}
stp token32 → sp; x0=descriptor; x1=sp(token); x2=0
w0 = blraaz sym                            ; TCCAccessPreflightWithAuditToken
return (w0 == 0)                           ; cmp w0,#0; cset w0,eq
```

**Signature:** `int f(void *token32ptr, void *descriptor)`.
**Return:** 1 iff preflight returned 0, else 0 — collapses
{resolve-fail, transport, denied, unknown} into one bit.

### 1d. Preflight polarity — resolved from `analysis/TCC_22F82`

`TCCAccessPreflightWithAuditToken` @0x1850d03bc: writes `w8=1` default
to out+0x18, invokes `TCCAccessRequest` with reply block @0x1850d0560
(`w1≠0 → out=0`) and error block @0x1850d0548 (`w1≠0 → out=2`), returns
`out[0x18]`. Convention: **0 = allowed, 1 = denied (pessimistic
default), 2 = unknown/error** — consistent with SkyLight-internal
logic (impl_rc≠0 → ctx[0x10] = provably the allow-mask, else future
cached hits would return 1 on a deny — contradiction).

**Consequence:** the D041 probe's `result=1` on its SELF token means
**denied** — no "self-allowed vs gate-denied" asymmetry ever existed.
Prior docs implying `1=allowed` here are inverted. The
sender-vs-self-token question remains open only as an unverified-bytes
matter, not as an asymmetry needing explanation.

## 2. Outcome matrix — one bounded SLSPostEventRecord post (expect 1002)

Observables per site: `*_calls` counters (denominators, survive disarm),
`rc`, plus records: gate{audit_ptr,audit_size,token32,desc40},
disp{token32,desc40,impl_ptr}, impl{token32,lazy_pre/post,service_ptr}.

| gate | audit_ptr/size | disp | disp_rc | impl | lazy pre→post | impl_rc | Producing layer |
|---|---|---|---|---|---|---|---|
| 1 | NULL | 0 | – | 0 | – | – | **ARM A** container NULL — structural; next var = who populates gCurMsgAudit on the post path |
| 1 | ptr,<0x34 | 0 | – | 0 | – | – | **ARM B** undersized container — anomalous shape |
| 1 | ptr,≥0x34 | 0 | – | 0 | – | – | **contradiction** — gate always calls dispatcher past arm B; if seen, disasm wrong or alt path |
| 1 | ok | 1 | 1 | 0 | – | – | **D1 self fast-path** (token.pid==WS-pid) → gate=1 → dispatch: conflicts with 1002 ⇒ deny lives post-gate |
| 1 | ok | 1 | 0 | 0 | – | – | **D0/D2** dispatcher early deny — discriminate via token.pid==0 (flag byte) vs cached-deny on key (desc masks + pid field) |
| 1 | ok | 1 | 0 | 1 | 0→0 | – | **I0 resolve-fail** — dlopen/dlsym never produced a sym → deny authored by TCC-symbol resolution, not policy |
| 1 | ok | 1 | 0 | 1 | 0→≠0 / ≠0→≠0 | 0 | **evaluated deny** — preflight≠0 → {denied=1, unknown=2, transport-error} collapsed at this boundary |
| 1 | ok | 1 | 0 | 1 | →≠0 | 1 | **contradiction** — disp returns impl_rc raw on miss; if seen, disasm/arm map wrong |
| 1 | ok | 1 | 1 | 1 | →≠0 | 1 | allowed-through — 1002 impossible ⇒ deny post-gate |

Repeat-post rows add cache dynamics: post1 miss→impl (deny bits stored
when task-port resolves); post2 disp_calls=1/impl_calls=0 → **cached
deny** vs renewed impl entry (insert deferred/death-watch fired/tcc.
access.changed sweep). A second deny after an impl_rc=1 post identifies
the fold/cache arm.

**Boundary-residual (cannot be observed at these three sites):**
preflight's raw value (denied=1 vs unknown=2 vs transport-error) —
needs an intra-impl read of w0 post-blraaz, or tccd-side reply
correlation. Also unobservable: cache dict contents (only effects).

## 3. Candidate review — `libmachook/MacWSSenderObservation.{h,c}` +
`mac_hooks.m`/`Makefile`/`cydiasubstrate_macos.m` working-tree diff

Overall structure is sound: dedicated `/tmp/macws_sender_observation`
sentinel (NOT the umbrella — coordinator's unconfounded-switch
requirement met), isWS+permit-gated install, all three prologue pins
byte-exact, record-only wrappers, errno save/restore, 256-record
budget + dropped counter, site-call denominators that survive live
disarm, per-record tid/seq correlation, no return/cache/audit/policy
mutation, reads mirror the originals' own dereference domain
(container [+4], token [+0x14..+0x33] under the same size check;
caller-stack copies at disp/impl).

### Blocking defects (fix before any deploy)

| # | Defect | Evidence | Effect |
|---|---|---|---|
| B1 | `MACWS_SENDER_OBS_OFF_LAZY_CHECKER` `0x5AC1B50` wrong — va `0x1d8ba6b50` − base `0x1850e5000` = **`0x53AC1B50`** | impl disasm `ldr x8,[x21,#0xb50]` on adrp `0x1d8ba6000`; python delta `−0x4e000000` | `g_macws_obs_lazy` points ~1.2 GB low — lazy_pre/post read unrelated mapped bytes → poisoned resolve-fail discriminator |
| B2 | `MACWS_SENDER_OBS_OFF_RIGHT_DESC` `0x542E66C8` wrong — va `0x1d3dcb6c8` − base = **`0x4ECE66C8`** | gate disasm `adrp 0x1d3dcb000+0x6c8`; delta `+0x5600000` | `g_macws_obs_right_desc` lands ~90 MB high — `memcpy(desc,ptr,40)` reads wrong region; garbage desc fields, possible fault if unmapped — could crash WS on first gate record |
| B3 | `MACWS_SENDER_OBS_OFF_AUDIT_SLOT` `0x5AC11B8` wrong — correct `0x53AC11B8` | same arithmetic | **latent only** — unused this diff (gate slot still computed inline correctly at mac_hooks.m:6184); still wrong in the "single source of truth" header |

### Non-blocking corrections

- ~~`mac_hooks.m` impl comment asserts `x1 = rightDesc[0] (the
  kTCCServicePostEvent CFStringRef)` — unproven~~ **WITHDRAWN D045:** the
  literal bytes DO demonstrate it — `ctx[0]→__cfstring→__cstring` =
  `kTCCServicePostEvent` (my earlier "unreadable" claim was wrong: the
  desc is file-carried in this slice). Backend's label is accurate.
- Record captures no arm discriminator at the gate beyond
  audit_ptr/size — sufficient (arms A/B distinguishable), fine.
- `desc[40]` at GATE site is the static rightDesc bytes — same content
  as the disp site's stack copy; keeping both is harmless redundancy.
- `pthread_mutex`-protected sync block-invoke (+0x185425504) means the
  insert path is NOT async — my earlier "first post may deny while
  pending" line is withdrawn; verdict always synchronous.
- `xpc_bootstrap_services` IA+0 signing fix (arm64e) is correct and
  unrelated-but-good: raw MSFindSymbol pointer needed sign for C call.
- Provider diff (cbz inversion, terminal braaz/brabz/blraaz/blrabz,
  per-failure dladdr logging, `sys_icache_invalidate(thunk_used)`)
  matches the reviewed D036 relocator semantics; the three sites'
  prologues (pacibsp/sub/stp/stp) all take the verbatim path + appended
  ldr/br branch-back — compatible.
- `MACWSHookPermitV2` exists provider-side with the exact 4-arg
  (base,uuid,offset,expected) contract the new registrations use —
  offset+uuid matched, fail-closed on drift.
- IOMFB ABI table now pins the Ventura uuid `7AC531B7-…` (matches the
  extracted image) and leaves 15.6.1 as `{0}` → fails closed. Correct.

### Requirements for the pending diff (if the candidate is revised)

1. Fix B1–B3 constants (`0x53AC1B50`, `0x4ECE66C8`, `0x53AC11B8`) and
   add a host-side unit check that recomputes each `OFF_*` from the
   documented VA comment — the constants and comments already disagree.
2. Soften the `kTCCServicePostEvent` assertion to unverified-descriptor
   wording in both files.
3. Keep `site_calls` pre-check ordering (denominator survives disarm) —
   already correct.
4. Add a one-line record field note documenting that `impl_rc=0`
   collapses {resolve-fail, transport, denied, unknown} and that the
   raw preflight value is a boundary-residual — prevents the
   "impl_rc=0 ⇒ explicit policy deny" misread the coordinator flagged.
5. No new observation of unrelated process tokens: records already
   limit capture to the sender token the gate itself receives —
   compliant; keep it that way.
6. Deploy-time acceptance (future packet): one bounded post ⇒
   `gate_calls=1`; `site_calls` denominators visible; changed-bytes
   readback `58000051 d61f0220` at all three offsets; original
   forwarding verified by identical rc=1002 reply.

## 4. My own corrected ledger

| Prior claim (doc/result) | Correction |
|---|---|
| responsibility103 "REFUTED at binary level" | → no direct static call in checked import table only; indirect/dynamic/tccd-mediated NOT excluded — THEORY (D042 doc §3 + §5 + result amended in place) |
| "first post could deny while async request in flight" | → withdrawn; verdict fully synchronous; PAC block is a mutex'd sync invoke (task-port resolution) + keyed continuation (death-watch THEORY) |
| "impl x1 = ctx_field" | → refined: impl(token32ptr, rightDesc[0] descriptor); descriptor is what preflight receives as its service/right arg — identity unverified |
| probe `result=1` treated as allow-compatible | → polarity: 0=allowed/1=denied/2=unknown (TCC_22F82 disasm); probe self-token was DENIED — no asymmetry |

Stopping after handover per packet.

(handover to codex now)

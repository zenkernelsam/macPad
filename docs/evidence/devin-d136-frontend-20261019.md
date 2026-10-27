# D136 — TCC/responsibility upstream-cause evidence reconciliation

Date 2026-10-19. Packet `coordination/_relay/devin-136-frontend-task.md`.
Read-only synthesis of D100–D106 + R003/R005–R008 + D050/D055/D056 +
D117/D119/D121. No new archaeology; all items cite saved artifacts or
exact-binary disasm. No device/probes/TCC/kernel/source/Git operations.

## 1. Strict FACT/THEORY ledger

### (a) Pre-remap generation — transport-invalid

| Claim | Class | Evidence |
|---|---|---|
| `com.apple.tccd.system` absent from **system domain on this device** | FACT (scoped) | `launchctl print` exit 113 (R007, corrected scope) |
| All sends returned `_xpc_error_connection_invalid` (8/8) | FACT | R006 `tccd_send_message` +940 captures |
| Remap missing was in `Metal_hooks.x`'s *second* parallel table (`macws_private_chroot_service_name`), not `macws_private_bootstrap_service_name` | FACT | R007 fix record |
| The R005-era `w0=1`→1002 was the client-side transport default — **no tccd ever ran the request** | FACT | R007/D104 §4 |
| Therefore any "responsibility getter" attribution for that era is void | FACT | corollary |

### (b) Post-remap — real replies, empty content

| Claim | Class | Evidence |
|---|---|---|
| Real `OS_xpc_dictionary` replies round-trip to `com.apple.macosbooter.tccd.system` | FACT | R007 corrected capture table (type reads are from later sends — per-capture pairing unproven) |
| `xpc_dictionary_get_bool(reply,"result")` → 0 | FACT (as accessor read) | R007; key presence/absence/type **unproven** (missing→0, false→0 control) |
| Reply contents beyond `count=0` never captured | FACT | D105 §5; R008 decode mechanism exists, no decoded dict artifact |
| `result`-absent vs `result=false` vs P0/P1/P2 internal class — undecidable from reply alone | FACT | D104 §4 |
| SkyLight still returns 1002 | FACT | R007 table; unchanged |

### (c) Correlated `#103` — responsibility getter error (R003 generation)

| Claim | Class | Evidence |
|---|---|---|
| WS 48891 → tccd 48750: msgids `48891.17`/`.19` (PostEvent) → `responsibility_get…from PID[49186/49199]: (#103)` → `count=0` → `sls_rc 1002` — **same-msgID correlation** | FACT | `R003-tccd-key-chains-20261018.txt` + `R003-tcc-fresh-attribution-20261018.json` |
| Logged pid = **declared `target_token`'s pid** (injector), not sender WS | FACT | msg text dump `{pid:49186,…}` matches `from PID[…]`; D102 map live-confirmed |
| #103 fires on launchd-hosted chroot processes AND self-target SSH probe AND iconservicesagent | FACT | R003 records (49211, 48579, R002 46525) |
| Ventura getter returns `rc=-1 errno=103` for **both** chroot and live-native tokens | FACT | R004 controls (`control_self`, `control_live_native_token`) → **environment/provider-level**, not sender-specific |
| tccd REQUEST `sender_auid=501` is **interpose-forged** (`audit_token_to_auid_new` writes val[0]=501) | FACT | D053/D056 — logged auid is not kernel truth |

### (d) Upstream kernel/provider cause

| Claim | Class | Evidence |
|---|---|---|
| Getter `0x18b320b00` copies caller token, extracts `val[5]=pid`/`val[7]=pidversion`, calls helper `0x18b320b2c` → auth stub `0x18b321bc0` with `x0="Quarantine"`, `w1=0xb6` (selector 182) | FACT | D106 verbatim disasm on exact 22F82 `A2D17FF6` image |
| libquarantine synthesizes no errno (zero `mov #0x67` in `__TEXT`); 103 propagated verbatim from provider | FACT | D106 §3 full sweep |
| Stub target = shared-cache auth-pointer slot `0x1dfef8f88`; provider identity unresolved offline | FACT | D106 §4 + D117/D119/D121 |
| 15.6-adjacent: `__sandbox_ms`==`__mac_syscall` alias; `__mac_syscall` dispatch loop defaults `ENOPOLICY`@1587 when no `mpc_name=="Quarantine"` with `mpo_policy_syscall` | FACT for 15.6/adjacent xnu source; THEORY for 22F82 device kernel | R008 §2-3; device kernel = `8792.82.2` vs source `8792.81.2` |
| Three ENOPOLICY producers in `__mac_syscall` (precheck / no-match :1587 / matched-handler-returns-103) — observed 103 doesn't select | FACT (source) | R008 §3, D106 §5 |
| "Responsibility entries follow launchd job tracking" | THEORY — and largely moot | consistent with semantics but the observed failure is one level upstream (policy dispatch); R006 refuted "launchd hosting suffices" |

## 2. D103/D105/D106 apparent contradictions — resolved

1. **D103 "len!=0x20 is an empty-reply producer"** — RETRACTED by D104 §5
   erratum: malformed token only narrows which process is evaluated
   (fallback to `_requestingProcess`); empty reply still requires the
   downstream failure. Producer class list corrected.
2. **D050 "lookup runs on connection's audit"** vs **D105 "runs on
   declared target_token"** — D050's case was self-target (sender==target
   degenerate). WS records (pid 49186/49199 ≠ sender 48891) prove the
   getter runs on `_accessingProcess` = declared target token. D105
   reading stands; D050 wording superseded.
3. **R002 "WS postmin → 0 tccd lines"** vs **R003 WS posts produce #103**
   — different generations/windows: R002's were cached denies after the
   generation's first evaluation; R003 caught the first posts fresh.
   Both true, not contradictory (D105 §4).
4. **R005 "launchd hosting suffices"** — refuted twice: hosted
   iconservicesagent/probe got #103 (R003), AND R004 controls show the
   getter itself 103s on native tokens. Hosting is irrelevant to the
   provider-level failure.
5. **D105 "#103 identifies P0 uniquely"** vs **D106 three kernel
   producers** — refinement, not contradiction: tccd-side the `#103` log
   line can only come from the getter arm (P0 among tccd classes);
   *within* the getter, kernel has three producers. Both statements true
   at different levels.
6. **D056 "orphaned stray / SETEXEC missing"** — retracted by D057:
   `POSIX_SPAWN_SETEXEC` is active (`main.m:119/:133`); launchd-tracked
   pid IS the macOS process. Re-opening stage-1 cause — which (4)+(d)
   then resolve: the failure is policy-dispatch, not tracking.

## 3. Missing current-generation witness (exactly)

- **No tccd-window capture exists for the current generation** (the live
  WS 22527 / current tccd): its first-miss PostEvent request's
  msgID-correlated `SEND → REQUEST_MSG → ERROR(#?) → RECV` quadruple is
  unrecorded; any posts since are cached denies.
- **No decoded reply dictionary** from any generation — `result` key
  presence/type unknown; `count=0` is the only observed shape.
- **No successful responsibility lookup has ever been observed** in this
  environment (all four getter controls failed or are untested).
- No pidversion/asid material printed in any capture.

## 4. Smallest safe discriminator for the upstream root cause

The remaining root-cause claim is which `__mac_syscall` branch produced
103. The single smallest discriminator = **the already-frozen
`macws-policy-snapshot-v3` authorization packet** (read-only kernel
`mac_policy_list` enumeration, hash `8bb90399…`, prepared not executed):

- **Correlation fields**: same-boot pairing of (i) kernel version/build,
  (ii) policy count + per-entry `mpc_name` + `mpo_policy_syscall`≠NULL,
  (iii) one live probe call to the Ventura getter recording `rc`/`errno`
  with pid/pidversion of its token. All captured in one bounded window.
- **Decides**: "Quarantine" absent or lacking `mpo_policy_syscall` →
  branch (b) = kernel MAC-policy contract gap → **no userland repair is
  justified**: no spawn-side, TCC.db, or identity change can fabricate a
  kernel policy; the honest path is a responsibility route that does not
  traverse it (Indirect-broker viability still gated on D056's unrun
  handler RE) or accept the structural bound.
  "Quarantine" present with ops → branch (c) = the policy's own syscall
  denied → the pid-keyed responsibility entry is the genuine missing
  input → **spawn-side registration** becomes the justified repair layer.
  Precheck (a) would not produce 103, so a confirmed-(b/c) split also
  clears (a).
- Optional cheaper prerequisite (no kernel read): resolve stub provider
  `0x1dfef8f88` via one live slid-8-byte read or the covering fixup
  record (D121 checklist) → confirms `__mac_syscall` is even on the path.

## 5. Repair-layer statement

Until the discriminator runs, the only honest statement is the ordered
one above. No repair is currently justified by evidence: the
environment-level 103 (native tokens included) already excludes
sender-side and launchd-registration fixes as sufficient; kernel policy
presence is the deciding unknown.

(handover to codex now)

# D095 — audit: renewed private R031 compiler replay (scope, checklist, R005 link)

Offline audit only — no device, no execution, no edits. Sources:
R005 (live-tcc-branch + responsibility-audit), R006, R014, R024,
R031, R032, D033 task/result, `WORKER_SWITCH_HANDOVER_REQUEST.md`.
Local `analysis/r031-f11h/` artifacts re-hashed and verified intact.

## 0. Park status — currently binding

- D033 task: "**R031 candidate grammar/audit passes but NEVER
  compiler-tested; park it. No more private compiler replay now.**"
- Worker-switch handover: "prioritize recovery of last known visible
  wallpaper/Dock/menu bar before production integration of compiler
  candidates. Never claim grammar success is compiler/pixel success."
- The park remains in force. Everything below is a *prepared* packet
  that activates only on an **explicitly renewed, named grant** — the
  D033 priority order (visible desktop first) is unchanged. D091/D094
  confirm the desktop is currently `ready`/advancing, so the visible-
  baseline precondition is satisfied *today*, but that does not un-park
  the replay.

## 1. What the reviewed chain establishes (evidence, not theory)

| Link | State | Source |
|---|---|---|
| Production crash site | `iconservicesagent` loops in `MPSLibrary::MPSKey_Compile → MTLReportFailure → __assert_rtn` (root-role preheat thread, chroot arm64e) | D033 inventory |
| Service-side fault locus | `readModuleFromBinaryRequest → fatalDiagnosticHandler → abort` on the type-15 F11H request; client sees `Connection interrupted` | R014 (crash stacks, 16 owned `.ips`) |
| Exact reader rejection | `Unknown attribute kind (71) (Producer: 'LLVM16.0.0' Reader: 'LLVM ')`, 4/4 hooked instances, byte-verified hook at MTLCompiler `+0x7985c` | R024 |
| Candidate construction | Canonical bitstream retarget — only `MODULE_CODE_TRIPLE` → `air64-apple-ios19.0.0-macabi`; 0 record diffs across 21 blocks/476 records; VST/FNENTRY re-derived canonically | R031 §3, R032 §2 |
| Prior retargets withdrawn | R028 in-place STRTAB splice never touched the real record; R030 splice invalid bitstream — neither tested the target-OS path | R031 §2 |
| Grammar validity only | Host clang 17 + device LLVM-16 `llvm-dis` accept all payloads — **no real compiler reply has ever been observed for the correct triple** | R031 §4, R032 §3 |

**Open question the replay answers**: does the exact 22F82 MTLCompiler
reader accept a module carrying the real `air64-apple-ios19.0.0-macabi`
triple — i.e., does it get *past* the attr-71/target-OS failure the
converted module dies on? Everything else (grammar, container, request
shape) is already pinned.

## 2. Hash-bound checklist (verified 2026-10-19; local items re-hashed)

### Frozen local artifacts (`analysis/r031-f11h/`)

| Artifact | Bytes | SHA-256 |
|---|---|---|
| `r031-candidate.type15.data` (the request) | 7,184 | `75663758aa1a914e1cd538ec22690ee73d9e0f9df52ea6c43f56032f40d3da1e` |
| `r031-candidate.record` | 6,944 | `5d805ec5f65c94c94377370659ebd9f8eae71749486b272ba6c4ed68268252cb` |
| `retargeted-F11H.payload.bc` | 6,920 | `8dc40d786b9e82befaa9e2d7166d88dd63ad9d64339cb293616656acf749c269` |
| `mpsimage.r031-candidate.metallib` | 17,293,876 | `7b71a6479ed162bf4cf4af2445978d7bb849b529d502e9254d3ad67006803aae` |
| `bcrewrite.py` (writer) | — | `9b34ab9742807e434c5a284a91c18e60ee4b81ef2222f4a4e2901395bd10e4e5` |
| control `type15.control.data` (device `client/r028/`) | 7,632 | `ffa7daa51d8de4493c06c8c3e65717b116521d4e9fb6cc89fa8e89f91cbe2e90` |

### Device-side identities (as of R024/R031 — re-verify at run time)

| Item | Identity |
|---|---|
| `r024_wrapper` (bundle exe) | CDHash `44767b6a…`, trustcache live |
| patched `MTLCompilerService.real` | SHA `7bc22e08e234c18fb5a230f930edf5ea0665fe32bf46eae8a3ba9ab9538bce32`, CDHash `b3e29eef…` |
| `r024_diag.dylib` arm64e | SHA `61aec4da…`, CDHash `d0b584a8…`, trustcache live |
| `r028_replay_client` | CDHash `2f0d5143…`, trustcache live |
| `r014_redirect_v6.dylib` | CDHash `1bb93be6…`, trustcache live |
| unpatched no-hook `.real.orig` | SHA `4d318283…` (for the confirmation endpoint) |
| Owned tree source | `/var/mobile/macws_r028_evidence_20261018/owned/r014/` → live path `…/XPCServices/MacWS/r014/` |

### Baseline re-verification (pre/post)

`com.kdt.macosbooter` pkg 0.3.4; provider `b2ae21da…`; libmachook
arm64e `47f78880…` / arm64 `6de9b688…`; native SpringBoard 872 /
tccd 883 / backboardd 905 (D091: still identical lstarts); all six
markers absent; **production compiler count** — record pre-run
(R024 recorded 14, R031 observed 15 — a stock on-demand instance;
the run must capture the actual count before and after).

## 3. Exact scope of a renewed replay

Activation requires ALL of: (a) named grant for this packet;
(b) desktop `state=ready` + advancing seq + unchanged WS/tccd pids;
(c) marker/ residue baseline `find` captured; (d) production compiler
count captured.

1. Restore `owned/r014/` → `…/MacWS/r014/` (mv, not copy — keeps the
   evidence tree the single source of the bytes). Verify the three
   CDHashes listed in `jbctl trustcache` before any launch.
2. **Control run**: `type15.control.data` — expected
   `Unknown attribute kind (71)` fatal via the R024 diag channel;
   proves the endpoint is live and the reader behaves as established.
3. **Candidate run**: `r028_replay_client` +
   `DYLD_INSERT_LIBRARIES=r014_redirect_v6.dylib`, in the chroot,
   exactly R028 §5.2; capture reply object, `service/stderr.log`,
   service pids, any `R024-DIAG-*` lines.
4. **No-hook confirmation (conditional)**: only if the candidate
   returns a real object/no-error reply — rerun on an endpoint whose
   `MTLCompilerService.real` is the unpatched `4d318283…` copy, to
   exclude the R024 diagnostic dylib from the success path.
5. **Success criterion (unchanged from R031 §6)**: a real reply is
   not success — only a fully specialized function or the no-error
   reply carrying a real object/metallib counts. `Connection
   interrupted`, attr-71, or any new fatal = the negative branch.

**Hard exclusions**: no production compiler path/bundle touched; no
WS/desktop/launchd restart; no marker creation; no trustcache add —
if any required CDHash is missing, the run stops
(`missing-authorization`); no second candidate or improvised retry;
no source/package/Git change.

## 4. Rollback/cleanup contract

1. `mv …/MacWS/r014/` back under the evidence directory (single-owner
   tree; no residue at the live path).
2. `ps` witness: zero `r014`/`r028`/replay/`llvm` owned processes;
   each service pid recorded and confirmed exited.
3. `find` diff vs the step-0 baseline on the MacWS tree + evidence
   dirs; crash reports (`MTLCompilerService.real-*.ips`, helper
   `.ips`) are *expected* artifacts — moved to the evidence tree, not
   deleted.
4. Markers: all six absent in both namespaces.
5. Production compiler count returns to baseline; native pids
   unchanged; package/provider/libmachook hashes unchanged.
6. Trustcache: disposable CDHashes remain as inert admissions
   (documented residual — `jbctl` has no per-hash removal; `clear`
   deliberately not used).

## 5. Evidence still needed to connect a result to R005/IconServices

A replay result alone is a **compiler-reader statement** — it does not
yet touch the production crash. The gap items:

- **Same-module proof**: confirm the production `iconservicesagent`
  crash compiles *this* F11H module — needs the production-side
  request/module hash (crash-side metadata or a bounded capture of
  the real request) matched against `5d805ec5…`/`75663758…`, not an
  inference from the symbol name.
- **Candidate acceptance vs production fix**: even a successful
  replay on the private endpoint proves reader acceptance of the
  triple only; whether the same conversion heals the production
  `default-desktop-effects-macabi.metallib` path requires the
  candidate MTLB (`7b71a647…`) exercised in a bounded production-side
  A/B — **a separate named grant**, since that touches the real
  desktop's dependency chain.
- **No-hook elimination** (§3 step 4) is required before any
  acceptance claim, since R024's diagnostic dylib sits inside the
  endpoint.
- **Attribution discipline**: a passing replay + persisting
  IconServices crashes would re-open the supplier UID501/GID20 theory
  (currently untested, not supported — D071); a failing replay closes
  the "triple retarget heals F11H" hypothesis cleanly.

## 6. Verdict

Prepared packet complete: frozen hashes verified, scope bounded,
rollback defined, exclusions explicit. The replay remains **parked**
per D033 until the user renews it by name; visible-desktop priority is
satisfied today but does not constitute renewal.

# D071 — read-only backend consolidation (no device, no builds)

Scope: consolidate existing evidence for provider/F1 identity, the saved
IconServices crash, the smallest legal responsibility/TCC discriminator
under the D069 contract, and the layered-acceptance reconciliation.
Every row cites an artifact; theories and missing witnesses are labeled.

## 1. Merged provider/F1 — identity and actual-load evidence

**Identity (FACT, artifact-level):**

- Merged source = provider `3c4d581` + F1 `4bcf20d` (R007 audit).
- Package `com.kdt.macosbooter 0.3.4` `install ok installed`; uploaded
  .deb remote SHA matched host SHA (R010 §package).
- Installed libmachook UUID `76a98a5a-0c9c-32e5-93a3-b006f2bcd6d4`
  (R010); provider resolves in chroot at
  `/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`
  via the real load-command path (R007 FACT).
- R012 callsite bytes: built thin arm64e library read back from the
  installed file is **byte-identical**; installed callsite contains
  `pacia1716` with x16=0 (direct A-key/zero signing) — the actual
  deployed bytes match the authorized diff, no drift.
- Rootfs copies: inodes `250668646/250668647`, identical hashes
  (R010) — no inode-shadowing hazard present.

**Load/callability (FACT, isolated):**

- Post-install disposable thin probes loaded provider UUID
  `A755DD35-D80C-3D90-9312-D566DE392770` on the arm64e slice:
  `replacement_called=1 original_called=1`,
  `VERDICT=INSTALLED-ORIGINAL-CALLABLE` (R010 §5).
- `F1-VERDICT=PASS` exit 0 — the repaired exact-class method hook works
  against the installed provider in the disposable chroot probe (R010).

**Observed firing vs installed (the load-bearing distinction):**

- **Fired in live WS**: the `CGXSYNTH` sender gate — `#### CGXSYNTH`
  records emitted with `gCurMsgAudit` populated (52-byte creds) and the
  genuine `gate→dispatcher 0x1854257ec→impl` chain returning 0→1002
  (AGENTS.md R002-era update; D042-series). This is the only hook with a
  *live-WS firing* witness.
- **Installed but never witnessed firing in live WS**: the F1
  direct-drawable route (`F1-VERDICT=PASS` was a disposable-probe
  result; R010 explicitly: "a live F1 route witness remains missing"),
  and the R012 `__xpc_bootstrap_services` signed call — callsite bytes
  proven installed, but "a direct signed-call execution witness remains
  missing" (R012 §5). The surviving generation (WS pid 10447, no former
  XPC crash) is *negative-space* evidence: the path no longer crashes,
  not proof the call executes.

## 2. IconServices crash audit (saved runtime evidence, R005 excerpt)

Artifact: `coordination/artifacts/R005-iconservices-crash-excerpt-20261018.txt`
— verbatim from four `.ips` reports + live job list, captured 2026-10-18.

- **Identity (FACT)**: `iconservicesagent` pid 50980, parent launchd,
  path `/System/Library/CoreServices/iconservicesagent` (chroot stock),
  userID 0, coalition `com.macwsguide.iconservicesagent`; agent CDHash
  `1c0d3271…` trusted; injected libmachook arm64e CDHash `ee0f018f…`
  trusted — **dependency trust closed for both images**.
- **Daemon/role (FACT)**: job runs `launchdchrootexec 0 0` (uid/gid 0,
  `runAsRoot`); sibling `iconservicesd` runs uid/gid **240**, its store
  `/var/mnt/rootfs/Library/Caches/com.apple.iconservices.store` is
  `240:240 mode 0700` and the daemon is **stable** (no crashes).
- **Cache dirs (FACT)**: agent env `HOME=/Users/root`, `TMPDIR=/tmp`;
  `/var/mnt/rootfs/Users/root/Library/Caches` is **absent** on the fs.
- **Crash mechanism (FACT)**: `EXC_CRASH SIGABRT` on the MPS preheat
  queue; verbatim assertion — `MPSLibrary.mm:485 … Unable to get MPS
  kernel F11H. Error: Compiler encountered an internal error` via
  `MTLReportFailure→__assert_rtn`. Sibling workloop thread waits on the
  MPS key-compile lock.
- **Verdict (R005, stands)**: the abort is an **MPS kernel-compile
  internal error** — not a uid/gid/HOME/cache/trust failure.
  THEORY labeled there and still unproven: supplier's UID501/GID20
  recipe would not address an `MTLReportFailure` compile path.
- **Missing witnesses**: no lldb/dtruss on the actual
  `MPSKey_Compile` request bytes; the LLVM-side diagnostic (R024:
  `Unknown attribute kind (71)` = nocallback per independent wire audit)
  is a separate compiler-service failure, correlated not proven causal
  for the F11H abort.

## 3. Smallest bounded responsibility/TCC discriminator (D069 contract)

**Prepared spec** — one run, read-only, bounded:

- Channel: SSH, `SSHPASS` env only (never argv — the D069 argv
  credential use is a recorded violation, not a pattern).
- Executor: procursus `python3` (already trusted on device — **zero new
  binary, zero trustcache change**). ctypes → `libjailbreak.dylib`.
- Arm sequence: `jbclient_initialize_primitives()` →
  `jbclient_root_get_physrw()` (returned 0 in D069 setup; this is the
  step that arms `kread64` — **unverified under python's entitlement
  set**, the one open risk).
- Identity/verification (D069-mechanics, FACT): sysinfo XPC dict →
  `kernelConstant.base` must equal `staticBase+slide`
  (`0xfffffe0007004000 + slide`); `kread64(base)&0xffffffff` must be
  `0xfeedfacf`; `kread64` at `0xfffffe0007e95ee5+slide` must return the
  literal `Darwin Kernel Version … RELEASE_ARM64_T8103` string. Any
  failure → STOP, emit nothing but the failing check name.
- Bounded reads (only): `mac_policy_list` header double-read (tear
  abort), caps `numloaded≤max≤0x400`, `maxindex≤0x40`, names ≤0x80
  printable+NUL; per entry: name, `ops+0x388` and `ops+0x3a8`
  low-56-masked presence flags. No pointer is dereferenced past range
  checks; no PAC decoding.
- Sanitization (frontend §4): emit **only** slide-relative offsets and
  presence flags — no raw `kernbase`, `slide`, `entries`, `conf`, `ops`
  VAs. (D069 run violated this by printing raw values; flagged.)
- Forbidden by construction: no setters, no `__mac_syscall`/MAC
  syscall, no writes, no trustcache mutation, no restart, no TCC
  request.

**Contract met?** Partially — every property is satisfiable *except one
unverified link*: whether `jbclient_root_get_physrw` + `kread64` arm
under procursus python's entitlements. If they do, the whole run is
read-only with no trustcache change and no new executable — fully within
contract. If they do not (entitlement gate inside jailbreakd), the only
fallback is a newly-signed helper whose cdhash admission **is** a
trustcache change → the contract as written cannot be met and the task
must report missing authorization rather than degrade.

## 4. Reconciliation — provider, F1, reopen, layered pixels

| Layer | Status | Witness |
|---|---|---|
| Provider identity/bytes | **PASS** | byte-identical installed callsite, matched package SHAs |
| Provider load+call | **PASS (isolated)** | `INSTALLED-ORIGINAL-CALLABLE`, UUID `A755DD35…` |
| Hook observed firing in live WS | **gate only** | `#### CGXSYNTH` + populated `gCurMsgAudit`; F1 route firing **missing** |
| Final-composite stream | **PASS** | `ready` + continuously advancing seq, producer=WS pid |
| Visible pixels / Host presentation | **partial** | VNC/observer pixels accepted (D037); full Host-layer interactive acceptance remains open per SYNC heartbeat language |
| TCC / global input | **OPEN** | AppInput accepted (D041); global 1102/1002; #103 arm unresolved |
| Reopen freshness | **plan only** | R016 source-level plan accepted with limits; no device witness produced |

Consistent story: every layer below "live route firing" is proven; the
gaps are all *witnesses* (F1 route firing, signed-call execution,
reopen generation), not proven defects. Per checklist discipline these
stay open until a live generation produces each row — none requires
reopening closed experiments or importing share-package bypasses.

## Provenance

Sources: AGENTS.md baseline, SYNC.md, R005/R007/R010/R012/R013/R016
evidence docs, D041/D050/D055/D060/D065/D067/D068/D069 backend docs,
D069 frontend acceptance contract, share-lessons checklist. Local reads
only; no device contact this packet.

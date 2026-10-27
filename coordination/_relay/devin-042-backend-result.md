# devin-042-backend-result — provenance reconcile + global-boundary localization

Packet: `devin-042-backend-task.md` (read-only; auto-continuation off).
Scope honored: no restart, no deploy, no rebuild, no source/production
change, no runtime patch. Live desktop preserved; ssh via `sshpass -e`
(env-carried credential — no argv, no secrets/addresses in docs).

## Provenance — RESOLVED (per-slice identity, not guesswork)

Pulled live binaries + D037 preserve/stage dirs; compared LC_UUID +
`__text`/`__const` SHA-256 per slice against all local build outputs.

| file | live UUID / `__text` | match |
|---|---|---|
| CydiaSubstrate (fat) | `735E6385`/`57DA3076`, `a2b074cc`/`88f65a50` | **`CydiaSubstrateProvider/.theos/obj`** build (Oct-18 23:34, post-P3-hardening) = the D037 deployment itself (`82e74519` unsigned ↔ `134b7eb9` installed) |
| libmachook arm64e / arm64 | `2DAFB6A1`/`3D8E5D89`, `be0ed487`/`104d18e7` | **`libmachook/.theos/obj`** build (Oct-18 23:11), exact link |
| macwsinputd | `C2AF8FA7`, `bdd31226` | `.theos/obj` build — identical `__text`, different link UUID (same source) |
| macwsdisplayd | `50BED241`, `f5ba333b` | `.theos/obj` build — identical `__text`, different link UUID |

**Correction to D041 wording (applied to its doc + result):** the
whole-file hash deltas were signing/fat-vs-thin artifacts plus one
superseded staging slice (`657E80C1`, 5068B `__text` — not the final
deployed artifact). There is **no evidence of a post-D037 redeploy and
no foreign binaries**; installed code is the repo's own build output.
The `/tmp/d037/staged` thin slice predates the P3 hardening rebuild.

## WS-local gCurMsgAudit — no existing safe read path

`task_for_pid` denied (D037), `vmprobe` is a self-mmap test, `libkrw`
reads kernel VA only (a proc-walk reader would be a new tool + deploy —
out of read-only scope). Probe-process globals invalid (R003). So the
current-generation audit container was **not** read; the populated-audit
witness is historical (prior generation). Current WS log: `CGXSYNTH`
count = 0 — diagnostic never armed this generation.

## Observation-patch design (documented, NOT applied)

Three function-boundary permits on SkyLight-22F82
(`96676A53-B1E0-3D7E-B98B-B73873CD1880`), all prologues byte-verified
verbatim-capable, all via `MACWSHookPermitV2` fail-closed:

- `+0x318464` gate `CGXSenderCanSynthesizeEvents` — exists already
  (`macws_diag_sender_can_synth`): calls, audit ptr/size/euid/pid, rc.
- `+0x3407ec` dispatcher `WSAuditTokenHoldsCachedProcessRight` — NEW:
  entry count + return + audit pid/pidver (self vs cached vs miss).
- `+0x340da0` impl `audit_token_check_tcc_access` — NEW: entry count +
  orig return (raw TCC boundary; `==0` passes at this callsite).

Bounded fixed-size state (u64 calls, u32 last_rc, ≤0x34B audit snapshot,
4-slot ring); off by default behind `isWS` + runtime-diagnostics
sentinel + per-target permit; errno save/restore; **no return-value,
cache, audit, policy, or producer changes** — record-only.

## One-variable root-cause experiment

Arm **only** the impl counter (`+0x340da0`, gate counter as denominator),
one bounded `SLSPostEventRecord` canonical post (D041 shape, rc=1002):

- `impl_calls=0` → deny authored by dispatcher early paths
  (auid/`pid==0` or cached deny bits on `pid|pidver<<32`) — next variable
  is cache-key/rightDesc, not TCC.
- `impl_calls=1, rc≠0` → TCC preflight itself denies the sender's audit
  token — producing layer = TCC evaluation; only then does the
  `responsibility103` cache-key theory become the next variable.
- `impl_calls=1, rc=0, gate_rc=0` → TCC passed but deny bits were
  folded in — cache-fold/mask bug, a third distinct producer.

Each outcome names a different producing layer; not a generic retry.

## Health at close

`macws_final_composite.state`: `ready producer=10447 sequence=1529
validated-final-composite` (advancing from D041's 1176); workspace graph
`ready` with wallpaper/dock/menubar/backing; Finder 32 / Terminal 27
layers live. No owned jobs or probes running; no sentinels armed; no
device state altered.

Evidence: `docs/evidence/devin-d042-baseline-global-boundary-20261019.md`.
D041 corrections applied in place. No commit.

(handover to codex now)

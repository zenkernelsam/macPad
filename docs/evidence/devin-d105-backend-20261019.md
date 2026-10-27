# D105 — runtime #103 correlation: WS-originated vs probe records

Offline saved-evidence audit only. No device, probes, TCC/setters, KRW,
source, build, or Git operations. Sources are the saved artifacts under
`coordination/artifacts/` and the evidence docs; all excerpts verbatim.

## 1. The verbatim #103 record set (complete inventory)

Every saved runtime `#103` (`responsibility_get_responsible_audit_token_for_audit_token`
ENOPOLICY) record in the tree, by generation:

| Record | Generation (tccd / WS) | Sender | Target pid | Service | Source |
|---|---|---|---|---|---|
| `from PID[46525]: (#103)` | tccd 45068 | macwstccdprobe 46525 (SSH spawn) | 46525 (self) | PostEvent | `R002-tccd-oslog-window-20261018.txt` |
| `from PID[49186]: (#103)` | tccd 48750 / WS 48891 | **WindowServer 48891** | 49186 (injector) | ListenEvent (msgid 48891.16), **PostEvent (48891.17)** | `R003-tccd-key-chains-20261018.txt` |
| `from PID[49199]: (#103)` | same | **WS 48891** | 49199 (injector) | ListenEvent (48891.18), **PostEvent (48891.19)** | same |
| `from PID[49121]: (#103)` | same | **WS 48891** | 49121 | ScreenCapture (48891.20) | same |
| `from PID[48579]: (#103)` | same | **WS 48891** | 48579 (iconservicesagent, launchd-hosted) | ListenEvent (48891.21) | same |
| `from PID[49211]: (#103)` | same | hosted probe 49211 (launchdchrootexec) | 49211 (self) | PostEvent (49211.1) | same |
| `rc=-1 errno=103` ×2 | — | chroot helper controls | self-token + live-native-token (pids 51732/51948) | getter direct | `R004-responsibility-controls`/`R004-responsibility-boundary` JSON |
| `from PID[?]: (#2) ENOENT` | D052-era | iOS-side sender | — | (different producer — `proc_pidpath_audittoken`, path stage) | cited in `devin-d060-…-audit` |

D050 (`devin-d050-empty-tcc-reply-producer-20261019.md`) and D060
(`devin-d060-…-audit` / `…-review`) are **offline re-analyses** of the R002/R003
records and tccd disasm — they introduce no new runtime captures. The latest
verbatim runtime #103 records are R003's (2026-10-18).

## 2. The WS-originated records — exact excerpts

`coordination/artifacts/R003-tccd-key-chains-20261018.txt` (tccd 48750, WS
48891, both fresh generation; endpoint `com.apple.macosbooter.tccd.system`):

```
WindowServer[48891]: SEND: 0/7 synchronous to com.apple.macosbooter.tccd.system:
  request: msgID=48891.17, function=TCCAccessRequest, service=kTCCServicePostEvent,
tccd[48750]: REQUEST: tccd_uid=0, sender_pid=48891, sender_uid=0, sender_auid=501,
  function=TCCAccessRequest, msgID=48891.17
tccd[48750]: REQUEST_MSG: msgID=48891.17, msg={ ... target_token={pid:49186,...}
  TCCD_MSG_ID="48891.17" ... }
tccd[48750]: ERROR: responsibility_get_responsible_audit_token_for_audit_token()
  from PID[49186]: (#103) Policy not found
WindowServer[48891]: RECV: synchronous reply <dictionary: ... { count = 0 ... }>
```

and the second post:

```
WindowServer[48891]: msgID=48891.19, service=kTCCServicePostEvent,
  [target_token pid 49199]
tccd[48750]: ERROR: responsibility_get_responsible_audit_token_for_audit_token()
  from PID[49199]: (#103) Policy not found
WindowServer[48891]: RECV count = 0
```

**Same-message correlation is msgID-established**, not window-proximity:
`R003-tcc-fresh-attribution-20261018.json` records `postmin_1.ws_msgid =
"48891.17" → sls_rc 1002` and `postmin_2.ws_msgid = "48891.19" → sls_rc 1002`
— the *same* message IDs that produced the #103 lines are the posts that
returned 1002 at the injector.

Important semantics for `from PID[x]`: the logged pid is the **target token's
pid** (`_accessingProcess`, the declared `target_token` — injector
49186/49199), not the sender (WS 48891 is the kernel-attested
`_requestingProcess`). D102's map is confirmed live: the getter runs on the
declared target.

## 3. Provider-level corroboration (R004 controls)

The Ventura `libquarantine` getter itself returned `rc=-1 errno=103` for a
live **chroot** token AND a live **native** token
(`control_self`/`control_live_native_token` in
`R004-responsibility-boundary-20261018.json`) — the ENOPOLICY is
provider-level (Quarantine `mac_policy_list` no-match per R008's
`__mac_syscall` walk), fired before any relationship-entry query. So the
observed #103s identify P0 (getter-rv) uniquely among D102's classes — the
`(#103)` line can only come from the getter-error arm — but do not
discriminate "policy absent" from deeper causes within it.

## 4. WS vs probe separation — answers to the narrow question

- **Post-remap WS PostEvent requests causally tied to P0 #103: YES — two of
  them.** msgids 48891.17 (target 49186) and 48891.19 (target 49199), both
  `service=kTCCServicePostEvent`, sender=WS 48891, both ending in
  `count=0` replies and `rc=1002` at the injectors, same tccd generation
  48750.
- Probes are corroborating, not the only producers: R002's 46525 (SSH-spawned,
  self-target) and R003's 49211 (launchdchrootexec-hosted, self-target) both
  #103 → the failure is environment-level (provider), independent of sender
  provenance or launchd hosting.
- **Not a universal-WS-cause claim**: the records establish the failure for
  *the target tokens involved* (fresh-spawn chroot injector/probe/ISA pids).
  A target whose token had a resolvable responsibility entry was never
  captured — no such target exists in saved evidence.
- **Missing correlation ≠ absent failure**: R002's "WS postmin → 0 tccd lines"
  was *cached* checker output within that generation (deny bits folded after
  the first miss), not a request that ran clean. R003's fresh-generation
  capture is the fix: it caught the first posts before the fold.
- **Current/D041-era generation (WS 10447)**: no tccd-window capture exists for
  its posts — its #103s (if the first post in that generation missed) were not
  recorded. `state=ready` + sequence advancing is orthogonal to TCC.
- pidversion is **not** in the logged token descriptor (`{pid,auid,euid}` only)
  — correlation is msgID + target pid + tccd-generation; the 32-byte wire token
  is present but unprinted.

## 5. Missing links (explicit)

- No capture shows the reply dict's *contents* beyond `count=0` — per D050 the
  empty-send path is the bail shape, and per R007's correction the `result`
  key's presence was never proven. `count=0` is not an explicit policy denial.
- No pidversion/asid material in any capture → can't separate provider-#103
  from a hypothetical token-field mismatch downstream (per D102 caveat: no
  extra tccd-side comparison observed; dependency validation unresolved).
- No WS-generation-10447 tccd window → the current generation's first-miss
  request is unobserved (its posts to date were cached denies after the
  generation's first evaluation).
- R004's controls prove the getter's ENOPOLICY at provider level for both
  chroot and native tokens — so even a perfectly-formed WS request hits it;
  what has never been observed is a *successful* responsibility lookup in this
  environment at all.

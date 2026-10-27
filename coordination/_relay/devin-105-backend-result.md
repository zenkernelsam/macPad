# D105 backend result — WS-vs-probe #103 correlation

Offline saved-evidence audit. Doc: `docs/evidence/devin-d105-backend-20261019.md`.

## Narrow answer

**Yes — post-remap WS-originated PostEvent requests are causally tied to P0
getter #103, by msgID correlation** (not window proximity). In
`R003-tccd-key-chains-20261018.txt` (tccd 48750 / WS 48891, post-remap endpoint
`com.apple.macosbooter.tccd.system`):

- `msgID=48891.17` service=`kTCCServicePostEvent`, sender_pid=48891 (WS),
  `target_token={pid:49186}` → `ERROR: responsibility_get_…_for_audit_token()
  from PID[49186]: (#103) Policy not found` → `RECV count=0` →
  `postmin_1 sls_rc=1002` — same msgid.
- `msgID=48891.19` (target 49199) — identical chain → `sls_rc=1002`.
- Plus WS requests 48891.16/.18/.20/.21 (ListenEvent/ScreenCapture, targets
  49186/49199/49121/48579-launchd-hosted) — all #103.

Probe records corroborate independently: R002 SSH-spawn `46525` self-target
#103; R003 launchdchrootexec-hosted `49211` self-target #103; R004 getter
controls return `errno=103` for live chroot AND native tokens (provider-level,
Quarantine name no-match). `from PID[x]` logs the **target token's** pid —
the getter runs on the declared `target_token` (`_accessingProcess`),
consistent with D102.

## Missing vs absent — kept distinct

- R002's "WS post → 0 tccd lines" was a **cached deny** (deny folded after the
  generation's first miss), a missing correlation — not an absent failure.
- The `count=0` reply is the bail shape (D050's single reply site), **not**
  an explicit `result=false` — empty dict ≠ policy denial.
- The observed #103 uniquely identifies the P0 getter arm among D102's
  classes, but cannot separate "Quarantine policy absent" from deeper causes.
- Current/D041-era generation (WS 10447): no tccd window captured — its
  first-miss request is unobserved; posts to date are cached denies.
- No successful responsibility lookup has ever been observed in this
  environment.

(handover to codex now)

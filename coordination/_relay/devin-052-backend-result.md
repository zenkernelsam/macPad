# D052 backend result — sender-attribution control executed

2026-10-19. Executed `coordination/_relay/devin-052-backend-task.md`.
One bounded control; desktop preserved (`ready`, WS 10447, seq
3619→3707 throughout).

## Result

iOS-launchd-spawned disposable sender (`com.macws.d052sender`,
user/501 domain, `/var/jb/tmp` helper, euid=0 auid=-1) sent ONE
authentic 7-key `TCCAccessRequest` (preflight=true) to
`com.apple.macosbooter.tccd.system` (Ventura `tccd system` pid 10300,
`42c388e8…`).

**Reply: dictionary count=0 — empty**, and the server-side producing
stage MOVED:

```
chroot sender (R002): responsibility_..._for_audit_token() (#103)
                      Policy not found          -> stage-1 fail
iOS sender  (D052):   proc_pidpath_audittoken() (#2) ENOENT
                                                    -> stage-2 fail
```

No #103 for the launchd-spawned sender → responsibility stage passed;
chain then failed resolving the sender executable path (`/var/jb`
rootless bind). Both bails end at the same unconditional empty `x23`
send (D050 `0x1000069cc`).

## Attribution model now (FACT)

tccd's attribution chain needs BOTH responsibility registration AND a
kernel-resolvable executable path. Chroot senders fail stage 1; an
iOS helper under the Dopamine bind fails stage 2. Empty dict = early
bail signature; a working path emits populated `auth_*` fields
(same-window native tccd[883] reference: `auth_value`/`auth_error_code`
/`result` even on identity refusal).

## Corrections absorbed / recorded

- auid is NOT the differing axis — launchd `user/501` job still gets
  `auid=-1`; `sender_auid=501` in REQUEST is connection-inferred.
- responsibility getters stay SYMBOL_MISSING client-side on iOS
  (R004 consistent); server-side absence of #103 is the evidence stage
  1 passed — recorded as indirect evidence, not client-proven.
- No `result=1` policy claim; no policy write/consent path was
  exercised (preflight variant, verified wire schema).
- "Nonempty would support, not prove" — we got the third outcome:
  different error stage, same empty reply.

## Helper build notes (packet-owned)

`analysis/devin-d052/`: source `77dd7d67…`, unsigned `7216aaff…`,
device-signed `1cc8636e…`. Three pre-send crashes recorded (no request
left the process in any): raw dlsym fptr PAC fault, libxpc
`xpc_copy_description` null-node fault, `&_xpc_type_null` ≠ null object
(`xpc_null_create()` is the real one). iOS SDK hides the mach-service
API + XPC_NULL; helper uses local decls + `-undefined dynamic_lookup`.

## Next producing-layer experiment

Same helper at a canonical real-fs path (`/private/var/mobile/…`),
one resend: ENOENT gone/new error/`auth_*` reply → stage advances;
persistent ENOENT → trust-admitted-exec textvp gap (jailbreak-side,
not TCC).

## Hygiene

Job bootout rc=0; helper/plist/tmp/crash artifacts removed; no residue;
no repeated sends (one control request total); creds env-only.
Docs: `docs/evidence/devin-d052-sender-attribution-control-20261019.md`,
raw captures under `analysis/devin-d052/`.

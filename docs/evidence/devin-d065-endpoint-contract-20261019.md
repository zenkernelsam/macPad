# D065 — TCC endpoint routing and role contract (read-only)

Date: 2026-10-19. Backend. Read-only device job/plist/log/status reads + local
source/binary audit. No endpoint switch, no service start/stop, no requests,
no helper, no restart. Provider-absence remains **HYPOTHESIS** (D064
calibration) — nothing here depends on it.

## 1. Endpoint map — all TCC-facing names

| Role | Job | Domain | Vended Mach name | PID | Binary |
|---|---|---|---|---|---|
| iOS user tccd | `com.apple.tccd` | `user/501` | `com.apple.tccd` | 883 | iOS 16.3 tccd, UUID `0120CE08` (R005: zero responsibility imports — different protocol generation) |
| Ventura system tccd | `com.macwsguide.tccd-system` | `user/501` | `com.apple.macosbooter.tccd.system` (plist MachServices, endpoint `active=1` port `0xc840f`) | 10300 | Ventura tccd argv `system`, UUID `9845D17F`, via `launchdchrootexec 0 0 /var/mnt/rootfs …` |
| iOS system tccd | — | — | `com.apple.tccd.system` — **never registered on iPadOS** (runtime-confirmed 2026-10-15, recorded in `mac_hooks.m:15866`) | — | — |
| Ventura user tccd | — | — | — | — | **no `com.macwsguide.tccd` job exists** (plist dir has only `tccd-system`) |

plist keys (strings-decoded): `POSIXSpawnType=Adaptive`, `RunAtLoad`,
`EnablePressuredExit`, `PublishesEvents→com.apple.tccd.events`,
stdout/err `/var/jb/var/mobile/tccd-system.{out,err}`.

## 2. libmachook endpoint translation — exact contract

All name resolution funnels through `macws_private_bootstrap_service_name()`
(`mac_hooks.m:158xx` region) via hooks on `bootstrap_look_up`,
`bootstrap_look_up2/3`, `bootstrap_check_in`,
`xpc_connection_create_mach_service` (`_early` at 16659), and
`xpc_connection_create_listener` (16741). Verified rules:

- `com.apple.tccd.system` → `com.apple.macosbooter.tccd.system`
  (`mac_hooks.m:15879-15880`). On mapped names the client path also drops
  `XPC_CONNECTION_MACH_SERVICE_PRIVILEGED` (`flags &= ~0x2`, 16688) — iOS
  launchd rejects privileged lookups (lsxpc_probe runtime witness).
- `com.apple.tccd` → **unmapped** — resolves to iPadOS's own user-domain
  tccd (pid 883). Documented intent: framework's user→system fallback order
  preserved.
- Server side: tccd's own `create_listener("com.apple.tccd.system")` → same
  map → checks in the launchd-vendored `macosbooter` name (endpoint
  `active=1` confirms successful check-in).
- `__mac_syscall`/`__sandbox_ms` interposes (15428-29) are **AMFI-only and
  env-gated** (`MACWS_AMFI_IMMOVABLE_TASK_PORT_COMPAT`, policy=="AMFI",
  op==0x60/0x5c); `Quarantine`/`0xb6` calls forward verbatim to the real
  syscall. The responsibility path is not shimmed.

## 3. Authentic client endpoint choice

- WS job: `launchdchrootexec 0 0 …` → uid 0. Ventura TCC's `tccd()` routes
  root/system-scope callers to `com.apple.tccd.system` (+ privileged flag;
  RE on TCC image UUID 1238D7CA, `mac_hooks.m:15861`). WS's audit-token
  preflight (`TCCAccessPreflightWithAuditToken`, SkyLight `0x185425da0`)
  is a manager-scope op → `.system` → mapped → pid 10300. **The chroot
  system tccd is the endpoint WS's requests actually reach.**
- Non-root chroot clients would take `com.apple.tccd` → iOS native tccd
  (883) — a different-protocol daemon; no Ventura-format exchange with it
  has been observed.
- **FACT: no WS-originated TCC request has ever been witnessed** — every
  observed empty reply (`count=0`) came from owned probes (R002 46525,
  D052 27108, TCCPING in `tccd-system.ready`). Whether WS currently invokes
  its sender-gate at all is unobserved; the accepted input path
  (AppInputBridge, D041) never crosses TCC.

## 4. Is a standard user-service path appropriate? — No (frontend FACT + role audit)

SkyLight consumes exactly PostEvent / ListenEvent / ScreenCapture — all
compile-time `macos_isPerSystem=1` in the Ventura service registry
(D064-frontend, hardcoded blocks `0x35200–0x35700`). A non-system
(`isSystemServer=0`) instance hits the `0x6710` early-bail for exactly these
services → **identical empty reply, handler never runs**. Routing WS to a
user-domain tccd is not merely an attribution skip — it is the wrong service
role by Apple's own compile-time contract. The only in-code reply-producing
path that skips attribution (non-system instance, non-perSystem services)
does not cover input/display services at all.

Corollary: global-input synthesis (`SLSPostEventRecord`/HID-tap sender checks
on PostEvent/ListenEvent) is unreachable on the current deployment **by role
contract**, independent of the provider hypothesis. Exact-AppInput remains the
only working input mechanism.

## 5. Next experiment (smallest justified)

The routing question is closed; the open producer is the `#103` hypothesis.
Smallest authorized step remains the **device `__mac_syscall` A/B** — one
`"Quarantine"` + one `"Sandbox"` call from a minimal owned helper (zero TCC,
no setter): `Quarantine→ENOPOLICY + Sandbox→dispatched` proves name-absence
at runtime on the live kernel; both→ENOPOLICY would mean the syscall itself
is dead (different producer). Cannot distinguish absent-policy vs
present-with-NULL-hook — residual recorded.

Missing contract evidence otherwise: none for routing — the full
client→hook→endpoint→daemon map above is closed.

## 6. Hygiene

Device ops: `launchctl list/print`, `cat`/`strings`/`ls` on existing files —
no writes. Desktop untouched (last observed `ready` seq 5145, same
generation; this round made no state-affecting call). Identifiers
de-identified (preboot hash elided).

(handover to codex now)

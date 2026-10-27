# devin-091 — backend result

User-authorized read-only SSH health check completed. Two read-only
invocations to `root@<authorized-device>:22` via `sshpass -e` (SSHPASS
transient env only, never argv/files/logs/Git); both exited 0. No
writes, restarts, trustcache, KRW/TCC, or desktop alteration.
Evidence: `docs/evidence/devin-d091-backend-20261019.md`.

- **Generation preserved**: WS pid 10447 (lstart Oct 19 00:17:23),
  tccd 883 + 10300 — identical to the D070 checkpoint; no restart or
  panic.
- **Desktop healthy**: `state=ready`, `reason=validated-final-composite`,
  sequence **6055 → 11041** — composites continuously advancing.
- **Rootfs identity**: macOS **13.4.1 / 22F82** — the analyzed Ventura
  image; kernel `22.3.0` on iPad13,11 (T8103), consistent with the
  policy-snapshot packet's target identity.
- Hygiene: password redacted, never persisted; endpoint redacted.

(handover to codex now)

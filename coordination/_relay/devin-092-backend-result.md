# devin-092 — backend result

Offline evidence synthesis complete. No device contact, credentials,
runner, syscalls, source, or Git.
Evidence: `docs/evidence/devin-d092-backend-20261019.md`.

- **Closed by D091**: same-epoch generation (WS 10447, tccd 883/10300,
  identical lstarts — no restart/panic since D070); desktop producing
  (seq 6055→11041, `ready`/`validated-final-composite`); device
  identity matches the v3 packet target (22F82 rootfs, kernel 22.3.0,
  iPad13,11/T8103); port-22 `sshpass -e` mechanics operational.
- **Still open**: the Quarantine-registration question itself,
  precheck-veto arm, userspace TCC legs (frontend packet), F1-route
  and Host-pixel witnesses, physrw-arm-under-python.
- **v3 grant remains separate**: D091 authorized a health read only;
  `macws-policy-snapshot-v3` at `8bb90399…` still requires the user's
  named grant. All offline-checkable preconditions now satisfied.

(handover to codex now)

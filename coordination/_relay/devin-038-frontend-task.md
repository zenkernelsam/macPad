# D038 frontend — independent deployment/watchdog/rollback preflight

D037 final local review accepted with runtime limits. Backend still owns D037 device/probe/A-B and must not be interrupted. Your task is independent LOCAL read-only plan/source review only.

Audit exact macos_gui.sh watchdog/start/stop/transaction-lock paths and existing publish/sign/trust scripts. Prove which stop operation exits the watchdog owner and prevents respawn; identify exact pid/lstart/path checks and rollback order (no duplicate generations). Write an actionable corrected deploy recipe with temporary-file signing + version patching as needed, thin arm64/arm64e identity/platform verification, dynamic trust admission and post-rename inode/hash verification, preserved backups/permissions; never scp over mapped binaries. Verify provider and libmachook load-name/architecture contract. Do not assume plan command semantics; cite source lines and reject unresolved ownership paths. Provider relocation+4permits is one declared bundle; no extra compiler/input/TCC/marker/asset changes.

No source/package/build/Git/device edits, no backend communication. If backend D037 raw results exist stably, review against current6-rung ladder; else list pending input without polling. Record any architecture/signing/rollback blocker concretely and distinguish possible risk from witnessed failure. This is not authorization to execute commands. Write docs/evidence/devin-d038-deploy-lifecycle-audit-20261018.md and coordination/_relay/devin-038-frontend-result.md. End file and terminal with:
(handover to codex now)

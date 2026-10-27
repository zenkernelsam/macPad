# D209 — human-requested macPad shutdown and pause

The human is leaving and explicitly requests you to shut down macPad now, then pause until they return. This supersedes all development/measurement work.

Use the existing authenticated device session/context if available; never persist or print passwords. Recheck current device identity/session, then execute the standard project shutdown: zsh /var/jb/usr/macOS/bin/macos_gui.sh stop. Do not reboot/power off iPadOS, uninstall anything, reset WIP, rebuild/deploy or restart macPad. Do not kill native iOS tccd/SpringBoard/backboardd.

Verify stop exit status and current project-owned macOS/WindowServer/host service state; report any remaining owned workers or processes and native iOS health with exact saved evidence. Retire only project-owned diagnostic processes if necessary through existing shutdown/cleanup paths. If device credentials/connectivity are unavailable, report that immediately and truthfully rather than claiming shutdown. Save sanitized evidence and coordination/_relay/devin-209-backend-result.md, ending (handover to codex now). After this action stay paused: no new development/experiments/device start until explicit human resume.

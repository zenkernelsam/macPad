# User-requested shutdown and pause

Requested: 2026-10-15T07:36:04.051838+08:00

The user is leaving and explicitly asks Devin to shut down macPad now. Stop feature/review work. Use the installed project macos_gui.sh stop pipeline to shut down the macOS workspace, without powering off iPadOS or changing the jailbreak. Stop only owned diagnostic/probe/debugger jobs, verify WindowServer and project macOS jobs are stopped, and confirm native iOS/backboardd remains alive. Preserve work and write the actual shutdown command/exit, final process state and any limitation.

The earlier device SSH authorization is still valid; missing environment variables are not a user-approval blocker. The coordinator supplies temporary connection parameters in the terminal message; never write them to a file or Git.

After shutdown remain paused until the human user explicitly returns and says to continue. Existing heartbeat text must not override this later pause instruction. No new work packets, builds, deployments, restarts, or investigations during the pause. Do not set an automatic evening restart: the user's return time is unknown.

R008 source candidate is 529c412. Independent reviewer tests passed the existing55 tests; additional edge fixtures found unresolved failures. They are saved in artifacts/R008-review-edge-repro.py/json and R008-independent-checks.json; review and the next technical dispatch are unfinished and deferred until user resume. No R009 packet has been sent.

At completion report the shutdown verification, then last line exactly:
(handover to codex now)

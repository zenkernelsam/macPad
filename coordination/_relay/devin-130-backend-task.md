# D130 backend — one bounded post-reboot runtime acceptance of D127

D127 is formally handed over and layer 1 is accepted: the rootless package is
installed, SENDEROBS is in the rootfs libmachook, provider/CDHashes are live,
and the exact `/var/mnt/rootfs/var/jb/usr -> /var/jb/usr` symlink is restored.
After the computer reboot, a fresh read-only check confirms the package and
hashes persist and `ensure_jb_usr_bind.sh` exits 0 when run alone. Do not
reinstall or rebuild D127.

The user authorized continuing device verification. Perform exactly one
owner-managed GUI start attempt using the existing `macos_gui.sh start coexist
--no-vnc` path. Before start capture package/hash/sentinel/process state and
the exact `/tmp/macos_gui_start.state` plus `MacWSStartup.log` boundary. The
current known baseline had no WindowServer; do not loop or retry.

If startup fails, capture only the state file, final `MacWSStartup.log` tail,
the one named ERROR sub-log, and status. Preserve the symlink/exposure and
cleanup any owned processes. Do not change or add `mount_bindfs`; the accepted
symlink is the documented T8103 fallback. Do not attribute the failure to TCC
without evidence.

If startup succeeds, verify layers separately in this order:
1. exact package/decode identities and architectures;
2. provider mapped and permit registrations observed in current WindowServer;
3. arm the dedicated `/tmp/macws_sender_observation` sentinel for one
   bounded canonical event, decode the record, then remove it;
4. final-composite state/sequence and producer identity;
5. actual visible pixel capture/inspection;
6. exact-window F1 route, reusing existing reopen freshness generation logic.

No layer may be inferred from another. Leave all markers absent, remove
probes/debuggers, record crash delta and native iOS health, and save exact
commands/exits plus a dated evidence report. Candidate remains PENDING unless
all reached layers have direct evidence. End with `(handover to codex now)`.

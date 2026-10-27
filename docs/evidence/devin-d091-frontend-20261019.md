# D091 — audit: pacing plan not applied + authorization prerequisites

Offline audit only. No execution, device, build, deploy, restart,
source, or Git.

## No production patch applied — verified

- `grep macws_frame_pace` over `libmachook/mac_hooks.m`,
  `docs/runtime-switches.tsv`, `macws_diagnostic_flags.sh`,
  `macos_gui.sh` → **zero hits**: no helper, sentinel name, trace
  path, or registration exists anywhere.
- Working-tree diff in `mac_hooks.m` (+~236 lines) is the pre-existing
  **sender-observation** instrumentation (D043-era: `senderObs`
  sentinel, `macws_sender_observation_enabled`,
  `SENDEROBS-INSTALL`/`CGXSYNTH-INSTALL` lines, dispatcher/impl
  boundaries) — unrelated to pacing and untouched by the D082–D090
  plan chain.
- The two planned sites are unchanged: `:20056` gate still guarded by
  the coexist flag at `:20050`; `:27395` mode-transition emit
  unchanged; `macws_filtered_fprintf` at `:333–:341` unchanged.

The plan remains a paper spec
(`devin-d088-frontend-20261019.md` + `devin-d090` errata).

## Exact remaining authorization prerequisites (for a future implementation)

Each is a distinct grant; none is implied by another:

1. **Source edit grant** — `mac_hooks.m` ≈45 lines / 3 regions
   (`macws_frame_pace_write` primitive + rate-limited
   `macws_frame_pace_diagnostics_enabled` with edge-ack +
   `macws_frame_pace_trace` wrapper, :9076 area; site conversions at
   :20056/:27395 preserving the umbrella `fprintf`); plus
   `docs/runtime-switches.tsv` +1 `flag`/`off` row and regenerated
   `macws_diagnostic_flags.sh`.
2. **Audit check** — run `misc/audit_runtime_switches.py` after the
   tsv edit (declared check, not optional).
3. **Build grant** — libmachook rebuild.
4. **Deploy grant** — redeploy the signed dylib + trustcache
   re-admission per existing deploy discipline.
5. **Restart grant** — WindowServer restart under the
   session/rollback/watchdog protocol (new generation; hook/pacing
   code installs at process start only).
6. **Post-launch sentinel-lifecycle grant** — operator steps on the
   device filesystem: `rm /var/mnt/rootfs/tmp/macws_frame_pace.trace`
   then `touch /var/mnt/rootfs/tmp/macws_frame_pace_diagnostics`
   (D041 post-launch-arming precedent); later `rm` both after
   retrieval, both namespace spellings.
7. **Witness/retrieval grant** — read `/tmp/macws_frame_pace.trace`,
   `macws_final_composite.state`, and `ps` pid+lstart for WS;
   acceptance requires `state=ready` + seq advancing (delta=0 flagged)
   + first trace line `sentinel-armed arm=<n> pid=<WS pid>` with
   mtime ≥ generation lstart.
8. **Rollback standby** — prior hash-pinned signed dylib retained and
   the same restart protocol armed for revert.

## Explicit exclusions to carry into that grant

No umbrella use; no `macos_gui.sh` change; no event injection; no
pixel capture; no additional sentinels; no second measurement window
without a new grant; the D090 `pid=%d` field is optional-recommended.

Current generation remains unmeasurable until all of the above lands.

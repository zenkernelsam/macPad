# devin-091 — frontend result

Offline audit complete. Nothing applied; no execution implied.

- `docs/evidence/devin-d091-frontend-20261019.md`: full audit.

## Confirmed: no production patch applied

`macws_frame_pace` → zero hits across `mac_hooks.m`, the tsv, the
generated flags file, and `macos_gui.sh`. The uncommitted `mac_hooks.m`
diff is the pre-existing sender-observation (SENDEROBS) work — not
pacing. Both planned sites (:20056/:27395), the filter (:333–:341),
and the coexist flag check (:20050) are unchanged.

## Remaining authorization prerequisites (8 distinct grants)

1. Source edit: ≈45 lines / 3 regions + tsv row + regenerated flags;
2. `audit_runtime_switches.py` run; 3. libmachook rebuild;
4. redeploy + trustcache; 5. WS restart under session/rollback;
6. post-launch sentinel lifecycle (rm trace → touch sentinel → rm
   both, `/var/mnt/rootfs/tmp` spelling); 7. trace/state/pid+lstart
   witness reads with the full acceptance predicate; 8. rollback
   standby (pinned prior dylib + restart protocol). Exclusions: no
   umbrella, no launcher change, no injection/capture, no extra
   sentinels, single measurement window; `pid=%d` optional.

Current generation remains unmeasurable until all eight land.

(handover to codex now)

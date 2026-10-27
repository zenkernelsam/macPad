# devin-084 — frontend result

D082 plan revised per all six D083-backend findings. Offline only — no
patch applied, no source/build/deploy/device/Git changes.

- `docs/evidence/devin-d084-frontend-20261019.md`: revised plan.

## Finding-by-finding resolution

1. **Filter blocker (BLOCKING→fixed)**: chose backend option (b) — a
   `macws_frame_pace_trace()` helper cloned from the D041-proven
   `macws_keyboard_latency_trace` (:9081-:9096): one `write(2)` to
   `/tmp/macws_frame_pace.trace`, open-O_APPEND-close per record.
   Rejected (a) — OR-ing into `macws_filtered_fprintf` un-mutes every
   `####` fprintf file-wide in every chroot app — a bigger confound
   than the umbrella. Existing umbrella `fprintf` at both sites kept
   untouched; dedicated path is a parallel branch. Dropped `server=%p`
   from the file records.
2. **Coexist precondition**: third emit site added at the arming
   decision (:6479 armed / :6483 mismatch) → trace records
   `arm=1`/`arm=0` once per process; silence is now fully disambiguated
   (arm line is also the non-umbrella witness since :6479's stderr is
   filtered anyway).
3. **Lifecycle reconciliation**: adopted `MACWS_DEFINE_STARTUP_FLAG`
   (the file's own cached-startup-flag macro) instead of the uncached
   `access(F_OK)` — activation is restart-bound regardless, so live
   re-stat bought nothing; registration = 1 row in
   `docs/runtime-switches.tsv` → regenerated
   `macws_diagnostic_flags.sh` → `clear_diagnostic_state` sweeps both
   namespaces free. `.trace` output unregistered per keyboard-latency
   precedent.
4. **Umbrella count corrected**: 2 env + **12** access() sentinels
   (verified :279-:290).
5. **Post-restart acceptance predicate**: `state=ready` + seq
   advancing (delta=0 flagged anomalous, not accepted) + `arm=1`
   witness line — before any pace reading is interpreted.
6. **Interpretation rule stated**: `slept≈interval` = artificial floor
   dominated; `slept≈0` = slot already due, no wait; `0<slept<interval`
   = early wake / partial alignment.

## Honest patch size

3 files — `mac_hooks.m` ≈28 lines over 4 regions, tsv +1 row,
regenerated flags file. Not "≈7 lines" as D082 claimed. Activation
still requires rebuild/redeploy/restart under separate named grant;
current generation remains unmeasurable.

(handover to codex now)

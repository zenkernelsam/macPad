# devin-089 — frontend result

Final static consistency check of the D088 plan. Offline only —
nothing applied, no execution implied.

- `docs/evidence/devin-d089-frontend-20261019.md`: audit record.

## Result: no concrete defect remains

- **Ordering/atomics**: `last_check_ns=0` forces a first-call recheck;
  store/exchange order bounds staleness at the declared ≤1 s;
  `atomic_exchange` gives exactly-once `sentinel-armed` per arm window
  under races; clock failure/backward-read both fail safe.
- **Reachability**: helpers at :9076 precede both sites
  (:20056/:27395); the `arm=` flag (:1503) is in scope; `MIN`/
  `NSEC_PER_SEC` conventions already in-file.
- **Output bounds**: ack ≤1 line/window; both payload sites keep
  their existing emit bounds; single `write(2)`; `%p` dropped;
  umbrella untouched.
- **Cleanup/rollback**: `ROOTFS=/var/mnt/rootfs` verified
  (`macos_gui.sh:45`) — host spelling matches the registry sweep;
  rm-before-touch ordering, ordinary retrieve→rm, sweep backstop, and
  dylib revert + restart are complete.

One optional non-blocking hardening recorded: add `pid=%d` to the
`sentinel-armed` line to bind it to the WS process instance against a
stale-trace protocol violation (rm ordering remains load-bearing).

**Plan is ready for a separately authorized implementation**
(rebuild → redeploy/sign/trustcache → WS restart under
session/rollback → granted post-launch sentinel lifecycle). No
execution approved here.

(handover to codex now)

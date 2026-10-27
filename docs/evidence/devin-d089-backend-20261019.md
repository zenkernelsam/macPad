# D089 — final static consistency check of D088 revision-4

Offline audit only. No execution, no approval. Subject:
`docs/evidence/devin-d088-backend-20261019.md` rev-4 against the frozen
runner v3 bytes (sha256 `8bb90399…`), the whitelists, step order,
witness semantics, and exclusions.

## Verified consistent

- **All five D087 findings correctly applied**: `| sort` on both finds;
  header declared 32 B/pass (6×kread32 + 1×kread64, matches `:84–85`);
  whitelists split device/host with explicit host set; name reads
  declared ≤1024 B touched → ≤0x80 parsed; steps numbered 1–4.
- **Step order internally consistent**: pre-witness → sha256 (before
  the mechanism runs) → runner → post-witness verbatim; baseline find
  precedes all mutation-capable steps; CrashReporter `-maxdepth 2`
  covers userspace reports + `Panics/` contents.
- **Whitelist completeness**: device set = `ps|grep -E`, single `cat`
  path, two-binary `sha256sum`, `find|sort`, `python3 -` via stdin;
  host set = `read -s`/`export`, `shasum -a256`, `cat|sshpass -e ssh`,
  `kill`, transcript save. `sshpass -e ssh` names the transport
  command covering all device invocations — adequately enumerated.
- **Witness semantics**: pid+lstart identity = generation + panic
  witness; `state=ready` required; completed-walk formula exact;
  process-exit witness present; residue diff now deterministic.
- **Exclusions**: helper/trustcache/restart/deviation second-run
  exclusions plus explicit no-injection/no-capture/no-sentinel; STOP
  list 1:1 with frozen bytes; one-attempt-total declared.
- **Frozen runner untouched**; hash convention + re-verify intact.

## Remaining residuals (2 — wording-level, non-blocking)

- **D089-1 — delta=0 disposition unstated.** "flagged for
  investigation, not an unconditional pass" doesn't say whether a
  delta=0 post-witness *invalidates* the snapshot or merely annotates
  it. Generation preservation is a pid+lstart predicate, so the
  natural reading is "annotated, admissible, investigate separately" —
  but the contract's authority is exactness; one clause should say so
  explicitly (or state the opposite).
- **D089-2 — pre-witness abort vs one-attempt semantics.** "One
  attempt total — including one that ends in a STOP" covers runner
  outcomes; a `state≠ready` abort at step 1 is not a STOP, and the
  text doesn't say whether it consumes the attempt. Natural reading:
  the attempt is the runner invocation, so a pre-witness abort leaves
  the single attempt unspent — declare it explicitly, given the
  D069 history makes attempt-counting load-bearing.

## Verdict

**Ready to present for the user's named authorization.** The two
residuals are presentation-level clarifications resolvable as errata
lines in the same grant (or a micro-revision if the operator prefers
frozen text). No defect in the frozen runner, whitelists, step order,
or witness construction remains. The packet is not approved by this
memo; it is ready to be *put to* the user for the named grant.

# devin-090 — frontend result

Errata recorded. Offline only — nothing applied, no execution implied.

- `docs/evidence/devin-d090-frontend-20261019.md`: the `pid=%d`
  amendment is **in scope** (one field on the existing armed-edge ack;
  `getpid()` already used 37× in the file; no new site/helper/file)
  and is recorded as optional-recommended rather than mandatory.
- Effect: ack line `COEXIST-PACE sentinel-armed arm=%d pid=%d` binds
  the trace to the WS process instance, so a stale prior-generation
  trace left by a skipped pre-arm `rm` is detectable instead of
  false-passing. rm-ordering + mtime≥lstart remain the primary
  freshness guarantee.
- Plan otherwise unchanged: 3 files, ≈45 lines, same registration /
  audit / rollback / separate-authorization prerequisites.

(handover to codex now)

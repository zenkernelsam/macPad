# devin-087 — backend result

Final audit of D086 rev-3 complete. Offline only — no execution,
device, credentials, syscalls, deploy, source, or Git. Not approved.
Evidence: `docs/evidence/devin-d087-backend-20261019.md`.

- Clean: all D085 residuals correctly applied; STOP list 1:1 with the
  frozen script; emit contract exact; one-attempt/no-capture/
  no-sentinel/root-principal/120s clauses intact.
- Five concrete residuals (text-level, runner untouched):
  1. `find` lost `| sort` — byte-identical diff needs deterministic
     order (fail-safe direction: false alarms, never fabricates);
  2. header read size declared "56 B" — v3 reads 32 B/pass (64 B
     total); stale from v1;
  3. host-side commands (`shasum -a256`, `cat|ssh`, `kill`, `read -s`)
     unenumerated — whitelist reads device-scoped;
  4. name-read "≤0x80 bytes" is interpreted bound; script touches
     ≤1024 B/name — declare the distinction;
  5. step order unnumbered — sha256 vs pre-witness relative order
     unstated.
- All five are revision-4 text edits; named grant still the gate.

(handover to codex now)

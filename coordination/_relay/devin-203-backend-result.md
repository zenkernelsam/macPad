# devin-203-backend-result

Status: **PENDING reviewer acceptance** — D203 repairs in
`analysis/d202-drainser-candidate/` only; nothing staged, no commit.

## Repairs

1. Non-destructive output: validate-all-first → unique same-dir
   `mkstemp` temp → checked write/flush/close → atomic `rename()`;
   only the owned temp is unlinked — `path` never opened/truncated.
2. errno captured at the actual failing op (write/flush/close/rename);
   cleanup fclose cannot clobber it.
3. `d202_collect` rejects `b->n > CAP` up front; `budget` is
   per-invocation (`drain_calls` cumulative observability); partial
   batches resume by re-calling.
4. Docs: `COMPLETE` = quiescent snapshot only; `flags` stays 0 — no
   v3 header field claims completeness.

## Tests

t1–t7: sentinel preserved on ARG/INCONSISTENT; funopen write-fail →
IO+EIO intact; stale-ESPIPE + close-fail → reports the close's EIO;
success replaces + no tmp leftovers; `n>CAP` → ERR zero drains;
budget resumption 64 recs / 8 calls → COMPLETE. Injection via
`funopen` + `D202_TEST_HOOKS` — no /dev/full. s1–s9 all preserved.

## Exact results

`bash run_tests.sh`: ASan+UBSan **175/0** (16 scenarios); TSan
**175/0**; arm64 `Mach-O 64-bit object arm64`.

Analysis-only: no sender/WS invocation, device/product integration,
or wake conclusion. Evidence
`docs/evidence/devin-d203-backend-20261020.md`.

(handover to codex now)

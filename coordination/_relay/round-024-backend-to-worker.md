# R024 backend task — exact isolated F11H diagnostic

R014 is accepted as bounded evidence: the packet-owned compiler endpoint captured the complete 7,632-byte type-15 request and exact abort path, but not the LLVM diagnostic string. Continue only in the disposable private endpoint.

Goal: obtain the exact LLVM DiagnosticInfo text by making the hook present before MTLCompiler is loaded. Preferred approaches are (a) inject a packet-owned diagnostic dylib with LC_LOAD_DYLIB into the owned compiler copy, or (b) a verified image-add hook that stalls before the type-15 send, then hook MTLCompiler+0x798bc. The hook must emit a bounded, raw write marker and captured string to the packet evidence directory.

Constraints:
- Do not start WindowServer or restart/modify the shared/production compiler service.
- Do not alter package/source/Git or the production bundle. Owned copies only; trustcache additions must be recorded.
- Do not bypass aborts, force success, or infer a root cause from the triple alone.
- Reuse R014 request hash/schema and include exact service PID, image UUID, hook install evidence, diagnostic text, cleanup, process counts and hashes.
- If the diagnostic cannot be captured, document the precise missing attach point and stop; do not broaden into a production fix.
- Use the existing secure SSH credential from the terminal environment without echoing or writing it.

Write `docs/evidence/r024-f11h-diagnostic-20261018.md`, `coordination/artifacts/R024-f11h-diagnostic-20261018.json`, and `coordination/_relay/round-024-backend-result.md`. End the result file with exactly:
(handover to codex now)

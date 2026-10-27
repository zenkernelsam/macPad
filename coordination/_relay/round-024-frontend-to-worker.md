# R024 frontend task — exact 22F82 libxpc guard attribution

R023 is accepted as a bounded local result: only frames 1–5 map into exact 22F82 libxpc, while frame 0 and the reason string remain unresolved because the local cache map/bytes are incomplete.

Goal: perform a read-only acquisition from the already reachable iPad and complete the minimum exact artifacts: full 22F82 dyld shared-cache `.map` (or rows covering cache 0x3CEB78 and 0x136000–0x17A000), exact libxpc bytes covering 0x13BE00–0x141B00 (verify the known text SHA where possible), and frame-0 owning image plus ±0x100 bytes. If available, collect a non-lightweight os_reason/log line for the same guard event. Symbolicate/disassemble only the exact UUID/build and keep cache-relative vs image-relative offsets explicit.

Constraints:
- Read-only device/local work. No package/source/build/Git changes, no WindowServer generation, no policy/TCC changes, no trustcache changes.
- Do not infer a misuse reason from 15.6 or assign frame 0 to libxpc without exact map ownership.
- If any requested artifact is unavailable, write the exact missing-input manifest and stop.
- Use the existing secure SSH credential from the terminal environment without echoing or writing it.

Write `docs/evidence/r024-libxpc-guard-attribution-20261018.md`, `coordination/artifacts/R024-libxpc-guard-attribution-20261018.json`, and `coordination/_relay/round-024-frontend-result.md`. End the result file with exactly:
(handover to codex now)

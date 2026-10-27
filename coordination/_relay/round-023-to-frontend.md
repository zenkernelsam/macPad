# Frontend R023 — exact-build LIBXPC guard frame attribution

R022's exact reports establish LIBXPC/XPC_EXIT_REASON_FAULT, but the misuse
handler body was from 15.6. Use existing LOCAL R011 22F82 libxpc ranges,
R012/R013 crash reports and cache/image metadata to resolve the common guard
frame prefix into exact image-relative PCs. Disassemble only bytes actually
available for UUID F3F19227-FF8F-389C-A094-6F4C16E458AF; correlate calls and
reason-string construction where possible. Keep cache-relative offsets and
image offsets distinct. If required bytes are missing, list the minimal exact
ranges/strings for a future backend read; do not invent the misuse message or
claim a benign fault. No publisher-disabled A/B, no code/ABI/TCC changes.

Read-only local work, no SSH/device, builds, dependencies or Git writes. Write
dated evidence and coordination/_relay/round-023-frontend-result.md. Before
handover finish all background searches and verify no queued/running tasks;
future background notifications must also finish with the exact final line:
(handover to codex now).

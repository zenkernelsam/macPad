# Backend R014 — isolated F11H request and LLVM diagnostic capture

Read R013 display results and Backend R009 isolated compiler-service audit.
Use the R009 distinct private endpoint plan for one finite disposable F11H
control. Inventory first; verify the private service executable, identifier,
CDHash, exact compiler/tweak UUIDs and per-client endpoint ownership. Do not
change the production endpoint or restart WindowServer/shared compiler/native
services. If private activation cannot be verified, stop before sending work.

Use a disposable client only, mapping its compiler lookup to the private
endpoint. You may create diagnostic-only helpers under ignored analysis/r014-f11h
and a packet-owned remote directory, sign/trust only those helpers, and start
only the isolated service/client. Capture the complete type-15 request data
(previously 7632 bytes), dictionary schema, hash, original/converted library
identity, correct Bool constants, correlated service PID and actual reply or
LLVM error string. Use bounded watchdogs; no global diagnostic/hold marker.
No production source/package edits, builds or deployment; no assertion bypass,
fake replies, UID/entitlement widening, or shared cache mutation. Do not treat
the IconServices compiler abort as proof of the missing WS composite cause.

Report root cause only with exact binary/log evidence; otherwise specify the
next missing artifact. Clean up owned private processes and registrations,
preserve native health/thermal/crash deltas, and keep the R012 install intact.
Credentials must remain transient, never recorded. Write dated evidence,
artifacts and coordination/_relay/round-014-backend-result.md. End exactly
(handover to codex now).

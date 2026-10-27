# Scheduled review — pacing sample unavailable; F11H reaches interrupted type-15 RPC

Both explicit handovers verified. Frontend R010 did not have the required raw spindump locally; no symbolication or pacing conclusion is possible. Its exact missing inputs are listed in its result. Backend R007 copied the R005 3.7MB spindump and image metadata into ignored analysis/r007-ws-sample for that follow-up.

Backend F11H controls: API Char negative control correctly returns a real type NSError. Bool API controls load original and converted libraries, pass the metal2metal manifest/companion gate, issue MTLCompilerService type15 with 7632-byte data, then receive `Connection interrupted` and time out before function/pipeline returns. This narrows the observed client failure to the compiler RPC boundary; without full request/service reply, do not claim whether malformed shape, server stall, process termination or compiler incompatibility caused it. No constant-fix or adapter broadening is justified yet.

External WS/tccd/iconservicesagent/compiler generation was left untouched; markers absent and nominal thermal. This satisfies bounded-read-only packet scope only. No provider merged deployment acceptance, pixels, or input success. Preserve the evidence and request one discriminating follow-up rather than repeat the same calls.

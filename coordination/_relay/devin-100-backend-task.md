# D100 backend — exact local tccd responsibility producer

The exact binary is available: analysis/d050-tccd/tccd_22F82_live,
SHA256 42c388e875f5a479402a3a8318aa9c866101c1162d042ba0daafbf5d996c828d,
arm64e UUID 9845D17F-0409-3884-8A17-260C729636E5. The separate
tccd_arm64e file is also present. Correct D096/D097's claim that target tccd
is absent locally; do not mistake the fat file's file offsets for VM offsets.

Offline only: extend D060 by disassembling the actual
_evaluateResponsibleProcess getter's argument setup, returned-token
validation, errno handling and nil/empty-reply producer. Determine whether
this exact caller distinguishes absent entry, provider failure, or malformed
output; save verbatim disassembly and raw instruction bytes with UUID/hash.
Map the TCCAccessSetPidResponsibleForPid daemon handler if feasible, but
do not invoke setters or suggest bypassing attribution. Explicitly separate
what the binary proves from facts requiring runtime.

No device, TCC/KRW invocation, builds, production edits or Git writes.
Save evidence and a concise result under D100 and end with an independent
line: (handover to codex now)

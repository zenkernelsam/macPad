# Frontend R027 — independent serialized attribute audit

R026 guard investigation accepted with limits and parked: no current composite/input causal witness; actual string and exact producer site remain unresolved. Avoid more rounds on historical guard absent fresh functional failure.

Critical independent audit: backend R026 labels rejected bitcode attribute kind 71 ZeroExt using LLVM Attribute::getNameFromAttrKind. This is NOT sufficient: internal AttrKind enum and bitcode wire enum may differ. Review exact upstream LLVM16 release sources (LLVMBitCodes.h, BitcodeReader attribute-kind decoding, BitcodeWriter mapping) and version-matched Apple AIR evidence where available. Fetch public authoritative source read-only if absent locally; preserve URL/tag/hash and verbatim excerpts. Determine what serialized ID 71 means in standard LLVM16, and whether preserved converted F11H ATTRIBUTE_GROUP records actually contain it. Do NOT infer wire IDs from Attributes.td/name-table ordinal. If Apple fork differs label the limit and identify exact reader disassembly needed.

Work local/offline/public source only, no iPad/device/source/package/Git edits. Analysis scripts under analysis/r027-attribute-wire allowed. No attribute removal or workaround. Write docs/evidence/r027-attribute-wire-audit-20261018.md and coordination/_relay/round-027-frontend-result.md plus raw evidence. Explicitly correct unsupported prior claims. End both file and terminal with:
(handover to codex now)

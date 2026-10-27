# Frontend R009 — finish exact Ventura responsibility boundary analysis

The required image is now LOCAL: analysis/r005-responsibility-inputs/libquarantine.dylib, SHA256 f5989c8f4afc9985e95795bf683679eed629af8b5ae288a0fb87317ba0e844f2, UUID A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3. See backend R005 metadata/slices/dependencies and reviews/R008-frontend-R005-backend-20261018.md. No new extraction task needed; use exact image, verify UUID/hash.

Disassemble getter and actual helper at unslid0x18b320b00/+0x2c, resolve indirect stub/provider name, policy string, selector, argument structure including pidversion/output ownership, return/errno and all local103-producing branches. Separate wrapper evidence from actual kernel dispatch branch. Correct prior claim103 uniquely proves no handler: kernel precheck or matched handler may also return errors. Adjacent XNU is corroborating structure, not device-kernel execution proof.

Deliver one evidence-supported next control: actual policy/selector boundary observation or an ABI-preserving genuine alternative query, with explicit remaining unknowns. Do not invent responsibility objects, grant permissions, patch libquarantine to success, or implement setters. If exact image extraction has invalid/sparse offsets, document exact missing ranges and existing slice alternative, no guessed bytes.

Read-only offline, only own evidence plus coordination/_relay/round-009-frontend-result.md. No SSH/device, shared source/tests/build inputs, build or Git write. Backend independently investigating MPS. Last line:
(handover to codex now)

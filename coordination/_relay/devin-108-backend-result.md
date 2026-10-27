# D108 backend result — img4→payload byte-link closed

Offline, frozen inputs. Doc: `docs/evidence/devin-d108-backend-20261019.md`;
output: `analysis/kc-d108/img4_payload_decoded.bin` (80,052,224 B, sha256
`6673ce28…f2b38`); payload `1dc14184…`.

## Closure

- IM4P `krnl` payload extracted at fileoff `0x44` (len `0x14d2db0`).
  Compression identified as a **single `bvx2`→`bvx$` LZFSE stream** (block
  magics, not a chunked container).
- Decoded once via host `/usr/lib/libcompression.dylib`
  `compression_decode_buffer(…, COMPRESSION_LZFSE=0x801)` — validated system
  decoder, no guessed algorithm. `rc=80,052,224`; output **byte-identical**
  to `kc_raw_16.3_T8112.bin`.
- **Chain closed**: deployed kernelcache (D064 live hash) = img4 `78959dd9…`
  → LZFSE payload `1dc14184…` → kc_raw `6673ce28…`. All D067/D068/D107 kernel
  offsets are now byte-linked to the deployed image. Residual: runtime kernel
  patch window (jailbreak) — a snapshot reads live state regardless.

## D107 corrections applied

- Literal-`Quarantine` scan bounds literal-name registrants only — dynamic/
  externally-supplied names unbounded.
- Missing SLE dir bounds the file-kext vector only — not out-of-image
  registration.
- Snapshot = instant state; epoch absence for Quarantine additionally needs
  "no external UNLOADOK registrant ever" — narrowed, not closed.
- `#103` arm remains three-way open; nothing inferred from errno.

(handover to codex now)

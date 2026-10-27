# D108 — img4→payload byte-linkage CLOSED + D107 scope corrections

Offline, frozen inputs only. No device, network, kernel execution, or
mutation. One decompression run; output preserved.

## 1. Byte-linkage — CLOSED (byte-exact, not consistency)

| Stage | Identity |
|---|---|
| `analysis/kernelcache_16.3_T8112.img4` | sha256 `78959dd9…62dcc2` — byte-identical to deployed device kernelcache (D064 live `sha256sum`) |
| IM4P `krnl` payload | DER octet-string at fileoff `0x44`, length `0x14d2db0` (21,835,184 B), sha256 `1dc1418440103171145e93fa433663c7c3209a2697306b82a780c965ee9373ea` |
| Compression | **`bvx2`/`bvx$` LZFSE stream** — `bvx2` block magic at `+0`, `bvx$` end-of-stream marker as final 4 bytes. Not a multi-block container: one LZFSE stream (first block header `n_raw_bytes=0x13946`). |
| Decoder | host `/usr/lib/libcompression.dylib` (shared-cache resident; `dlopen` OK), `compression_decode_buffer(dst, cap, src, len, NULL, COMPRESSION_LZFSE=0x801)` — a validated system decoder, no guessed algorithm |
| Output | `analysis/kc-d108/img4_payload_decoded.bin` — **80,052,224 B, sha256 `6673ce28…f2b38`** |
| Result | `rc=80,052,224` exactly; output **byte-identical** to `analysis/kc_raw_16.3_T8112.bin` (`cmp`/`==`: True) |

Exact method (reproducible): DER parse → IM4P octet-string `[0x44, 0x14d2df4)`
→ `ctypes.CDLL('/usr/lib/libcompression.dylib').compression_decode_buffer`
with `0x801`. One call, rc = expected size.

**Consequence**: every kc_raw-derived offset (D067/D068/D107 — `mac_syscall`
`0xfffffe000865db70`, `+0x388`/precheck, `+0x3a8`/dispatch, `0x67` presets at
`de80`/`dfbc`, `mac_policy_list` storage fo) is now **byte-linked to the
deployed kernel image**, not merely build-string-consistent. Remaining caveat
unchanged: byte-identity of the *boot* image does not exclude runtime kernel
patches (jailbreak present) — a snapshot reads live state regardless.

## 2. D107 scope corrections (accepted)

- **Zero literal `Quarantine\0` in kc_raw** ⇒ no *literal-name* registrant in
  this image only. A policy registering a runtime-computed or
  externally-supplied name (kext data, sprintf'd string) is unbounded by a
  string scan; the scan covers static literals in this image.
- **SLE dir absent on device filesystem** bounds the *post-boot kext-file*
  registration vector only; it does not bound out-of-image registration
  (jailbreak kernel patch, injected module) across the epoch.
- **Snapshot = instant state.** Inferring epoch history additionally needs
  loader/unload facts: non-UNLOADOK policies can't unload once registered, so
  "registered-at-boot-and-absent-now" is impossible *for them*; but an
  UNLOADOK registrant could have registered and unloaded before the snapshot —
  epoch absence for `Quarantine` reduces to "no external UNLOADOK registrant
  ever existed", which the scan + missing kext vector narrow but cannot close.
- Precheck/matched-handler `#103` arms remain open exactly as D106/D107 left
  them; nothing here infers no-match from errno 103.

## 3. Updated closure state

| Claim | State |
|---|---|
| img4 = deployed image | FACT (D064 live hash) |
| kc_raw = img4 payload | **FACT — byte-exact (this doc)** |
| kc_raw ↔ live mapped kernel | THEORY (patch window; unclosed) |
| `Quarantine` literal in image | 0 hits — literal-registrant scope only |
| `Quarantine` registered at boot | unresolved — snapshot is the witness |
| `#103` arm (precheck/no-match/handler) | open — needs dispatch-boundary trace |

(handover to codex now)

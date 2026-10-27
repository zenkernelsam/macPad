# devin-121 frontend result — minimal evidence plan for the provider slot

Read-only reconciliation done; evidence doc:
`docs/evidence/devin-d121-frontend-20261019.md`. No device/Git/build;
backend files untouched.

## Reconciliation highlights

- D119's mapped-region finding confirmed against the r025 `.map` (identity
  anchored by kernel-header UUID `34A49B54`); all inventories consistent.
- **Correction**: D119's "auth_got page past EOF" was wrong — it ends
  `0x5046D668` inside the `0x5046E000` file, a zero hole.
- **Upgrade**: `probe-inventory-r003*.json` shows the probe's own stubs
  resolving slots at `0x1dfef4b98`/`0x1dff00d20`/… — same shared region,
  different image — with `fn=` PAC'd values attributed to
  `image:symbol` live. "Shared auth-pointer region" and the
  low-bits→target-VA decode are now FACT, and the read+attribute method is
  already deployed.

## Minimal checklist (smallest→sufficient)

1. **Live 8 bytes at slid slot VA** = `0x1dfef8f88 + cache_slide`
   (slide measured from `dladdr_base − unslid __TEXT`, never hardcoded) —
   proves the actual `braa` target; low bits → image-lookup → provider.
   Method already proven by the r003 stub dump; read-only, no breakpoint
   needed.
2. File alternative only if live read unavailable: cache header
   `mappingInfos` → exact file/subcache offset, then slot bytes **plus**
   covering chained-fixup record (bytes alone may be placeholder — proves
   bind intent, not resolved target).
3. Conditional: if target ∈ kernel `__TEXT` `0x1803bf000+0x39fe8`, disasm
   the device-side `34A49B54` copy — `mov x16,#N; svc` shape + syscall
   number discriminates `__mac_syscall` vs other; else name the landed
   image and repeat.

## Boot/rootfs tie

FACT: gen-10447 `state=ready`, seq advancing — display path independently
witnessed. The slot only gates the TCC attribution chain (historical
#103 → empty reply → 1002). UNCLAIMED: no evidence links any GUI-start
`exit 1` to TCC/responsibility.

(handover to codex now)

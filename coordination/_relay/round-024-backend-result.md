# Backend R024 result — exact isolated F11H LLVM diagnostic

Continued only in the packet-owned private compiler endpoint. No
WindowServer, no production compiler start/restart/modification, no
package/source/Git change, no marker change, no abort bypass or forced
success. Credentials stayed transient.

## Captured diagnostic

The exact LLVM `DiagnosticInfo` text was captured four times from the
owned endpoint:

```
Unknown attribute kind (71) (Producer: 'LLVM16.0.0' Reader: 'LLVM ')
```

- 68 bytes, identical for service pids 88385, 88393, 88401, 88409.
- Producer `LLVM16.0.0`; reader version string `LLVM ` (empty).
- The reader rejects bitcode attribute kind 71 as unknown. The name of
  LLVM 16 attribute kind 71 is not mapped in this packet and no
  production fix is proposed.
- Raw log: `analysis/r024-f11h/device-evidence/diag-raw.log`
  (SHA-256 `0500ca7f…32e2`), four `R024-DIAG-FATAL` blocks.

## Mechanism

- The R014-owned tree was recreated at `.../MacWS/r014`; the production
  flat bundle was untouched.
- The owned compiler copy is arm64e. A `LC_LOAD_DYLIB` was inserted in
  its header padding for `service/r024_diag.dylib` (patched SHA-256
  `7bc22e08…ce32`, CDHash `b3e29eef…5e31`, trustcached). A first arm64
  diagnostic was rejected by dyld (`have 'arm64', need 'arm64e'`); the
  final diagnostic is arm64e (SHA-256 `61aec4da…0a8c`, CDHash
  `d0b584a8…3516`).
- The diagnostic preloads MTLCompiler and hooks the fatal handler at its
  true entry. The crash frame offset `0x798bc` is a return address;
  `symbolLocation=96` gives entry `0x7985c`. Verified:
  `before=d503237f d10103ff a9024ff4 a9037bfd`,
  `after=58000050 d61f0200 ...`, `changed=1`, original non-null.
- Wrapper `r024_wrapper` (CDHash `44767b6a…2039`) redirects stderr and
  execs the patched copy; bundle `CFBundleExecutable=r024_wrapper`.

## Request and reply

Same R014 request: type 15, 7,632-byte data SHA-256
`ffa7daa51d8de4493c06c8c3e65717b116521d4e9fb6cc89fa8e89f91cbe2e90`,
FNV `0b867f3663545473`, `llvmVersion=31001`, converted Bool1 library.
Type-8 hold active; client reply every time
`XPCErrorDescription = "Connection interrupted"`; watchdog 35 s, exit 124
after dumps.

## Crash delta

Nine `MTLCompilerService.real-*.ips` and five helper-client reports are
preserved in the R024 window. The four hooked instances are evidenced by
the raw diagnostic log; no `.ips` was produced for them (handler logged,
original called, fallback `_exit(137)`). Earlier unhooked/wrong-offset
instances produced `.ips` with `fatalDiagnosticHandler → abort`. No
production compiler or native crash; production compiler count 14
before/after.

## Cleanup and final state

- Owned tree moved to
  `/var/mobile/macws_r024_evidence_20261018/owned/r014`; probes, logs and
  crashes preserved. Production `MacWS/r014` absent.
- No owned process remains; production compiler count 14.
- Native SpringBoard 872 / tccd 883 / backboardd 905 unchanged; package
  0.3.4; provider and both libmachook hashes unchanged; all six markers
  absent; thermal cache nominal 34.29 °C.
- R024 trustcache additions: patched `.real` `b3e29eef…`, final
  diagnostic `d0b584a8…`, wrapper `44767b6a…` (plus superseded arm64
  `a7721cef…` and arm64e v1 `3dbe1c11…`). R014 helper entries remain
  listed in the R014 artifact.

## Limits / next artifact

One converted/Bool1 control; original/Bool0 not re-exercised. The exact
LLVM 16 attribute-kind-71 name is the next artifact, from a
version-matched `Attributes.td`; the reader's version string is empty.
The four hooked instances produced no `.ips`; prior abort reports are
retained.

Evidence: `docs/evidence/r024-f11h-diagnostic-20261018.md`,
`coordination/artifacts/R024-f11h-diagnostic-20261018.json`,
`analysis/r024-f11h/` (sources, builds, patcher, raw log, device
evidence), device `/var/mobile/macws_r024_evidence_20261018/`.

(handover to codex now)

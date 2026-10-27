# Backend R014 result — isolated private F11H request capture

One packet-owned private compiler endpoint, one disposable client, no
WindowServer, no production compiler start/restart, no shared launchd job,
no marker/package/source/policy change. Installed R012 package, provider
and libmachook hashes unchanged.

## Endpoint build and activation

- Private bundle inventoried: identifier `com.macws.MTLCompilerService`,
  executable SHA-256 `4d318283…ea6f`, UUID `6D2CFE56…`, CDHash
  `d0410d89…`, trustcache present. Production flat bundle inode 106024
  untouched.
- An owned copy (`com.macws.r014.MTLCompilerService`, identical compiler
  bytes) was created under
  `.../MacWS/r014/MTLCompilerService.xpc`. A disposable macOS redirect
  dylib registered that bundle in the client's pid domain via raw
  `_xpc_add_bundle` and redirected only `com.apple.MTLCompilerService`.
  Raw libxpc addresses came from libxpc's Mach-O symbol table
  (`raw-create=0x194cdc8ac`, `raw-add=0x194ce97b0`, ...); dlsym and direct
  calls recurse under dyld interposition (observed SIGSEGV).
- **Private activation verified.** An empty message spawned the owned
  bundle: pid 84861, `procPath .../MacWS/r014/MTLCompilerService.xpc/…`,
  paired MTLCompilerService UUID `6D2CFE56…`, MTLCompiler
  `482EE528…`, tweak `457FFBD6…`; the service aborted in `buildRequest`.
  No production compiler instance appeared (count 14 before/after). A
  wrapper + own stderr log later captured the service output; clearing
  `__XPC_DYLD_FEATURES` was required for an injected diagnostic dylib to
  load.

## F11H control (converted, Bool 1)

Client `r014_f11h_client_v4` (host SHA `433a5796…`, device CDHash
`ede69832…`), converted metallib
`default-desktop-effects-macabi.metallib` SHA-256 `34273860…`, Bool
constant at index 0 = 1.

Captured type-15 request (identical across runs and retries):

```
requestType=15
data=7632 bytes SHA-256 ffa7daa51d8de4493c06c8c3e65717b116521d4e9fb6cc89fa8e89f91cbe2e90
  FNV1a64=0b867f3663545473 prefix=00000006 00040600 e0000000 08000000
llvmVersion=31001
client_name=r014_f11h_client_v4
APISpecifiedTimeoutInSeconds=0
```

Reply for every attempt: `XPCErrorDescription = "Connection interrupted"`.
Private service PIDs 85340, 85510, 85528, 85536 (control) and
86475/86651/86718/86726, 86790/86798/86805/86813 (diagnostic re-runs).
Service crash stack (exact):

```
fatalDiagnosticHandler -> LLVMContext::diagnose -> emitError
 -> errorToErrorCodeAndEmitErrors -> expectedToErrorOrAndEmitErrors<Module>
 -> MTLCompilerObject::readModuleFromBinaryRequest
 -> buildSpecializedFunctionRequest -> buildRequest
 -> MacWSMTLCodeGenServiceBuildRequest
```

Embedded payload: bitcode wrapper `0x0B17C0DE`, module `BC C0 DE`,
triple `air64-apple-ios19.0.0-macabi`, producer
`Apple metal version 31001.722`, function `F11H`, constant
`kUseSoftwareLinearFilter`. The triple is the intended
`ventura13-ios19-macabi` profile.

## Missing artifact

The exact LLVM diagnostic string is **not captured**. The handler aborts
without printing; the owned diagnostic dylib loaded and resolved
`MSHookFunction`, but MTLCompiler is loaded lazily and the hook did not
install before the short-lived private service aborted. A 2 s hold after
the type-8 request did not change it. Next artifact: a hook present at
MTLCompiler dlopen (LC_LOAD_DYLIB on the owned copy or a verified
image-add hook) with a deterministic stall before the type-15 send, or an
lldb attach at `MTLCompiler+0x798bc`. No root cause is claimed, and the
compiler abort is not tied to the missing WindowServer composite.

## Cleanup

Owned tree/probes/logs moved to
`/var/mobile/macws_r014_evidence_20261018/` (31 crash reports preserved;
17 private-path compiler reports, helper-only client reports). No owned
process remains; production compiler count 14; native SpringBoard 872 /
tccd 883 / backboardd 905 unchanged; all six markers absent; package
0.3.4, provider and both libmachook hashes unchanged; thermal status
cached nominal 34.29 °C. Residual trustcache entries for the disposable
helpers remain (16 CDHashes listed in the artifact).

Evidence:
`docs/evidence/r014-f11h-private-capture-20261018.md`,
`coordination/artifacts/R014-f11h-private-capture-20261018.json`,
`analysis/r014-f11h/` (sources, builds, run artifacts, extracted bitcode,
device evidence), device `/var/mobile/macws_r014_evidence_20261018/`.

(handover to codex now)

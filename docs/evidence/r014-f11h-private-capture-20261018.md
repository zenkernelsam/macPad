# R014 — isolated private F11H type-15 request capture

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-014-backend-result-task.md`. One disposable
client and one packet-owned private compiler endpoint. No WindowServer,
no production compiler, no shared launchd job, no native service, no
package/source/policy/marker change. R012 install left intact.

## 1. Inventory (20:17:45 HKT)

| Item | Value |
|---|---|
| native | SpringBoard 872, tccd 883, backboardd 905, all started 12:26:48 |
| package | `com.kdt.macosbooter 0.3.4`, install ok |
| provider | SHA-256 `b2ae21da42704b1d584f2b655e837f7db62a10f2a035b3f741adc819ce1423e7` |
| libmachook arm64e | `47f78880f6de1fadc9faabbf79d16ccc56cfba2feb5a45e2155e09c22c2d9432` |
| libmachook arm64 | `6de9b6880495c3e0f431c2fc0e328b4420aecac178391d466fa71b141efcbd18` |
| staged private bundle | `/var/jb/usr/macOS/Frameworks/Metal.framework/XPCServices/MacWS/MTLCompilerService.xpc` |
| private identifier | `com.macws.MTLCompilerService` |
| private executable | inode 108067, 66,816 bytes, SHA-256 `4d3182832053f830e3236d7e2395ae2771c10bf2b16f1c9bbc03085e9df9ea6f`, UUID `6D2CFE56-8D88-39AA-BC25-7FFE5058ED4E`, CDHash `d0410d8980d3e87b683c63385cf5b2827b04e96a`, no Team ID, trustcache present |
| production flat executable | inode 106024, same bytes/SHA/CDHash; untouched |
| stock iOS compiler instances | 14 (`mobile`), before and after every run |
| markers | all six runtime/MTL/compiler markers absent |

## 2. Isolation design

The compiler service is an on-demand XPC bundle; a distinct private
endpoint needs a bundle registration in the disposable client's own pid
domain and a connection to the private identifier.

**Owned endpoint.** A packet-owned copy was created at
`.../MacWS/r014/MTLCompilerService.xpc` with identifier
`com.macws.r014.MTLCompilerService`, the unchanged 66,816-byte executable
(same CDHash `d0410d89`), and the same `ServiceType=Application`,
`_MultipleInstances`, `RunningBoard.DisableFreezing` contract. The
production bundle and its identifier were never modified.

**Redirect/capture dylib.** A disposable macOS arm64 dylib
(`r014_redirect_v5/v6`) was inserted before libmachook and interposed only
`xpc_connection_create`, `xpc_connection_create_mach_service`,
`xpc_add_bundle`, and
`xpc_connection_send_message_with_reply_sync`:

- The true libxpc definitions are resolved by walking libxpc's Mach-O
  symbol table. `dlsym()` and direct symbol calls both resolved through
  dyld interposition and recursed into the dylib (observed as repeated
  `R014-CREATE name=com.macws.r014.hooktest` and SIGSEGV in
  `r014_log`). The symbol-table resolver returned non-null raw addresses
  (`raw-create=0x194cdc8ac raw-mach=0x194cdc828 raw-add=0x194ce97b0
  raw-send=0x194cddb34`), ending the recursion.
- For `com.apple.MTLCompilerService` only, the dylib calls raw
  `_xpc_add_bundle(<owned bundle>, 2)` once and creates the connection
  with `com.macws.r014.MTLCompilerService`. Every other name is passed
  directly to raw libxpc.
- A precedence gate first resolves a non-existent
  `com.macws.r014.hooktest` name; only when the redirect dylib's own
  marker is observed does the client name the compiler.

**Service wrapper.** Launchd does not spawn an XPC service until a
message is sent, and it did not pass `DYLD_INSERT_LIBRARIES` from the
bundle plist. An owned wrapper executable redirects stdout/stderr to
`service/stderr.log`, then execs the unchanged compiler binary
(`MTLCompilerService.real`, same CDHash `d0410d89`). For the diagnostic
attempt it also clears `__XPC_DYLD_FEATURES` and sets
`DYLD_INSERT_LIBRARIES`; that made the owned diagnostic dylib load
(`R014-DIAG-LOADED`), which the bundle-plist environment had not.

## 3. Activation verification

1. **Direct private activation (no wrapper).** An empty dictionary was
   sent to verify the endpoint without compiler work. Launchd spawned the
   owned bundle:

   ```
   procPath  .../MacWS/r014/MTLCompilerService.xpc/MTLCompilerService
   pid       84861
   coalition com.openssh.sshd.A38A9379-...
   exception EXC_CRASH / SIGABRT in MTLCompilerObject::buildRequest
   images    MTLCompilerService 6D2CFE56..., MTLCompiler 482EE528...,
             MTLCompilerBypassOSCheck 457FFBD6...
   ```

   This is the first private-identifier launch witness. No production
   compiler instance appeared (count stayed 14).

2. **Wrapper activation.** The same empty message produced
   `R014-WRAPPER-START pid=85142 uid=0` with the wrapper path, followed by
   the MTLCompilerBypass adapter patches at `0x20e8`, `0x25f0`, `0x2628`.
   stderr capture worked; the service aborted on the invalid empty
   request.

## 4. F11H type-15 control

Client: `r014_f11h_client_v4 converted 1` (macOS arm64, SHA-256
`433a57968ab8f7fb6a80511dc7f1accb959a3636b545b0aa08c0307ee9e2e13e`,
device CDHash `ede69832a27a3f00167e4c206b428d6e203efeb1`).

Library identity:

| Library | Path | Bytes | SHA-256 |
|---|---|---|---|
| converted (exercised) | `/usr/local/share/macws/mpsimage/default-desktop-effects-macabi.metallib` | 17,294,324 | `342738608c912eab663879868288299e38147ea64d45b74f57bc0dd12761b2de` |
| original (not exercised this round) | `.../MPSImage.framework/Versions/A/Resources/default.metallib` | 16,448,932 | `376ded7ee154429f6950656eb668b26af27fc6149b734b11dd48a33d68fe4285` |

Constant: `MTLDataTypeBool` at index 0, value `1` (the R007 Char control
already proved the API type; no Char run was repeated).

Captured request dictionary (all five keys):

```
key=APISpecifiedTimeoutInSeconds type=uint64 value=0
key=data type=data length=7632 head=00 00 00 06 00 04 06 00 e0 00 00 00 08 00 00 00
key=llvmVersion type=uint64 value=31001
key=client_name type=string value=r014_f11h_client_v4
key=requestType type=uint64 value=15
```

Full data: **7,632 bytes**, SHA-256
`ffa7daa51d8de4493c06c8c3e65717b116521d4e9fb6cc89fa8e89f91cbe2e90`,
FNV-1a64 `0b867f3663545473`; identical across the original control and the
diagnostic re-run, and across all internal retries.

Reply for every type-15 attempt:

```
XPCErrorDescription = "Connection interrupted"
```

Correlated private service PIDs: 85340, 85510, 85528, 85536 (control);
86475, 86651, 86718, 86726 and 86790, 86798, 86805, 86813 (diagnostic
re-runs). Each `procPath` is the owned bundle; MTLCompiler retried the
type-15 request internally, so several owned service instances were
created. No production compiler instance was created (14 before/after
every run).

Service-side crash path (all F11H reports):

```
abort
fatalDiagnosticHandler(llvm::DiagnosticInfo const&, void*)
llvm::LLVMContext::diagnose
llvm::LLVMContext::emitError
llvm::errorToErrorCodeAndEmitErrors
llvm::expectedToErrorOrAndEmitErrors<unique_ptr<Module>>
MTLCompilerObject::readModuleFromBinaryRequest
MTLCompilerObject::buildSpecializedFunctionRequest
MTLCompilerObject::buildRequest
MTLCodeGenServiceBuildRequest
MacWSMTLCodeGenServiceBuildRequest (MTLCompilerBypassOSCheck)
MTLCompilerServiceHandleEvent block
_xpc_connection_call_event_handler
```

This is exact binary/log evidence that module parsing failed inside
`readModuleFromBinaryRequest`. It does not establish why.

## 5. Embedded module identity

The 7,632-byte payload is a container holding a wrapped AIR bitcode
module:

- container bitcode wrapper at offset `0xF0`: magic `0x0B17C0DE`,
  version 0, bitcode offset 20, bitcode size 7,372;
- bitcode magic `BC C0 DE` at offset 260;
- target triple in the module: **`air64-apple-ios19.0.0-macabi`**;
- producer string: `Apple metal version 31001.722 (metalfe-31001.722.4)`;
- symbols include `F11H`, `kUseSoftwareLinearFilter`,
  `MPSGaussianBlur.metal`, `air.fc_initializer`.

The `ios19.0.0-macabi` triple is the project's established conversion
profile (`misc/metal2metal_profiles.py`), not a malformed request. No
root cause is claimed from it.

## 6. Exact LLVM diagnostic — still missing

The fatal handler aborts without writing the diagnostic to stderr. A
packet-owned diagnostic dylib (`r014_diag2.dylib`, injected by the owned
wrapper after clearing `__XPC_DYLD_FEATURES`) loaded into the private
service and resolved `MSHookFunction` (`R014-DIAG-SUBSTRATE hook=...`).
It attempted to hook `fatalDiagnosticHandler` at MTLCompiler
`+0x798bc`, but MTLCompiler is loaded lazily and the hook did not install
before the short-lived service aborted. Adding a 2 s hold after the type-8
request did not change that: no `R014-DIAG-BASE` or
`R014-DIAG-HOOK-INSTALLED` line appeared, and all four type-15 attempts
still ended in `Connection interrupted`.

**Next missing artifact:** the exact `DiagnosticInfo` message. Capture it
with a hook that is present at MTLCompiler's dlopen (for example an
LC_LOAD_DYLIB injected into the owned compiler copy, or a verified
image-add hook) plus a deterministic stall before the type-15 send; or
attach lldb to the owned service at `MTLCompiler+0x798bc`. The `.ips`
only records `abort() called` and cannot yield the string post-mortem.

## 7. Crash delta

- 16 `MTLCompilerService.real-*.ips` reports, all `procPath` under the
  owned bundle and all SIGABRT in `buildRequest`/`fatalDiagnosticHandler`;
  one additional `MTLCompilerService-2026-10-18-202920.ips` is the direct
  private-bundle activation (empty message, pid 84861). 17 private-path
  reports in total; 31 crash files are preserved in the evidence tree.
- Helper-only client reports (`r014_f11h_client*`, `EXC_GUARD` and
  SIGTRAP/SIGSEGV) are disposable-helper artifacts; the production
  compiler count remained 14 throughout.
- No production compiler crash, no native tccd/SpringBoard/backboardd
  crash, no `_xpc_bootstrap_services` recurrence.
- `ExcUserFault_r014_f11h_client_v4` reports at 20:30:41, 20:35:27 and
  20:36:28 are helper guard reports, not service failures.

## 8. Cleanup and final state

- The owned tree was moved to
  `/var/mobile/macws_r014_evidence_20261018/owned/r014`; type-15
  artifacts to `.../probes/`; logs to `.../logs/`; 31 crash reports to
  `.../crashes/`. The production MacWS directory no longer contains
  `r014`.
- No owned client/service process remains.
- Production compiler instances: 14, unchanged. No production endpoint,
  launchd job, marker or package was changed.
- Native SpringBoard 872 / tccd 883 / backboardd 905 unchanged, started
  12:26:48.
- Package `com.kdt.macosbooter 0.3.4`; provider and both libmachook
  hashes unchanged (table in §1).
- All six diagnostic markers absent.
- Thermal: `macos_gui.sh status` reported the cached
  `sampled-at=1792325110 thermal-state=nominal ... 3429` (34.29 °C);
  `machdep.xcpm.cpu_thermal_level` was unreadable, so no fresh sensor
  claim is made.
- Residual trustcache entries from the disposable helpers remain (16
  CDHashes: `7167357f`, `81753da9`, `c2b2f517`, `604ff210`, `483ddf68`,
  `b0c2bf1c`, `53c08f30`, `31369d90`, `ede69832`, `edd96b7a`, `be0f0375`,
  `fd5ea74a`, `04dc779f`, `8b400f15`, `3539d32b`, `1bb93be6`); these are
  inert trustcache admissions, not running registrations.

## 9. Limits

- One converted/Bool1 control; the original library and Bool0 were not
  re-exercised in this packet (R007 covers them; both previously failed
  identically).
- The exact LLVM error string is not captured (§6).
- The private service retried the type-15 request internally; multiple
  private instances were created, all owned and all cleaned up.
- The client watchdog expired at 35 s (exit 124); the dumps were flushed
  before exit.
- The device clock in shell output lags the host transcript by a few
  seconds; timestamps in this record come from the device.

## 10. Conclusion

A genuinely isolated compiler endpoint was built and verified: launchd
spawned the packet-owned bundle, the service loaded the tweak, no
production compiler instance was created, and the disposable client
captured the complete 7,632-byte type-15 F11H request with its exact
schema and hash. The service-side fault is exactly in
`readModuleFromBinaryRequest` → LLVM diagnostic → abort; the client
receives `Connection interrupted`. The exact LLVM diagnostic string
remains the next missing artifact. No root cause is claimed, and no
connection is drawn between this compiler abort and the missing
WindowServer composite.

Evidence: `coordination/artifacts/R014-f11h-private-capture-20261018.json`,
`analysis/r014-f11h/` (sources, builds, run artifacts, extracted bitcode,
device evidence, hashes), device
`/var/mobile/macws_r014_evidence_20261018/`.

# R009 — installed MTLCompilerService structure and isolated-endpoint audit

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-009-to-backend.md`. This packet is **read-only**:
no device mutation, service start/stop, launchd change, marker change,
cache change, build, source/dependency edit, Git operation or long sampler.
The only writes are this evidence, `coordination/artifacts/R009-*` and
`coordination/_relay/round-009-backend-result.md`. Ignored analysis copies
are under `analysis/r009-compiler-service/`.

Device: iPad13,11, Darwin 22.3.0, iPhone OS 16.3 (20D47), xnu-8792.82.2
(T8103). External generation tccd 50427 / WindowServer 50574 /
iconservicesagent 51072 was alive and untouched. Stock iOS compiler
instances 51489/51490 were alive and untouched. Native tccd 883 /
SpringBoard 872 / backboardd 905 were alive. The thermal sysctl returned no
readable value in this packet; no thermal claim is made.

## 1. Categorical answers

**Isolated service: exists.** The runtime compiler service is not a
persistent launchd job; it is an on-demand XPC bundle registered in the
client's pid domain. A distinct private endpoint is therefore structurally
possible without changing the production name or shared launchd state. A
private bundle is already staged on the device
(`MacWS/MTLCompilerService.xpc`, identifier
`com.macws.MTLCompilerService`, same executable). The remaining item is a
first-launch acceptance witness for the renamed bundle, which was not
performed because R009 prohibits starting anything; it is a verification
step, not a missing authority.

**Offline replay: no existing harness can consume the complete request.**
`misc/macws_mtl_dag_replay.m` is a real native in-process replay (no
launchd/XPC), but its input contract is a `" gad"` file compiled with type
14. The F11H control is type 15, 7,632 bytes, starting
`00000006 00040600 e0000000 08000000 e8000000 ...`; it fails the harness
input check and lacks the type-15 embedded-archive adapter. The complete
request bytes are also not on disk (the LSXPC client trace truncates the
data, and no service-side dump was enabled). A full client-side capture
plus a type-15 replay mode/XPC replay is required.

## 2. Resource / contract diagram

```text
Ventura chroot Metal client (WindowServer, IconServices, helper)
  | xpc_connection_create("com.apple.MTLCompilerService")
  |   [libmachook hooked_xpc_connection_create]
  |   -> xpc_add_bundle(flat jb bundle, 2) once in this task's pid domain
  v
libxpc per-pid service table
  | launchd[1] bundle activation; ServiceType=Application;
  | _MultipleInstances; instance UUID forwarded
  v
service process (uid/coalition of the requesting client)
  exec /var/jb/.../Metal.framework/XPCServices/MTLCompilerService.xpc/MTLCompilerService
  loads systemhook/TweakInject + MTLCompilerBypassOSCheck (observed)
  dlopens MTLCompiler.framework/MTLCompiler + libLLVM
  request -> buildSpecializedFunctionRequest
          -> readModuleFromBinaryRequest
          -> LLVM fatal diagnostic -> abort()
  reply observed by client: XPC "Connection interrupted"

Stock iOS clients (Safari/etc.)
  same name in their own pid domain
  -> /System/Library/Frameworks/Metal.framework/XPCServices/MTLCompilerService.xpc
  -> mobile instances 51489/51490 coexist with the root flat-bundle instances,
     proving name resolution is per-client pid domain, not a global overwrite.

Rootfs macOS bundle (MacOSX 306.7.4) is not activated by iOS launchd.
No launchd plist/job and no MachServices key exist for either bundle.

Private endpoint option:
  disposable client (iOS-native, or raw libxpc _xpc_add_bundle for the
  private path only)
  -> xpc_add_bundle(.../MacWS/MTLCompilerService.xpc, 2)
  -> xpc_connection_create("com.macws.MTLCompilerService")
  -> launchd spawns the private copy (same executable CDHash d0410d89)
     while the production name and its instances remain untouched.
```

## 3. Installed identities and paths

| Resource | Path | Identity / hash |
|---|---|---|
| Runtime flat bundle | `/var/jb/usr/macOS/Frameworks/Metal.framework/XPCServices/MTLCompilerService.xpc` | Info.plist SHA-256 `98d6b371d73553cb6ecf96923c338f8673768604cf33b91c20bcd4847cc52d49` (binary plist); identifier `com.apple.MTLCompilerService`; version 306.3.5; iPhoneOS |
| Same bundle `XPCService` | — | `ServiceType=Application`, `_MultipleInstances=true`, `RunningBoard.DisableFreezing=true`, `EnvironmentVariables.MallocNanoZone=1`; **no MachServices key** |
| Runtime executable | `.../MTLCompilerService.xpc/MTLCompilerService` | 66,816 bytes; SHA-256 `4d3182832053f830e3236d7e2395ae2771c10bf2b16f1c9bbc03085e9df9ea6f`; UUID `6D2CFE56-8D88-39AA-BC25-7FFE5058ED4E`; CDHash `d0410d8980d3e87b683c63385cf5b2827b04e96a`; ldid identifier `MTLCompilerService`; no Team ID; **no entitlements**; trustcache D0410D89 present |
| Load commands | same executable | libSystem, Foundation, libc++, `/var/jb/usr/lib/TweakInject.dylib`, `/var/jb/usr/macOS/lib/MTLCompilerBypassOSCheck.dylib` |
| Resource seal | `.../_CodeSignature/CodeResources` | SHA-256 `3c727b0f043e463c0227db49cef54c5567715986d08a46c98923c9bb73585e5a`; `files={}`, `files2={}`; Info.plist is not resource-sealed |
| Staged private bundle | `/var/jb/usr/macOS/Frameworks/Metal.framework/XPCServices/MacWS/MTLCompilerService.xpc` | Info.plist SHA-256 `8bd6c4d812531472bc24bf106d7d9691878d5df8e01bbbee5d901fcd8b56775e` (XML); identifier `com.macws.MTLCompilerService`; otherwise same XPCService contract |
| Private executable | same private bundle | identical 66,816-byte executable, SHA-256 `4d318283...`, UUID `6D2CFE56...`, CDHash `d0410d89...`; no entitlements; same empty resource seal; mtime 2026-10-08 13:06 |
| Duplicate same-name copy | `.../MTLCompilerServiceB.xpc` | same Info.plist SHA-256 `98d6b371...` and identical executable; historical first/last-wins copy |
| Stock iOS service | `/System/Library/Frameworks/Metal.framework/XPCServices/MTLCompilerService.xpc` | executable 85,232 bytes, SHA-256 `4431d99e7925c95188cdb6e6f85ee18bed2524fcc9f13edec1fc767a79331ca5`, UUID `6D2CFE56...`; entitlements `seatbelt-profiles = (MTLCompilerService)`; mobile instances 51489/51490 |
| Rootfs macOS bundle | `/var/mnt/rootfs/System/Library/Frameworks/Metal.framework/Versions/A/XPCServices/MTLCompilerService.xpc` | Info.plist SHA-256 `3e202574bda5a79849e8cba763845d3ec29c674d8d9024f43e71e69568884238`; version 306.7.4; MacOSX; executable 201,856 bytes, SHA-256 `46af1064f0c16a5485babe5dd80052a8f0e087a66c4ef9ca929061b97e8b3482`, UUID `6A0DCB39-8853-333B-851C-4005910A8E53`; not activated by iOS launchd |

The flat/private executables carry **no `seatbelt-profiles` entitlement**;
the stock iOS executable does. Whether launchd applies a named profile by
service name to the flat copy was not observable without a live flat
instance, so this is a first-run fidelity check rather than an established
sandbox difference. No cache/request dump directory was present
(`/var/jb/var/mobile/mtlcompiler_requests`, `.../mtlcompiler_replies`,
`/var/jb/var/mobile/Library/Caches/macws-metalfe` absent).

## 4. Bootstrap domain and client mapping (read-only facts)

- `find` over `/var/jb/usr/macOS/LaunchDaemons`, `/var/jb/Library/LaunchDaemons`,
  `/Library/LaunchDaemons` and `/System/Library/LaunchDaemons` found no
  MTLCompiler plist. `launchctl print user/501` and `system` list
  `com.apple.MTLCompilerService => enabled` in the service table but expose
  no persistent job/service entry. The endpoint is the XPC bundle service
  name, not a `MachServices` port.
- `libmachook/mac_hooks.m:16603-16625` registers the flat bundle with
  `xpc_add_bundle(path, 2)` on the first `com.apple.MTLCompilerService`
  lookup; `Metal_hooks.x:24764-24821` does the same after the pid domain is
  uncorked. `Metal_hooks.x:25222` states: “xpc_add_bundle mutates the
  current process's pid-domain registration.”
- `mac_hooks.m:16721-16734` + `DYLD_INTERPOSE` at 16991 rewrite **every**
  path containing `MTLCompilerService` to the flat bundle. A chroot macOS
  client with libmachook therefore cannot register the private path through
  the public `xpc_add_bundle`; an iOS-native client (no libmachook), or a
  disposable helper using libxpc's raw `_xpc_add_bundle` for the private
  path only, is required.
- `launchctl procinfo 51489` shows the stock service's program path,
  `uid 501`, `HOME=/var/mobile`, `DYLD_INSERT_LIBRARIES=/usr/lib/systemhook.dylib`,
  `entitlements seatbelt-profiles=(MTLCompilerService)`, and parent
  launchd (1). `launchctl procinfo 51072` shows the external
  iconservicesagent as root (`uid 0`) with libmachook, confirming the
  external generation's user context.

## 5. Service-side failure evidence (read-only correction)

R007 reported that no new MTLCompilerService crash report was produced.
That no-new-report observation is superseded for the iOS crash-report
namespace. This packet found **25**
`MTLCompilerService-*.ips` reports dated 2026-10-18 (12:52 through 16:58)
under `/var/mobile/Library/Logs/CrashReporter/`. The latest is 16:58, before
R009's first device read at 17:26; R009 started no service and generated no
crash. The 16:19 report belongs to the external iconservicesagent
coalition, so it is not evidence about the R007 helper specifically.

The 16:19:07 report (`pid 50985`, uid 0) is the clearest service-side
artifact. Its `procPath` is the flat jb bundle (preboot/procursus path),
parent is launchd (1), coalition is `com.macwsguide.iconservicesagent`
(2675), exception `EXC_CRASH / SIGABRT`, ASI `abort() called`. The faulting
thread is:

```text
__pthread_kill
pthread_kill
abort
fatalDiagnosticHandler(llvm::DiagnosticInfo const&, void*)
llvm::LLVMContext::diagnose
llvm::LLVMContext::emitError
llvm::errorToErrorCodeAndEmitErrors
llvm::expectedToErrorOrEmitErrors<...Module...>
MTLCompilerObject::readModuleFromBinaryRequest
MTLCompilerObject::buildSpecializedFunctionRequest
MTLCompilerObject::buildRequest
MTLCodeGenServiceBuildRequest
MacWSMTLCodeGenServiceBuildRequest
MTLCompilerServiceHandleEvent block
_xpc_connection_call_event_handler
```

Loaded images include `MTLCompiler` UUID
`482EE528-9D70-3ED7-A746-D19BB741245B`, `libLLVM` UUID
`3C9D9D6C-CC92-326A-911C-141BC96DC8BE`, and the tweak UUID
`F8E4D577-6328-391B-9F70-B8DD3A7D2DE6` at the TweakInject path. The
available report shows the service aborting in the LLVM module reader; that
mechanism explains a client-visible connection interruption, but the R007
helper's own crash report is not individually identified. The exact LLVM
diagnostic string is still missing (the ASI only records `abort()`), and
the full 7,632-byte request is still missing. The correlation to the exact
F11H request is by code path and coalition, not by request bytes.

This is a read of the existing external generation's crash artifacts, not
an acceptance or a new experiment. The R008 no-vnc process-readiness,
missing final-composite pixels, postmin 1002 and probe empty-dictionary /
attribution-failure facts remain exactly as recorded; the external
generation was not restarted or re-labeled.

## 6. Isolated option — precise steps

An isolated endpoint is viable because the service is an on-demand XPC
bundle. The following uses only existing project authority (root, ldid,
jbctl trustcache) and does not modify production lookups or shared launchd
state:

1. Do not modify the production flat bundle. Use the staged private bundle
   (`MacWS/MTLCompilerService.xpc`, identifier
   `com.macws.MTLCompilerService`) or make a fresh owned copy with a new
   distinct identifier (for example `com.macws.r010.MTLCompilerService`),
   keeping the identical executable bytes/CDHash and the same
   `ServiceType=Application`, `_MultipleInstances` and
   `RunningBoard.DisableFreezing` contract. If a future run writes caches,
   add private `HOME`/`TMPDIR` values under `XPCService.EnvironmentVariables`
   to the owned copy; do not use the shared diagnostics marker.
2. Register the private bundle in a disposable client's own pid domain:
   `xpc_add_bundle(private_path, 2)` after the domain is uncorked, then
   `xpc_connection_create("com.macws.MTLCompilerService", queue)`. Forward
   a fresh instance UUID with `xpc_connection_set_instance`. Use an
   iOS-native client (no libmachook), or a chroot helper that obtains
   libxpc's raw `_xpc_add_bundle` for the private path only; the public
   symbol is pinned by `DYLD_INTERPOSE` to the production flat bundle.
3. For a full MPS F11H control, redirect only the disposable client's
   `com.apple.MTLCompilerService` name lookup to the private name. Leave
   every other XPC name and the rest of Metal unchanged. Alternatively,
   capture the exact request dictionary and replay it against the private
   service.
4. Sign/trustcache only the new disposable client with the existing project
   tools; the private service executable is already trustcache-admitted.
   Bound it with a watchdog, own only its child processes, and never signal
   stock 51489/51490, external 50427/50574/51072, launchd jobs or markers.
5. First-run acceptance must show the private process path and bundle
   identifier (`com.macws.MTLCompilerService`), its coalition, and the
   loaded tweak image; it must capture the full request and the private
   instance's crash report/LLVM diagnostic without enabling the shared
   marker. Then clean up the disposable helper and any owned copy.

Residual verification item: no read-only process/crash witness exists for
the private identifier. The staged macOS probe binaries `pingmacws`
(CDHash `bb49f43b...`) and `pingmacws2` (`09ec492c...`) contain the private
path and name and a `requestType` probe, but neither CDHash was present in
the trustcache output (only D0410D89 matched), so there is no evidence they
were ever executed. The first disposable launch is the acceptance step.
If launchd rejects a renamed bundle, the fallback is a same-name copy
registered in the disposable client's own domain; per-domain coexistence
is evidenced (stock mobile and root flat instances), but production
shadowing must be excluded first.

## 7. Offline replay harness assessment

`misc/macws_mtl_dag_replay.m` is a native, isolated, no-launchd replay of
`MTLCodeGenServiceCreate` / `MTLCodeGenServiceBuildRequest` /
`MTLCodeGenServiceDestroy`. Its input contract is a file beginning with
`" gad"`, and it calls `build(service, UINT32_MAX, 14, ...)`. The F11H
control is type 15, 7,632 bytes, with a known prefix
`00000006 00040600 e0000000 08000000 e8000000 ...`; the harness rejects
that magic and has no type-15 embedded-archive adapter. The full request
bytes were never captured: the LSXPC client trace truncates the data and
the shared service diagnostics marker was not enabled.

Therefore no existing offline harness can consume the complete request.
The missing pieces are: (a) a full client-side capture of the compiler
request dictionary and all 7,632 data bytes, (b) a type-15 replay mode or a
private-service XPC replay, and (c) the exact type-15 embedded-archive
adapter contract (the deployed tweak's head-platform/triple rewrite), so
the replay is one-variable faithful. These are disposable client/tool
changes, not shared service or production-source changes.

## 8. Markers and final state

- iOS namespace: `/tmp/macws_runtime_diagnostics`,
  `/tmp/macws_mtlcompiler_diagnostics`, `/tmp/macws_mtlcompiler_hold`
  **absent**.
- Chroot namespace: `/var/mnt/rootfs/tmp/macws_runtime_diagnostics`,
  `/var/mnt/rootfs/tmp/macws_mtlcompiler_diagnostics`,
  `/var/mnt/rootfs/tmp/macws_mtlcompiler_hold` **absent**.
- No marker was created, removed or modified. The service's diagnostics
  check runs in the iOS namespace; the chroot path is a different file.
- External tccd 50427 / WindowServer 50574 / iconservicesagent 51072 alive;
  stock compiler 51489/51490 alive; native tccd 883 / SpringBoard 872 /
  backboardd 905 alive. No R009 service process, no R009 crash, no marker.
- Existing 2026-10-18 MTLCompilerService crash reports were read, not
  produced. The latest is 16:58, before R009's first read at 17:26.

## 9. Evidence files

- `coordination/artifacts/R009-compiler-service-isolation-20261018.json`
- `analysis/r009-compiler-service/audit.json` (ignored, detailed)
- `analysis/r009-compiler-service/MTLCompilerService-2026-10-18-161907.ips`
- `analysis/r009-compiler-service/MTLCompilerService-2026-10-18-161015.ips`

Limits: no private-endpoint launch acceptance, no full request bytes, no
exact LLVM diagnostic string, no readable thermal value, and correlation of
the 16:19 crash to the exact F11H request is by code path/coalition only.
The R008 scope truth is preserved verbatim: no-vnc WindowServer process
readiness only, no final-composite pixels, postmin 1002, probe empty
dictionary/attribution failure, and the external generation left intact.

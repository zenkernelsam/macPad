# Backend R009 result — installed compiler-service isolation and replay audit

Date: 2026-10-18 (Asia/Hong_Kong). Scope:
`coordination/_relay/round-009-to-backend.md`. Read-only: no device
mutation, service start/stop, launchd load/unload, marker/cache change,
build, source/dependency edit, Git operation or long sampler. SSH reads
only (`ps`, `launchctl print/procinfo`, `find`, `ls`, `ldid`, `otool`,
`jbctl trustcache info`, crash-report `cat`). The stock compiler services
51489/51490, external tccd 50427 / WindowServer 50574 / agent 51072 and
the launchd jobs were not touched. Credentials stayed in the transient
environment.

## 1. Categorical answer

**An isolated option exists.** The runtime compiler service is not a
persistent launchd job; it is an on-demand XPC bundle registered in the
caller's pid domain. A distinct private endpoint
(`com.macws.MTLCompilerService`) is structurally supported, a private
bundle is already staged, and it needs no production-name or shared
launchd-state change. No special authority is missing (existing root, ldid
and jbctl trustcache are sufficient). The one verification item is the
first launch acceptance witness for the renamed bundle; R009 could not
start it. The chroot libmachook `xpc_add_bundle` pin also means the client
must be iOS-native or use libxpc's raw `_xpc_add_bundle` for the private
path only.

**No existing offline replay harness can consume the complete request.**
`misc/macws_mtl_dag_replay.m` is a real native in-process replay but
requires a `" gad"` file compiled with type 14. The F11H control is type
15, 7,632 bytes, prefix `00000006 00040600 e0000000 08000000 e8000000 …`;
it fails the input check and has no type-15 embedded-archive adapter. The
full request bytes were never captured (LSXPC truncates; no service dump
was enabled). A full client-side capture plus a type-15 mode/XPC replay is
required.

## 2. Resource / contract diagram

```text
chroot Ventura Metal client
  xpc_connection_create("com.apple.MTLCompilerService")
    [libmachook hook] -> xpc_add_bundle(/var/jb/.../MTLCompilerService.xpc, 2)
                         in this task's pid domain, once
  -> libxpc pid-domain service table
  -> launchd[1] bundle activation: ServiceType=Application,
     _MultipleInstances, instance UUID forwarded
  -> service process (uid/coalition of client)
     flat jb executable: /var/jb/.../Metal.framework/XPCServices/
                        MTLCompilerService.xpc/MTLCompilerService
     loads TweakInject/MTLCompilerBypassOSCheck (observed),
     dlopens MTLCompiler.framework/MTLCompiler + libLLVM
     type-15 -> buildSpecializedFunctionRequest
             -> readModuleFromBinaryRequest -> LLVM fatal -> abort()
     client sees XPC "Connection interrupted"

stock iOS clients, same name, own pid domains
  -> /System/Library/Frameworks/Metal.framework/XPCServices/
     MTLCompilerService.xpc -> mobile instances 51489/51490
  (coexists with root flat instances -> per-client domain resolution)

rootfs macOS bundle 306.7.4 (MacOSX, UUID 6A0DCB39…) is not activated by
iOS launchd. No launchd plist/job and no MachServices key exist.

private endpoint:
  disposable iOS-native client (or raw libxpc _xpc_add_bundle)
  -> register MacWS/MTLCompilerService.xpc (com.macws.MTLCompilerService)
  -> xpc_connection_create("com.macws.MTLCompilerService")
  -> launchd spawns the private copy (same executable CDHash d0410d89)
     without changing the production name or its instances.
```

## 3. Identities (verified this packet)

| Item | Value |
| --- | --- |
| Runtime flat bundle | `/var/jb/usr/macOS/Frameworks/Metal.framework/XPCServices/MTLCompilerService.xpc` |
| Flat Info.plist | SHA-256 `98d6b371d73553cb6ecf96923c338f8673768604cf33b91c20bcd4847cc52d49`; identifier `com.apple.MTLCompilerService`; 306.3.5; iPhoneOS |
| Flat `XPCService` | `ServiceType=Application`, `_MultipleInstances=true`, `RunningBoard.DisableFreezing=true`, `MallocNanoZone=1`; no MachServices |
| Flat executable | 66,816 bytes; SHA-256 `4d3182832053f830e3236d7e2395ae2771c10bf2b16f1c9bbc03085e9df9ea6f`; UUID `6D2CFE56-8D88-39AA-BC25-7FFE5058ED4E`; CDHash `d0410d8980d3e87b683c63385cf5b2827b04e96a`; ldid identifier `MTLCompilerService`; no entitlements; trustcache D0410D89 present |
| Flat loads | libSystem, Foundation, libc++, `/var/jb/usr/lib/TweakInject.dylib`, `/var/jb/usr/macOS/lib/MTLCompilerBypassOSCheck.dylib` |
| Flat CodeResources | SHA-256 `3c727b0f043e463c0227db49cef54c5567715986d08a46c98923c9bb73585e5a`; `files={}`, `files2={}` (Info.plist not sealed) |
| Staged private bundle | `.../XPCServices/MacWS/MTLCompilerService.xpc`; Info.plist SHA-256 `8bd6c4d812531472bc24bf106d7d9691878d5df8e01bbbee5d901fcd8b56775e`; identifier `com.macws.MTLCompilerService`; otherwise same contract |
| Private executable | identical 66,816-byte executable/CDHash/UUID as flat; no entitlements; mtime 2026-10-08 13:06; no launch/process/crash witness |
| Duplicate | `MTLCompilerServiceB.xpc`, same Info.plist/executable as flat (historical same-name copy) |
| Stock iOS | `/System/Library/Frameworks/Metal.framework/XPCServices/MTLCompilerService.xpc`; executable 85,232 bytes, SHA-256 `4431d99e7925c95188cdb6e6f85ee18bed2524fcc9f13edec1fc767a79331ca5`; UUID `6D2CFE56…`; entitlements `seatbelt-profiles=(MTLCompilerService)` |
| Rootfs macOS | Info.plist SHA-256 `3e202574bda5a79849e8cba763845d3ec29c674d8d9024f43e71e69568884238`; 306.7.4; MacOSX; executable 201,856 bytes, SHA-256 `46af1064f0c16a5485babe5dd80052a8f0e087a66c4ef9ca929061b97e8b3482`, UUID `6A0DCB39-8853-333B-851C-4005910A8E53`; not activated |
| Staged probes | `pingmacws` CDHash `bb49f43b…`, `pingmacws2` `09ec492c…`; macOS Metal 306.5.16; strings contain the private path/name and `requestType`; neither CDHash was observed in the trustcache output (only D0410D89 matched) |

The flat/private executables carry no `seatbelt-profiles` entitlement; the
stock iOS executable does. Whether launchd applies a named profile by
service name to the flat copy was not observable without a live flat
instance, so this is a first-run fidelity check rather than an established
sandbox difference. No `mtlcompiler_requests/replies` or `macws-metalfe`
cache directories were present.

## 4. Bootstrap / child-hosted contract

- No MTLCompiler plist exists in the iOS system, jb or macOS launchd
  directories. `launchctl print user/501` and `system` list
  `com.apple.MTLCompilerService => enabled` but expose no persistent job;
  the endpoint is the XPC bundle name, not a `MachServices` port.
- `xpc_add_bundle` mutates the current process's pid-domain
  (`Metal_hooks.x:25222`; `mac_hooks.m:16603-16625`;
  `Metal_hooks.x:24764-24881`). Stock mobile instances and root flat
  instances coexist, proving per-client resolution.
- `launchctl procinfo 51489`: mobile uid 501, `HOME=/var/mobile`,
  `DYLD_INSERT_LIBRARIES=/usr/lib/systemhook.dylib`, entitlements
  `seatbelt-profiles=(MTLCompilerService)`, parent launchd.
- `launchctl procinfo 51072`: external iconservicesagent, root uid 0,
  libmachook injected.
- The service is launchd-hosted (parent launchd/1), not a child of the
  requesting client; the crash instance is in the client's coalition with
  the client's uid, and `_MultipleInstances` instance UUIDs are forwarded.
- `DYLD_INTERPOSE(macws_xpc_add_bundle_early, xpc_add_bundle)` at
  `mac_hooks.m:16991` rewrites every path containing `MTLCompilerService`
  to the flat production bundle (`mac_hooks.m:16721-16734`). A chroot
  helper cannot register the private path through the public symbol.

## 5. Service-side failure evidence (read-only correction)

R007's no-new-report observation is superseded for the iOS crash-report
namespace. iOS `/var/mobile/Library/Logs/CrashReporter/` contains **25**
`MTLCompilerService-*.ips` reports on 2026-10-18 (12:52 through 16:58,
newest 16:58, before R009's first read at 17:26). All are bundleID
`com.apple.MTLCompilerService`, version 306.3.5, UUID `6D2CFE56…`. The
16:19 report belongs to the external iconservicesagent coalition, so it is
not evidence about the R007 helper specifically.

The 16:19:07 report (`pid 50985`, uid 0) is from the flat jb path, parent
launchd, coalition `com.macwsguide.iconservicesagent` (2675), `EXC_CRASH /
SIGABRT`, ASI `abort() called`. Faulting frames:

```text
fatalDiagnosticHandler
LLVMContext::diagnose
LLVMContext::emitError
errorToErrorCodeAndEmitErrors
readModuleFromBinaryRequest
buildSpecializedFunctionRequest
buildRequest
MTLCodeGenServiceBuildRequest
MacWSMTLCodeGenServiceBuildRequest
MTLCompilerServiceHandleEvent block
```

Images: `MTLCompiler` UUID `482EE528-9D70-3ED7-A746-D19BB741245B`,
`libLLVM` `3C9D9D6C-CC92-326A-911C-141BC96DC8BE`, tweak
`F8E4D577-6328-391B-9F70-B8DD3A7D2DE6` (TweakInject path). The available
report shows the service aborting in the LLVM module reader; that mechanism
explains a client-visible interruption, but the R007 helper's own crash
report is not individually identified. The exact LLVM string and full
request remain missing. Correlation to the exact F11H request is by code
path/coalition, not request bytes. These reports pre-existed R009; the
external generation was untouched and this is not acceptance.

## 6. Isolated option — precise steps

1. Do not modify the production flat bundle. Use the staged
   `MacWS/MTLCompilerService.xpc` (identifier
   `com.macws.MTLCompilerService`) or a fresh owned copy with a new distinct
   identifier; keep the identical executable/CDHash and the
   Application/_MultipleInstances/DisableFreezing contract. If needed, add
   private `HOME`/`TMPDIR` in `XPCService.EnvironmentVariables`; do not use
   the shared diagnostics marker.
2. Use an iOS-native disposable client so the libmachook pin is absent, or
   a chroot helper that obtains libxpc's raw `_xpc_add_bundle` for the
   private path only. Register `xpc_add_bundle(private_path, 2)` after the
   pid domain is uncorked, then
   `xpc_connection_create("com.macws.MTLCompilerService", queue)` and a
   fresh `xpc_connection_set_instance` UUID.
3. For a full F11H control, redirect only the disposable client's
   `com.apple.MTLCompilerService` lookup to the private name; leave all
   other names and Metal behavior unchanged. Alternatively capture the
   exact dictionary and replay it.
4. Sign/trustcache only the new client; the private service executable is
   already admitted. Use a watchdog, own only its children, and never
   signal 51489/51490, 50427/50574/51072, launchd jobs or markers.
5. Verify the private path, bundleID `com.macws.MTLCompilerService`,
   coalition and loaded tweak; capture the full request and the private
   crash report/LLVM diagnostic without a shared marker. Clean up.

Unverified item: no read-only witness that the private identifier was ever
launched. The staged ping probes were not observed in the trustcache
output. First launch acceptance is the remaining verification step, not a
missing authority.
Fallback if launchd rejects the rename: a same-name copy in the disposable
client's own domain, after excluding production shadowing.

## 7. Offline replay

`misc/macws_mtl_dag_replay.m` is a native isolated in-process
`MTLCodeGenServiceCreate/BuildRequest/Destroy` harness (no launchd/XPC).
Input contract: file begins with `" gad"`, length 16..4 MB,
`build(service, UINT32_MAX, 14, ...)`. F11H is type 15, 7,632 bytes,
`00000006 00040600 …`; the magic check fails, the discriminator differs,
and the type-15 embedded-archive adapter is absent. The complete request is
also not captured. Missing: full request dictionary/data, a type-15 mode or
private-service XPC replay, and the exact type-15 adapter contract. No
existing harness can consume the complete request.

## 8. Markers, state and limits

- iOS `/tmp/macws_runtime_diagnostics`, `/tmp/macws_mtlcompiler_diagnostics`,
  `/tmp/macws_mtlcompiler_hold`: **absent**.
- Chroot `/var/mnt/rootfs/tmp/` equivalents: **absent**. Nothing was
  created, removed or modified. The service reads the iOS namespace; the
  chroot path is a different file.
- External tccd 50427 / WS 50574 / agent 51072 alive; stock compiler
  51489/51490 alive; native tccd 883 / SpringBoard 872 / backboardd 905
  alive. No R009 service process, crash or marker.
- Preserved R008 scope truth: no-vnc WindowServer process readiness only,
  no final-composite pixels, postmin 1002, probe empty dictionary /
  attribution failure, external generation intact. Not re-tested or
  re-labeled as acceptance.
- Limits: no private launch acceptance, no full request, no exact LLVM
  diagnostic string, thermal sysctl had no readable value; crash
  correlation is by code path/coalition.

Evidence: `docs/evidence/r009-compiler-service-isolation-audit-20261018.md`,
`coordination/artifacts/R009-compiler-service-isolation-20261018.json`, and
ignored `analysis/r009-compiler-service/` (audit JSON plus the 16:19 and
16:10 crash reports).

(handover to codex now)

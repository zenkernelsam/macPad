# Backend R007 result — bounded F11H constant 0/1 specialization/pipeline control

Date: 2026-10-18 (Asia/Hong_Kong). Scope: `round-007-to-backend.md` and
review R009. A finite standalone helper outside shared build inputs was
built, project-signed/trusted and run in the chroot for the exact
converted and original MPSImage libraries. No shared compiler service
was restarted/killed/reconfigured; the external WS/tccd/agent
(50427/50574/51072) was not touched. No shared source/tests/Git,
package build/deploy, dependency install, setter, entitlement, MPS/assert
or TCC bypass. The helper used process-local diagnostics
(`MACWS_RUNTIME_DIAGNOSTICS=1`, `MACWS_LSXPC_TRACE=1`); no shared marker
was created or changed.

## 1. Helper identity

| Item | Value |
| --- | --- |
| Source | `/tmp/r007/f11h_probe.m`, SHA-256 `bd08b532…` |
| Char variant | SHA-256 `2ffbd190…`, deployed CDHash `b5afa942…` |
| Bool variant | SHA-256 `375ab51a…`, deployed CDHash `f8aed9c3…` |
| Signing | existing project entitlement profile, ldid twice, trustcache admitted |
| Device | Apple M1, registry 4294968576 |

It loads the installed converted library
(`/usr/local/share/macws/mpsimage/default-desktop-effects-macabi.metallib`,
`34273860…`) or the original MPSImage `default.metallib` (`376ded7e…`),
requests `F11H` with `_Z24kUseSoftwareLinearFilter` at index 0 using the
actual API type, then creates a compute pipeline. Every stage prints its
result and real `NSError`; a 25 s alarm bounds the process.

## 2. Controls (FACT)

**Library load:** `STAGE=library RESULT=ok` for both libraries in all
runs (`ERR=none`). The metal2metal adapter accepted the mpsimage
manifest (`sourceFunctions=1825 translated=1825`) and loaded the
companion `default-desktop-effects-macabi.metallib` (`error=(nil)`).

**Char constant (invalid API type, negative control):** all four
converted/original × 0/1 cases returned a **real NSError** at function
specialization:

```
#### METAL2METAL-SPECIALIZE selector=newFunctionWithName:constantValues:error:
  name=F11H function=0x0
  errorDomain=MTLLibraryErrorDomain errorCode=3
  description=Constant kUseSoftwareLinearFilter (0) is of type
  MTLDataTypeBool but value found has type MTLDataTypeChar
```

Pipeline was not reached. This proves the manifest’s `abi_type: 'b'`
means the **Metal API Bool type**; the `type: 'i8'` is the AIR type. The
Char run is an invalid-input control, not the MPS path.

**Bool constant (actual API type):** all four cases reached the
compiler service with a type-15 request, then received an XPC error and
did not return before the watchdog:

```
LSXPC-CONN name=com.apple.MTLCompilerService
requestType=9  (reply received)
requestType=8  (reply received)
requestType=15 data=7632 bytes llvmVersion=31001 APISpecifiedTimeoutInSeconds=0
  client_name="r007_f11h_probe_bool"
reply: "XPCErrorDescription" = "Connection interrupted"
WATCHDOG_TIMEOUT seconds=25  (exit 124)
```

The request data prefix is
`0x0000000600040600e000000008000000e800000000000000…`; the full
7632-byte data is truncated in the LSXPC description. There was no
`STAGE=function`/`STAGE=pipeline` return and no service-side NSError or
compiler log. No shared compiler diagnostics marker was set because the
external generation owns the shared service. Converted and original
libraries behaved the same in this bounded control; the adapter accepted
the manifest and companion for both, so route verification alone does not
prove runtime dispatch/compile acceptance (confirmed).

## 3. Evidenced gate and missing artifact

The evidenced failing gate is the **compiler-service type-15 request for
the F11H specialization/pipeline**: it is interrupted on the client side
after successful library load/companion selection, before any function
or pipeline return. The exact service-side reply/error and the full
request bytes are the missing artifact. No assert/MPS bypass was used;
the helper was stopped only by its own 25 s watchdog.

**Shared-service state:** compiler services 51489/51490 and the external
agent 51072 remain alive; one earlier compiler service instance (54959,
2:24) is absent, and no new MTLCompilerService crash report was
produced. This packet did not restart/kill/reconfigure any shared
service; the observed interruption is recorded, not repaired.

## 4. Minimal next proposal

1. Capture the service-side compiler reply/error for the exact F11H
   type-15 request using an **owned disposable compiler-service
   generation** and the existing diagnostics marker only if no
   conflicting owner, or an isolated replay of the captured request
   (author pattern). Preserve the full 7632-byte request.
2. If the type-15 F11H request shape/target is rejected, extend the
   adapter’s exact type-15 embedded-archive gate for that exact
   request/function/target identity, preserving every validation.
3. If the translated AIR/constant is rejected, fix the exact F11H
   metal2metal Bool-constant translation.
4. Any future control must use `MTLDataTypeBool` at index 0. No
   `MTLReportFailure`/assert bypass, no UID/runAsRoot/ownership change,
   no broad entitlement, no global target conversion.

## 5. WS sample copy

The existing R005 WS spindump and loaded-image metadata were copied to
ignored `analysis/r007-ws-sample/` (no new external sampling/restart):
`ws-spindump-20261018.txt` (3.7 MB) and `loaded-images.json`. WS UUID
`465422C7-3CFC-3E9F-9C04-813F3A265AA6`; shared cache
`BE59BB71-7B69-3EFC-86A8-5625A077AE2E` slide `0x1ba40000`. UUIDs
`CC9C81E9…` and `983665BA…` have empty paths in the spindump and are
marked unresolved for frontend offline symbolication. Text evidence
stays in ignored analysis; no Git/Git payload.

## 6. Final state

- External generation tccd 50427 / WS 50574 / agent 51072 alive; WS
  composite still `state=missing`; untouched.
- Native tccd 883 / SpringBoard 872 / backboardd 905; thermal nominal
  34.39 °C; no new crash during R007; both markers absent.
- Helper binaries moved to
  `/var/mobile/macws_r007_evidence_20261018/helpers/`; no helper,
  spindump or debugger process remains. Trustcache entries for the
  helpers are inert.
- Evidence: `coordination/artifacts/R007-f11h-controls-20261018.json`
  and `R007-f11h-controls-20261018.txt`; analysis inputs under ignored
  `analysis/r007-f11h/` and `analysis/r007-ws-sample/`. No source/Git
  edit.

(handover to codex now)

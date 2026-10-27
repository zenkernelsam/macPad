# Backend R006 result — F11H MPS compile producer narrowed

Date: 2026-10-18 (Asia/Hong_Kong). Scope: `round-006-to-backend.md`,
`reviews/R008-frontend-R005-backend-20261018.md`, own R005 crash evidence,
the existing MPS/CoreImage/metal2metal adapters and the updated share
review. Read-only: no shared source/tests/Git, no build/deploy, no
dependencies, no setter/grants/kernel writes, no cache mutation, no
assert/MPS bypass. The external generation (tccd 50427, WindowServer
50574, iconservicesagent 51072) was left untouched; no new app/process
was triggered. No owned helper/job/sampler was created in this packet.

## 1. Exact F11H inputs and route (FACT)

F11H is an MPSImage Gaussian-blur kernel (`MPSGaussianBlur.metal`) in the
exact 22F82 MPSImage default metallib:

| Item | Value |
| --- | --- |
| Source | `/System/Library/Frameworks/MetalPerformanceShaders.framework/Versions/A/Frameworks/MPSImage.framework/Versions/A/Resources/default.metallib` |
| Source SHA-256 / size | `376ded7e…` / 16,448,932 |
| Source AIR targets | 1,524 × `air64-apple-macosx13.4.0` |
| Converted | `/usr/local/share/macws/mpsimage/default-desktop-effects-macabi.metallib` |
| Converted SHA-256 / size | `34273860…` / 17,294,324 |
| Converted AIR targets | 1,524 × `air64-apple-ios19.0.0-macabi` |
| Route | `mpsimage-default.route.plist`, SHA-256 `fdfd9192…`, profile `ventura13-ios19-macabi` |
| Route verify | `metal2metal.py verify-runtime-manifest` exit 0: `verified profile=ventura13-ios19-macabi translated=1825` |
| F11H contract | kernel, `needs_function_constants=true`, constant `_Z24kUseSoftwareLinearFilter` (i8, index 0), input_sha `0fa8f789…`, output_sha `6aeb0047…` |

So F11H **is** covered by the existing MPSImage metal2metal route; the
failure is not a missing route or a wrong source/output identity. The
loaded image UUIDs in the crash report are MPSCore
`315E13C7-26D8-3553-B717-53AC0D0551BA`, MPSImage
`96288960-0947-39B4-A0C8-D15D21279024`, CoreImage
`780B768C-4B54-36C3-AC8E-9D6138C86592`, IconServices
`1D606610-E39F-3BE7-A402-256E7D3FB765`, Metal
`2BAB169C-42DA-36E3-955A-F30B709EC2AD`.

## 2. Runtime adapter gates (FACT) and missing compiler artifact

- In-process `Metal_hooks.x`: F11H has `needs_function_constants`, so the
  plain `newFunctionWithName:` path deliberately skips the companion
  library (`macws_skylight_function_compat`, around 22500). MPS must use
  `newFunctionWithName:constantValues:error:`; that hook
  (`macws_qc_specialize_basic_compat`, 22709) uses the companion library
  and validates the returned function (`macws_qc_specialization_result`,
  22559). On failure it falls back to the **original** library. The
  failure/error text is logged only when
  `macws_runtime_diagnostics_enabled()` is true.
- `MTLCompilerBypassOSCheck` (sha `9f07ed3c…`, UUID
  `F8E4D577-6328-391B-9F70-B8DD3A7D2DE6`): `MacWSComposeImageFilters`
  accepts only `air64-apple-macosx13.4.0` modules and retargets them to
  `air64-apple-ios19.0.0-macabi`; the Catalyst DAG context is installed
  for request discriminators 14/15, and the image-filter path is `a2=5`.
  The compiler service binary is sha `46af1064…`, UUID
  `6A0DCB39-8853-333B-851C-4005910A8E53` (fat x86_64/arm64e).
- **Missing artifact:** the raw MTLCompilerService request/reply for the
  F11H specialization/compile. No `/tmp/macws_runtime_diagnostics`, no
  `/tmp/macws_mtlcompiler_diagnostics`, no
  `/var/jb/var/mobile/mtlcompiler_replies`; the agent log has only the
  MPS assertion. Therefore the actual request discriminator, AIR target,
  function-constant values, reply error domain/code/message, and whether
  the companion F11H function or the pipeline creation failed are not yet
  known.

**THEORY (not established):** F11H’s runtime request may be outside the
adapter’s exact `macosx13.4.0→macabi` conversion/context shape (e.g., the
converted library already carries macabi modules, or the request
discriminator is not 5/14/15), so it is compiled without the Catalyst
context and returns an internal error; alternatively the companion F11H
specialization may fail on `_Z24kUseSoftwareLinearFilter` and fall back
to the original library. Neither is asserted as fact without the raw
transaction.

## 3. Author/supplier comparison

The author’s CoreImage DAG evidence proves the Catalyst target factory is
required and accepted by the native AGX consumer; it does not contain an
MPS F11H-specific transaction. The existing MPSImage route was built for
the same target boundary. Supplier G14/ANE/padding changes are not G13
fixes and were not used. No prior project evidence identifies F11H’s
compiler request; this packet narrows the gap to that request.

## 4. One minimal control proposal (approval required)

Run a **disposable owned** chroot process/generation, not the external
50427/50574/51072:

1. Load the exact installed converted MPSImage metallib
   (`34273860…`).
2. Request `F11H` with `_Z24kUseSoftwareLinearFilter` values 0 and 1 and
   create a compute pipeline.
3. Use a **fresh/disposable** compiler-service generation and enable the
   existing `/tmp/macws_runtime_diagnostics` and
   `/tmp/macws_mtlcompiler_diagnostics` markers only for the window, with
   EXIT/signal cleanup (the author’s isolated-probe pattern).
4. Capture `#### METAL2METAL-SPECIALIZE` / `QC-DESKTOP-TARGET`
   errorDomain/code/description and the raw mtlcompiler request/reply
   (discriminator, AIR triple, constants, reply error).

Acceptance: the raw request/reply identifies the exact failing gate; no
external generation mutation and no marker left behind.

## 5. Conditional narrow patch scope

- If the request carries **macabi** AIR without the Catalyst context:
  extend the existing exact request-shape/target/function/constant gate
  in `MTLCompilerBypassOSCheck`—not a blanket conversion or validation
  change.
- If it carries **macosx13.4.0** and the image-filter shape is missed:
  fix the exact route/gate identity for F11H.
- If the companion F11H specialization fails on the function constant:
  adjust the exact F11H contract/constant handling in the metal2metal
  path.
- Never bypass `MTLReportFailure`/asserts, change UID/runAsRoot/
  ownership, widen entitlements, or alter global shader validation.

## 6. Commands, exits and final state

- `metal2metal.py verify-runtime-manifest` on the copied route/source/
  output: exit 0, `verified profile=ventura13-ios19-macabi
  translated=1825`.
- Offline AIR scan: 1,524 `air64-apple-macosx13.4.0` in source and 1,524
  `air64-apple-ios19.0.0-macabi` in the converted metallib; F11H contract
  and hashes as above.
- External generation: tccd 50427, WS 50574 (0.8–0.9% CPU), agent 51072
  alive; composite `state=missing`; untouched. Native health tccd 883 /
  SpringBoard 872 / backboardd 905; thermal nominal 34.59 °C; no new
  crash during R006; both markers absent; no owned helper/sampler
  processes.
- Evidence: `coordination/artifacts/R006-mps-f11h-20261018.json`;
  ignored analysis inputs `analysis/r006-mps-f11h/` (route, original and
  converted metallib, hashes). No source/test/build/Git change.

(handover to codex now)

# Frontend R025 result — exact 22F82 libxpc guard attribution

Status: **minimum exact artifacts acquired; fault chain resolved; only the
`xpc-fault` string value remains uncaptured.** Read-only device/local work:
no package/source/build/Git change, no WindowServer generation, no remote file
write, no TCC/policy/trustcache change. Endpoint/credential used only in the
temporary process environment and not recorded.

Device: iPad13,11, iPadOS 16.3 (20D47), xnu-8792.82.2.

## Exact acquisition (all exits 0)

- Full `.map`: 874,394 B, SHA `4c817de2…2088`.
- libxpc `__TEXT` `0x136000`-`0x17A000`: 278,528 B, SHA `af2bbe7c…0db5`
  (matches expected).
- Frame-0 block `0x3CE000`-`0x3CF000`: SHA `5a34a937…3696`.
- libsystem_kernel header at `0x3BF000`: SHA `e03416bf…078a`.
- Device iOS launchd: SHA `793c051b…fa4f`, UUID `790A3FDD-…94EB`.

## Exact attribution

- Frame 0 `0x3CEB78` belongs to
  `/usr/lib/system/libsystem_kernel.dylib` UUID
  `34A49B54-82B2-37A1-9314-F6A4A2BB3FF8`, image offset `0xFB78`.
  Exact bytes: `img+0xFB70 mov x16,#0x211; img+0xFB74 svc #0x80;
  img+0xFB78 b.lo` (frame). `0x211` = 529 =
  `os_fault_with_payload`; frame 0 is the syscall return.
- Frames 1–5 remain exact libxpc image offsets
  `0x5F8C`, `0xB928`, `0xB7B0`, `0xB0E4`, `0xAC84`.
- Frame 1 is the return after the only direct fault call:
  function `img+0x5E9C` reads `xpc-fault` from the reply dictionary and at
  `img+0x5F88` calls fault function `img+0x1D5A8`.
- Fault function `img+0x1D5A8` takes the reason string in x0 and calls
  `os_fault_with_payload(7, 9, NULL, 0, reason_string, 0)` via
  `img+0x1D754`. This is the exact 22F82 code; no 15.6 inference.
- Frames 2–5 are libxpc backtrace context, not the fault function and not
  direct callers of it.

## Reason-string correlation

`xpc-fault` is the dictionary key; its value is the reason string. Exact
launchd log (`launchd.log.1:64609-64610`):

```text
18:24:43.889040 launchdchrootex.67961 sent an invalid request
18:24:44.009730 failed lookup: name = com.apple.analyticsd, flags = 0x8,
                requestor = codesign[67961], error = 3: No such process
```

PID 67961 is the launcher/codesign process; the guard is 1 ms later. This is
a strong trigger correlation, not proof of the dictionary value. The only two
literal `xpc-fault` set sites in iOS launchd are cross-domain violation paths,
not the analyticsd lookup; `log show` for the same second produced no output.
The actual value remains missing. Do not substitute the launchd log text or a
15.6 misuse message for it.

## Remaining minimum input

Only the actual `xpc-fault`/`os_reason` value: a non-lightweight report,
ReportCrash line, retained libxpc `"%s (backtrace follows)"` log, or an exact
producer analysis. No more cache/map/libxpc bytes are needed.

Files:
- `docs/evidence/r025-libxpc-guard-attribution-20261018.md`
  (SHA-256 `40dec5e0f1041c974c41379d250a07105e752dfdda48da08ca4eba1e50eaa0b4`)
- `coordination/artifacts/R025-libxpc-guard-attribution-20261018.json`
  (SHA-256 `79b7f548a48e2f98c29a8366b550bac5bfcbfa26e0369320d0121580a916951e`)
- Raw exact artifacts under `analysis/r025-libxpc-guard/` (gitignored).

No background tasks remain running or queued.

(handover to codex now)

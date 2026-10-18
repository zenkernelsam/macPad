# Round 001 — frontend/provider offline boundary result

Date: 2026-10-18. Role: frontend. Scope: offline only (no iPad start, no deploy,
no WindowServer run). Device credentials supplied inline were not used and are
not recorded.

## Files

| File | SHA-256 |
| --- | --- |
| `misc/cydiasubstrate_macos.m` (fixed) | `6ac796c78f11fce18806840a20276308c782dbc77603c7781fc55d35eeaa7c2b` |
| `misc/cydiasubstrate_macos.m` (HEAD baseline) | `7b58e1d177349b98a4891e2230a48d3152505e94999e44a1191c6997009fffad` |
| `include/macws_app_input_route.h` | `d21f6b421ab5357723c1a3facd9b876b5d40fb7b122a9ca74107902ca1daeb71` |
| `libmachook/AppInputBridge.m` | `e794623b6130deb440f9272a8d54ca05afffd4940e38673ec232c0726c64a32b` |
| `misc/test_cydiasubstrate_provider_contract.py` | `27fad86b5293efb5d06808076e789ebee3e4f5504dc69a4cb2cd54e2bc28f7be` |
| `misc/test_app_input_route_contract.py` | `91dbfdedb5cb9c9f5ce979ca97f816c08c0f13ac3603ba7ea773d8b109dce5c2` |
| build artifact `libmachook/.theos/obj/libmachook.dylib` (not committed) | `63e9a7fc27822b1a19a38c13e27b0707a95af445c663ffcdf28e9fcd221febb3` |

## Changes

1. `MSFindSymbol` now converts every LINKEDIT file offset with
   `slide + __LINKEDIT.vmaddr + (off - __LINKEDIT.fileoff)` and bounds by
   `filesize`. Export trie and `LC_SYMTAB` share the conversion; local
   `N_SECT` symbols resolve; malformed images fail closed.
2. `MSHookMessageEx` now installs through `class_replaceMethod`, publishes the
   original IMP before installation, overrides inherited methods only on the
   requested class, supports metaclasses, and fails closed on missing
   class/selector/IMP/type encoding.
3. Added `include/macws_app_input_route.h`, a pure C classifier for exact-PID,
   exact-window, global-system-surface and kind-specific window requirements.
   `AppInputBridge.m` calls it from `MacWSInputRecordIsValid`; live
   `windowWithWindowNumber:` resolution remains downstream.

No share-package constant-true TCC hook, assertion bypass, 15.6.1 sidecar, or
M2/G14 ABI was copied. Existing permit containment for `MSHookFunction` is
unchanged.

## Commands and exits

| Command | Exit |
| --- | --- |
| `python3 -m unittest -v misc.test_app_input_route_contract misc.test_cydiasubstrate_provider_contract` (5 tests) | 0 |
| `cc ... misc/macws_protocol_test.c -lm` + run (`PASS`) | 0 / 0 |
| `python3 misc/audit_runtime_switches.py` (309 env / 86 flags / 467 entries) | 0 |
| `git diff --check` | 0 |
| `xcrun --sdk macosx clang -arch arm64 -c misc/cydiasubstrate_macos.m` | 0 |
| `xcrun --sdk macosx clang -arch arm64e -c misc/cydiasubstrate_macos.m` | 0 |
| `make THEOS=/Users/ciscohe/theos -C libmachook -j4` (arm64 + arm64e) | 0 |

Provider control (same synthetic cache-layout fixture):
- HEAD source: `VERDICT=provider-contract-gaps-present`,
  local/export NULL, method before=3 after=3.
- Fixed source: `VERDICT=provider-contract-satisfied`,
  local/export exact, method before=3 after=7, original callable=3.

## Limits / unresolved

- No device or 22F82 live-image acceptance. The provider was not run inside
  WindowServer; `MSFindSymbol` was not called against a live 22F82 process.
- `MSHookMessageEx` semantics verified against host libobjc only (inherited
  method, superclass untouched, original callable, metaclass, fail-closed).
- Real SkyLight 22F82 layout was inspected statically
  (`__LINKEDIT.fileoff 0x4753c1` vs `vmaddr 0x22193c000`; local symbols
  `_CGXSenderCanSynthesizeEvents` / `_audit_token_check_tcc_access`), which
  confirms the conversion gap but is not runtime acceptance.
- Full `misc/test_*.py` suite: `Ran 708 tests in 324.412s`,
  `FAILED (failures=14, errors=5, skipped=14)`, exit 1. Failure families are
  pre-existing host MetalTexel/NoCopy/IOSurface probes, webview-lifecycle
  concurrency, Metal diagnostic hot path, and the atomic-pointer baseline;
  none touches the files in this packet. Not claimed green.
- `test_atomic_pointer_click` proxy-string failure is pre-existing: its target
  file `libmachook/mac_hooks.m` was not modified by this packet.

## Cleanup

No device-side markers, jobs or processes. Local controls and logs live under
`/tmp`; build output is in ignored `libmachook/.theos/`.

Full detail: `docs/evidence/provider-and-appinput-route-contract-20261018.md`.

(handover to codex now)

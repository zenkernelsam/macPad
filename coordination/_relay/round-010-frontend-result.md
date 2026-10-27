# Round 010 — frontend result: WS spindump symbolication boundary

Date: 2026-10-18. Read-only offline analysis; no device/SSH, source/test/build
change, build or Git write. Added only this result and the dated evidence.

Candidate HEAD: `7e5d771`. Backend R005's generation was untouched.

## Local input status

The R005 spindump is **not local**. Searches of `/tmp`, `~/Downloads`,
`~/Desktop` and the repo found only iOS `libspindump.dylib`, no report text.
`coordination/artifacts/r005-lldb-pane1.txt` is a **10-15, PID 55506,
breakpoint-induced** pane at `WSAuditTokenHoldsCachedProcessRight +200`, not
the R005 10-18 WS 50574 passive sample; it cannot be used as a substitute.
The local R005 JSON is only a summary: main thread unsymbolicated, CA display
thread blocked on main 142/150, no `macws_coexist_wait` symbol observed.

**Therefore the main-thread stall cannot be symbolized locally and whether it
enters the pacing wait is currently undetermined.** “No symbol observed” does
not prove absence because the summary says the main thread is unsymbolicated.

## Exact files needed (minimum)

1. `/var/mobile/macws_r005_evidence_20261018/ws-spindump-20261018.txt`
   (3.8MB, original; includes raw frames/Binary Images if available).
2. WS 50574 loaded-image list/UUID/load-address/slide if the spindump lacks
   raw PCs: SkyLight UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`; WindowServer
   main UUID (verify; the old pane’s `465422C7-…` is a different generation);
   libmachook installed UUID/load address.
3. Exact installed libmachook artifact or dSYM/symbol table: arm64e SHA
   `9a511953…572819`, arm64 SHA `151a2175…32bced`. The local `.theos` build
   (`2df74403…`/`80b5ae2d…`) differs; its offsets must not be used.
4. Local `analysis/SkyLight_22F82` (UUID `96676A53-…`, SHA `13d7a0ca…`) is
   ready for disassembly once slide/offset is known.
5. Exact 22F82 libsystem_kernel image/UUID only if kernel frames need
   independent verification.

## Source-level decision contract (FACT)

`macws_coexist_wait_for_completion_slot` (`mac_hooks.m:27319`) locks
`pace_lock` (27324), polls the interaction wake socket (27343-27354), and
falls back to `usleep` (27357/27361). Callers: 19896
(`macws_iomfbserver_begin_skylight_update`), 27437
(`MacwsIOMobileFramebufferSwapEnd_new`), 27518
(`IOConnectCallStructMethod_new` selector-5 fallback).

Decision table after symbolication:

| Main-thread frame | Owning path | In pacing wait? |
| --- | --- | --- |
| `poll`/`usleep` return address in installed libmachook pacing function | interval loop | **YES** |
| `__psynch_mutexwait` at pacing function entry | `pace_lock` contention | reaches function, not interval sleep |
| `mach_msg2_trap`/`__psynch_mutexwait` in SkyLight audit-token/TCC/LaunchServices | unrelated block | **NO** |
| unsymbolicated libmachook frame | unknown | cannot determine |

The CA thread’s 142/150 wait on main is consistent with any long main-thread
block and does not identify pacing.

## Conditional next experiment

Do **not** run the enqueue→wait→dispatch→response timing experiment yet; the
sample cannot be checked. Only if the symbolized main thread maps to the
pacing `poll`/`usleep` (first row) should an owned generation instrument the
three call sites with bounded read-only timestamps, one attributable request,
frame sequence and idle CPU; no new receiver/fence/forced completion and no
timeout restart. If it maps elsewhere, investigate that blocking path first.

## Responsibility retention

R009 remains: exact 22F82 libquarantine policy `Quarantine`, selector `0xb6`,
helper structure confirmed; the stub GOT/bind (`0x1dfef8f88`) and actual 103
branch (precheck/no-match/matched handler) are still unknown. It is not a
proven “no policy” result; no adjacent-version re-analysis or setter.

## Commands / exits

Local `find`/`grep` for spindump content, source line inspection, `shasum`
and `git log` exited 0; no report file exists. No test/build/package/device
command; no commit/reset/stash/push.

Full detail: `docs/evidence/r010-ws-spindump-symbolication-20261018.md`.

(handover to codex now)

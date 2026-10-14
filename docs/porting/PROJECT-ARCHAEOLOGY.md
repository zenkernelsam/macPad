# macPad project archaeology — the full arc, 2026-09-26 → 2026-10-13

Reading this doc first gives a new agent the whole story in one pass. Each
era names its canonical docs; those docs carry the verbatim evidence. The
rule: **trust the dated doc's own evidence labels over any summary here**.

The project began as a fork of the author's `MacWSBootingGuide` — a working
Ventura-13.4 (22F66) stack on iPad13,6/iPadOS 16.3.1: native AGX desktop,
windowing, input/IME, VNC observer, apps running. Everything in this repo is
either (a) that upstream stack, (b) the 15.6.1 research branch (frozen), or
(c) the 22F82 bring-up (current).

---

## Era 0 — upstream foundation (before this repo's sessions)

`MacWSBootingGuide` upstream. Validated matrix and architecture live in
`AGENTS.md` (authoritative). Key fact that still drives decisions: the
author's runtime witnesses (menus opening in 0.3 s, sendEvent: delivery,
nested submenu tracking) were produced on a **non-`-virtualonly`** WS
generation on iPad13,6 — that difference mattered on 10-13.

Canonical refs: `AGENTS.md`, `analysis/MacWSBootingGuide/` subtree,
`docs/evidence/vnc-usability-stability-20260729/` (22F66 witnesses).

## Era 1 — macOS 15.6.1 / 24G90 CLI research (09-26 → 10-04, FROZEN)

Goal: real `chroot /var/mnt/rootfs /bin/echo HI` on a macOS **15.6.1**
userspace + dyld + shared cache. The detailed era narrative is
`ARCHAEOLOGY-15.6.1.md`; **all docs cited in this era's table live in
`docs/porting/archive/macos15-roots/`**.

| date | milestone | doc |
|---|---|---|
| 09-26 | first handover; chroot gets iOS shared region attached at exec (syscall 294); macOS dyld adopts iOS cache and loads ~90 iOS dylibs | `HANDOVER-15.6.1-2026-09-26.md`, `dyld-15.6.1-state.md` |
| 09-27 | exec gate cracked (`dyld` admit); `echo HELLO` post-cache SIGSEGV hunt; syscall 536 EINVAL root cause = **cache slide-info version 5 > kernel max 4** (`vm_shared_region.c:2934`) | `HANDOVER-HELLO-2026-09-27.md`, `HANDOVER-DYLD-ADMIT-2026-09-27.md`, `HANDOVER-REPLY-2026-09-27.md` |
| 09-28 | **syscall 536 works** — masking `VM_PROT_SLIDE` maps the cache, plus `files[].sf_slide` 16K-alignment fix; CLI milestone via **disk libSystem shim**: `echo HELLO`, `cat`, `sh -c` all pass (shim witnesses, NOT cache) | `HANDOVER-REPLY-2026-09-28-536.md`, `CLI-MILESTONE-2026-09-28.md`, `HANDOVER-DYLD-536-POPULATE-2026-09-28.md` |
| 09-29 | consolidated state; pointer-format-13 pager vs iPadOS 16.3 pager mismatch found via real IDB; 4-GB shared-region gap crossed by `emptysr+highreserve` dyld diagnostic | `CONSOLIDATED-2026-09-29.md`, `STATIC-*` docs |
| 09-30 | upstream merge; M3 dyld-pager task handed off | `HANDOVER-REPLY-2026-09-30-*`, `HANDOVER-M3-DYLD-PAGER-2026-09-30.md` |
| 10-01 | M3 pager final report; KB-E shared-region limit documented | `HANDOVER-REPLY-2026-10-01-FINAL.md`, `HANDOVER-15.6.1-COMPLETE-BRIEF-2026-10-01.md`, `KB-E-shared-region-limit.md` |
| 10-02 | Devin→external-GPT super handover — **15.6.1 echo HI still unreached** at that point | `HANDOVER-DEVIN-TO-EXTERNAL-GPT-2026-10-02.md` |
| 10-04 | **branch FROZEN** at kernel boundary | `HANDOVER-MACOS15-FROZEN-2026-10-04.md` |

**Why 15.6.1 froze** (RE-confirmed via T8103 kernel IDA + runtime): after
real dyld + real 24G90 cache mappings were achieved, the first cache text
page faults died `KERN_CODESIGN_ERROR`. T8103's `sub_FFFFFE00086A0924`
selects a PMAP owner for shared-region VAs by `nested_region_addr/size`
only — it never consults `nested_region_asid_bitmap` — so the fault routes
to an empty nested pmap and PPL returns failure below every userspace
fixable layer (trustcache, file hashes, page content all verified correct).
That is a kernel/PPL boundary, not a userspace problem; the branch was
frozen rather than patched around.

**Load-bearing lessons this era bequeathed:**

- `rm` before `cp` on deployed binaries (vnode CS cache) — first written
  down here; confirmed again catastrophically on 10-13
- "process survival/rc=0 ≠ milestone" — only stdout `HI` counts
- `error 40`/`EINVAL`/`SIGILL(132)`/`137` distinguish kernel-admission vs
  userspace-vs-codesign failures; `| head` masks real rcs
- LC_MAIN args arrive in x0-x3 registers, not the stack
- Darwin arm64 `fork` returns child-marker in **x1**, not x0==0
- static arm64e Mach-O can never exec (kernel veto)
- upstream methodology: README admits only GradedArchs patch — upstream
  never remaps the shared cache; adopt-iOS-cache was the designed path

## Era 2 — Ventura 13.4.1 / 22F82 pivot and `echo HI` (10-04 → 10-07)

`archive/handover-chain/HANDOVER-MACOS13-VENTURA-2026-10-04.md` records the
decision: 22F82 is the
newest build inside the author-validated Ventura family (13.4/22F66 and
13.4.1/22F82 share one cache CDHash pair in postinst/macos_gui); 13.7.x is
outside the validated family and was rejected as a target. 13.2.1 assets
were retired.

Canonical doc: `ventura-22F82-bringup-state.md` (the running state file).

| date | milestone |
|---|---|
| 10-06 | device inventory (`ventura-handover-device-inventory-20261006.md`); 22F82 rootfs installed |
| 10-06 ~22:02 | **`echo HI` achieved** via `launchdchrootexec` — real 22F82 dyld arm64e + real split shared cache + cache-resident libSystem (`.ips` usedImages `source:"P"` = cache). Blockers cleared: `ldid -Hsha256 -S<proj-ent>` ×2 + `jbctl trustcache add` (the 15.6-era recipe); `os_variant_has_internal_diagnostics` brk#1 bypassed by libmachook `DYLD_INTERPOSE`; macOS-platform `CydiaSubstrate` scaffold (`misc/cydiasubstrate_macos.m`, no-op MSHook stubs — labeled diagnostic seam) |
| 10-07 | cold-reboot verified — `jbctl trustcache` entries **survive reboot** on this Dopamine build (earlier "volatile" assumption corrected); `os_variant` trap root-caused to `kern.osvariant_status=0x7000000100000028` sysctl success path skipping file/override fallback — **no file/env/sysctl lever reaches it, interposition is required by design**; **literal bare `chroot` achieved** via `LC_LOAD_DYLIB`/`DYLD_INSERT_LIBRARIES` interpose carrier (commit `82403bd`); full CLI sweep: **918 binaries** on literal chroot (`73fe7af`, `cli-manifest-20261007.json`); plugin-closure trust registration 3566 hashes (`8882b3b`); cold-boot restore automated (`a2197f1`) |

Three different "echo" milestones — do not conflate:

1. `echo HELLO` on 15.6.1 via **disk libSystem shim** (09-28) — shim witness
2. `echo HI` on 22F82 via launchdchrootexec, **real cache** (10-06)
3. `/var/jb/usr/bin/chroot` **literal binary**, env-inserted interpose (10-07)

## Era 3 — desktop chain: LS/XPC + compositor (10-08 → 10-13)

Canonical docs: `ventura-22F82-desktop-chain-20261011.md`,
`ventura-22F82-input-transport-and-composite-20261013.md`, runbook
`ventura-22F82-session-reproduction-20261013.md`.

| date | milestone |
|---|---|
| 10-08 | WindowServer + virtual display up; display-hazard lesson recorded (rebooting a crashing WS leaves the panel owned by a dead process → looks like a frozen iPad; it was a button restart, not a panic) |
| 10-08..10 | XPC `Connection invalid` root cause: Dock passes `XPC_CONNECTION_MACH_SERVICE_PRIVILEGED` on mapped names → strip flag on mapped names only; Dock gets real ASN; `launchservicesd`, lsd pair, cfprefsd, `diskarbitrationd` (datavault entitlement stripped — that was the OS_REASON_EXEC 137 cause) all live |
| 10-11 | x86 dev-host handover; `final-composite` replay unanswered = documented blocker |
| 10-12 | fullscreen input routing → SLS-resolved owner (`c48a81d`); foreign-surface records forward to OSXvnc pointer proxy |
| 10-13 | **compositor resurrected**: EndUpdate never runs → `macws_publish_scanout_after_swap` publishes owned scanout per delivered swap (`0614a5b`); `state=ready` + continuous composite; wallpaper fixed (dynamic HEIC→static payload); **input transport fully mapped — dead at WS event subsystem**: `com.apple.WindowServer.HIDEvents` never registered, `SLSGetEventPort` returns an invalid port, all `CGEventPost` taps die, `SLSPostEventRecord` returns WS's own 1002 rejection, zero `WS-DISPATCH` witnesses all session |

## Where each era's lesson lives

| lesson | where written down |
|---|---|
| symptom-suppressing patch ≠ fix (the patch-discipline table) | `AGENTS.md` top |
| every claim needs disasm or runtime log (evidence discipline) | `AGENTS.md` top |
| fresh-inode deployment | `build_on_ios.sh` comment + 10-13 evidence §1 |
| trustcache restore on cold boot | `bringup-state` §10-07 + 10-11 evidence |
| dynamic HEIC → static payload | 10-13 evidence §3 (+ TODO: bake into build-rootfs) |
| nominal rc ≠ delivery; WS-DISPATCH is the acceptance test | 10-13 evidence §4.6 + runbook §9 |
| 15.6.1 kernel boundary (why we left) | `archive/macos15-roots/HANDOVER-MACOS15-FROZEN-2026-10-04.md` |

## Current frontier — one paragraph

Everything except **input dispatch** works end-to-end on 22F82: real chroot
CLI (918 binaries), WindowServer compositing with `state=ready`, wallpaper,
Dock launches, LS/XPC, trust pipeline, VNC observe. The blocker narrowed
again on 10-14/15: `SLSGetEventPort` is actually valid (`rc=0` receive
right), posts reach `_XPostEventRecord` and get `rc=1002` from the real
permission-check chain — runtime telemetry shows `gCurMsgAudit` populated
(52-byte sender creds) and the check returning 0. A repaired
`MSHookFunction` provider (`misc/cydiasubstrate_macos.m`, permit-gated
16-byte trampoline) is verified live. Remaining unknowns: the lazily-
resolved checker identity and what populates WS's per-{pid,pidversion}
rights cache — that work is gated on an IDA session against
`analysis/SkyLight_22F82` (UUID 96676A53-B1E0-3D7E-B98B-B73873CD1880);
all static interpretations before that are provisional. Input is still
unaccepted: `SLSPostEventRecord` → 1002, no WS-DISPATCH witness.

# D147 — feasibility audit of D146's no-restart witness plan

Frozen-input verification of each D146 §4 observation: prerequisites,
source-confirmed offsets/patterns vs unverified transport/tool claims.
**All four observations require on-device execution** — "no-restart" means
no SkyLight/WindowServer restart, not no device access. None is possible
purely offline; the smallest step is a single bounded read pass over SSH.

## O1 — live WS process identity: FEASIBLE, low privilege

`pgrep -fl WindowServer` / `launchctl print` of the
`UIKitApplication:com.macwsguide.windowserver` job. The plist launches
`launchdchrootexec 0 0 /var/mnt/rootfs …/WindowServer -virtualonly
-daemon` (layout plist verified). Precondition: the D130 generation
(pid 22527) must still be running — unverifiable offline; `ps -o lstart`
pins start-time so a pid-reuse edge can't misbind.

## O2 — state-file generation binding: FEASIBLE, unprivileged read

`/var/mnt/rootfs/tmp/macws_final_composite.state` —
`state=ready producer=<pid> sequence=N reason=validated-final-composite`.
Binding rule: `producer` == live WS pid AND WS `lstart` unchanged across
two samples AND `sequence` advanced ⇒ publisher still firing in this
generation. Pid-reuse edge closed by the lstart check. Two reads a few
seconds apart suffice — this is the cheapest observation in the plan.

## O3 — raw WindowServer.err capture: FEASIBLE, root read

`StandardErrorPath=/var/jb/var/mobile/WindowServer.err` (layout plist —
outer-launchd path, not chroot). `scp`/`cat` as root. Content
expectation per D146 §0: only `MACWSHook:` raw-write lines can exist;
count + attribute REFUSED lines to this file specifically (fixes the
D130 transcription gap). Caveat: the file exists only while the launchd
job ran — D126 showed a failed start can leave it absent; absence with a
live WS pid would itself be an anomaly to record.

## O4 — 16-byte patch-stub reads: FEASIBLE-IN-PRINCIPLE, privileged
(task_for_pid) — one unverified transport link

**Source-confirmed** (exact 13.4 ABI tables, `mac_hooks.m:1739/19702`):

| site | image (UUID) | imgoff | original prologue |
|---|---|---|---|
| publicSwapEnd | IOMobileFramebuffer `7AC531B7-…` | `0x11cc` | `b4000080 f9439401 b4000041 d61f083f` |
| enableTagList (observer) | QuartzCore `CF853BBD-…` | `0x29285c` | `d503237f d10243ff a9036ffc a90467fa` |
| begin skylight_update (pacing) | QuartzCore | `0x291288` | `d503237f a9be4ff4 a9017bfd 910043fd` |
| finish skylight_update (pacing) | QuartzCore | `0x291220` | same |

Installed ⇒ bytes = relocator stub `ldr x17, #8` (`51 00 00 58`),
`br x17` (`20 02 1F D6`), `.quad <thunk>`; the `.quad` target should lie
inside the mapped CydiaSubstrate thunk page / point at a libmachook
function — double confirmation. Original prologue ⇒ not installed
(**bound**: bytes distinguish installed/not, never "refused vs never
attempted" — both leave originals).

**Live address resolution**: TASK_DYLD_INFO → `dyld_all_image_infos` →
walk image infos, match the two LC_UUIDs → per-image load address
handles ASLR; no fixed VA guessing (fixes D119's lesson).

**Unverified links** (stated, not assumed):
- `task_for_pid` on WindowServer as root under Dopamine 16.3 — in-repo
  precedent: `misc/macws_7dtd_memory_probe.c` uses
  `task_for_pid`+`mach_vm_read_overwrite` as root vs a game process;
  WS is launchd-chroot-spawned, unsigned-adhoc — expected to allow root
  task ports, **never runtime-verified against WS specifically**.
- No in-repo WS-memory reader exists — the primitive is precedent code,
  the tool itself is a small new diagnostic binary (build+deploy needed).
- TASK_DYLD_INFO on a chroot process returns macOS dyld infos —
  mechanism is standard; untested here.
- Bonus witness the same walk gives for free: `CydiaSubstrate` in WS's
  image list = direct mapped-provider witness (upgrades O-count on
  question (1) without relying on err transcription).

## What is NOT available without cold load (D146 §5 confirmed)

`####`-gated lines (latched), success-side permit rc (never logged),
SENDEROBS hooks (load-time sentinel), and any distinction of
refused-vs-unattempted — byte reads only ever show installed/not.

## Smallest safe next step

One bounded on-device read pass, in order: O1 (pid+lstart) → O3
(scp `.err`) → O2 (state file ×2) → O4 optional behind a dedicated
task that builds the TASK_DYLD_INFO+vm_read probe. O1–O3 require no new
tooling, no privilege beyond existing root SSH, and already close the
provider-mapped + publisher-firing + generation-bound questions at
consumer/direct level. O4 is the only path that can additionally settle
*pacing-permit* install state.

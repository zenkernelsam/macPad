# D102 — pacing thread/lock ownership vs the D101 services loop

Offline only. Inputs: `analysis/SkyLight_22F82` (UUID
`96676A53-B1E0-3D7E-B98B-B73873CD1880`), `libmachook/mac_hooks.m`,
`analysis/r007-ws-sample/ws-spindump-20261018.txt` (SHA
`de7fe9fb50e1018b0aa3e91eab98f4ee07ff9b862e3ee6cc99332f35129a3d05`),
`analysis/r007-ws-sample/loaded-images.json`, and the prior symbolication
`docs/evidence/r011-ws-stall-symbolication-20261018.md`.

## 1. Generation separation (required)

- Spindump covers **WindowServer PID 50574**, sampled 2026-10-18
  16:42:01–04, `Time Since Fork 1434s` → an **historical generation**.
- Current producer generation is **PID 10447** (lstart 10-19 00:17:23);
  **no saved sample or crash stack exists for 10447** — nothing below is
  attributed to the current generation.
- R011 already proved the injected image `CC9C81E9` cannot be resolved to
  a specific local build; that limitation stands (offsets
  `+0x39098`/`+0x39050`/`+0x246b8` are "injected dylib", function
  unknown).

## 2. Pacing callers (source, exact)

`macws_coexist_wait_for_completion_slot` (`mac_hooks.m:27489`) has exactly
3 call sites, all on the update/swap path:

| site | context | lock state at call |
|---|---|---|
| `:20055` `macws_iomfbserver_begin_skylight_update` | before `IOMFBServer::begin_skylight_update` | deliberately **pre-lock**: before QC `server+0x18`/`server+0x158` acquire (`:20012–20037`) |
| `:27607` `MacwsIOMobileFramebufferSwapEnd_new` | SwapEnd hook, only if `g_macws_iomfbserver_finish_depth == 0` | outside server locks only when finish scope active (`:27600–27608`) |
| `:27688` `IOConnectCallStructMethod_new` sel-5 | kern_SwapEnd fallback | same `finish_depth` guard (`:27681–27689`) |

Wait internals (`:27489–27563`): `pace_lock` pthread mutex serializes the
wait itself; `poll()` on `macws_coexist_interaction_wake_socket()` with
timeout, or `usleep`; `CLOCK_MONOTONIC` deadline against
`last_completion_ns`; re-paces on interaction wake. So the pacing wait
holds only `pace_lock` — a mac_hooks-private mutex contended exclusively
among the three updater-path callers — and is taken **before**, not
inside, the QC display-server mutexes.

## 3. What the preserved 50574 stack proves

Main thread = `Thread 0x978af "ws_main_thread"` = **the services loop
thread itself**. 147/150 identical samples, root-first (R011
symbolication re-verified against this exact binary):

```
_SLXServer+0x534            0x1a0e62504 -> 0x185422504
_server_loop+0x8c           0x1a0e626c4 -> 0x1854226c4
CGXRunOneServicesPass+0x57c 0x1a0e61bb8 -> 0x185421bb8   ; == insn after bl _post_port_data (0x185421bb4)
_post_port_data+0x118       0x1a0e61f60 -> 0x185421f60   ; == insn after blraaz handler invoke (0x185421f5c)
_non_coalesced_timer_handler+0x18  0x1a0e23258 -> 0x1853e3258
_run_timer_pass+0x1f8       0x1a0e2314c -> 0x1853e314c
CC9C81E9 +0x39098           0x102c5d098                  ; injected dylib (unresolved)
update_display_callback+0x558      0x1a0daf264 -> 0x18536f264
CC9C81E9 +0x39050           0x102c5cfd0                  ; injected dylib (unresolved)
WS::Updater::UpdateDisplays+0x33a0 0x1a0dbcbe8 -> 0x18537cbe8
SLCADisplay::present_update+0x374  0x1a0c9e210 -> 0x18525e210
SLCADisplay::finish_external_update+0x98 0x1a0c9d000 -> 0x18525d000
983665BA +2585060404        0x1a6a66434                  ; cache frame, unresolved
CC9C81E9 +0x246b8           0x102c486b8                  ; injected dylib (unresolved)
libsystem_kernel +0x9834    0x19be08834
kernel leaf                 0xfffffe0007f92f1c  (146 samples)
1-sample leaf: IOWorkLoop::sleepGate+0x34 / IOCommandGate::runAction+0xcc /
               lck_mtx_sleep+0xb8  (IOKit command-gate wait)
```

**Established by this stack:**

- The services loop thread **executes the display-update path inline**:
  a timer port message → `_post_port_data` → `non_coalesced_timer_handler`
  → `run_timer_pass` → `update_display_callback` → `UpdateDisplays` →
  `SLCADisplay::present_update` / `finish_external_update` — the same
  call region that contains the hooked IOMFB begin/finish/SwapEnd pacing
  sites. So **pacing, when it runs, executes on the services loop
  thread** in this configuration.
- The thread was observed blocked *inside* that path in a **kernel
  wait**: 146 samples ≈ `assert_wait_deadline_with_leeway` (timed wait),
  1 sample on an **IOKit `IOCommandGate`/`lck_mtx_sleep`** — a kernel
  driver lock (IOMFB family gate), not a userland mutex.
- `com.apple.coreanimation.display.primary` was **turnstile-blocked on
  ws_main_thread** in 142/150 samples (`*142 … (blocked by turnstile
  waiting for WindowServer [50574] thread 0x978af)`) — the CA display
  timer waits on the services-loop thread's in-flight update; the
  wait direction is timer→main, not main→timer.

**Not proven by this stack (R011 carried forward):**

- No sample PC sits inside `macws_coexist_wait_for_completion_slot`'s
  `poll`/`usleep` — the pacing *wait* itself is not witnessed in this
  sample set; the stack places the thread inside the update path where
  pacing *can* run, not inside the wait function.
- Whether the observed kernel wait is "a lock held across pacing" is not
  decidable from the stack alone: the leaf is a kernel timed wait /
  IOKit gate, and the two unresolved libmachook frames
  (`+0x246b8`, `+0x39098`, `+0x39050`) cannot be bound to specific
  hook sites without the CC9C81E9 build.
- The QC `server+0x18`/`server+0x158` mutexes (RE'd in the `:20012`
  comment) and `pace_lock` are userland pthread mutexes — a wait on them
  would appear as `__psynch_mutexwait`; **no such leaf appears** in the
  saved 50574 stacks, so "services loop blocked on a lock held across
  pacing" is *not established* — and "absence of a stack" is not
  evidence the behavior is absent.

## 4. Label corrections (carried from D101, refined)

- rendezvous entry → impl `blraaz` (`0x1851eb584`) = **handler prelude**
  — request already dequeued.
- `_CGXPostReplyMessage` small-reply path = **enqueue** (`_gOutMsg`),
  not send; send = `mach_msg` at `0x185421554` (push_out_message) or
  `mach_msg_overwrite` at `0x185421974`.
- client send→receive-return is **not** "pure kernel queue residence":
  it also includes sender IPC cost, kernel delivery, **and the services
  thread's remaining work in the in-flight pass** — which per §3 can be
  a full display update (including any pacing wait inside it) because
  dequeue only recurs when the loop reaches `mach_msg` again.

## 5. Missing observation for a future one-variable measurement

To isolate "request waited while services loop was inside pacing" on
this architecture, the bounded observation is a single timestamp pair:

- `t_recv` — receive-trap return `0x185421978`/`0x1854219a8`, and
- `t_dispatch` — the handler `blraaz` `0x185421f5c` for that same
  message,

optionally joined with a client-visible stamp. To test the pacing
coupling specifically, bracket `macws_coexist_wait_for_completion_slot`
entry/exit (source-level, already gated diagnostic precedent) on the
same thread and compare `t_dispatch − t_recv` for the request(s)
dequeued immediately after a paced update — one variable, no new state
machines. This is only definable; no instrumentation is authorized.

Also unresolved if pursued: the exact `CC9C81E9` build's symbol map
(deployed dylib UUID differs from every local copy) — needed to name the
two inline libmachook frames in the 50574 stack.

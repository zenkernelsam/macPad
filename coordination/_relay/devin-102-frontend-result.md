# D102 frontend result — pacing thread and lock ownership evidence

Offline static + preserved-sample audit only. Evidence:
`docs/evidence/devin-d102-frontend-20261019.md`.

## Findings

- **Pacing callers (exact)**: `macws_coexist_wait_for_completion_slot`
  (`mac_hooks.m:27489`) is called from 3 sites only, all on the
  update/swap path — `begin_skylight_update` hook (`:20055`,
  deliberately **pre-lock** before QC `server+0x18`/`server+0x158`),
  `SwapEnd` hook (`:27607`), and `IOConnectCallStructMethod` sel-5
  fallback (`:27688`), the latter two gated by `finish_depth==0`. The
  wait holds only `pace_lock` (private pthread mutex serializing the
  3 callers), then `poll()`/`usleep()` on `CLOCK_MONOTONIC`.
- **Preserved stack (gen 50574, historical — NOT 10447)**: the
  services-loop thread (`ws_main_thread` `0x978af`) is captured
  *inside* the D101 dispatch chain: `CGXRunOneServicesPass+0x57c` →
  `_post_port_data+0x118` → `non_coalesced_timer_handler` →
  `run_timer_pass` → `update_display_callback` → `UpdateDisplays` →
  `SLCADisplay::present_update`/`finish_external_update` → injected
  dylib (`CC9C81E9` `+0x246b8`/`+0x39050`/`+0x39098`, unresolved) →
  kernel wait. So **the services loop itself executes the
  display-update path that contains the pacing hooks** — pacing runs on
  this thread when it runs.
- **Lock evidence**: 146-sample leaf ≈ `assert_wait_deadline_with_leeway`
  (kernel timed wait); 1-sample leaf `IOWorkLoop::sleepGate`/
  `IOCommandGate::runAction`/`lck_mtx_sleep` (IOKit gate). No
  `__psynch_mutexwait` leaf — so "services loop blocked on
  `pace_lock`/`server+0x158` held across pacing" is **not established**
  by this stack. `display.primary` turnstile-blocked on `0x978af`
  (142/150) — direction timer→main.
- R011 already ruled the sampled leaf is **not** the `poll`/`usleep`
  pacing-wait shape; that verdict stands and is incorporated.
- **Gen 10447**: no saved sample/crash stack exists — nothing here is
  attributed to the current generation; absence of a stack ≠ absent
  behavior.

## Label corrections

- prelude (rendezvous entry → `blraaz` impl `0x1851eb584`) is
  post-dequeue, not queue wait — confirmed.
- `_CGXPostReplyMessage` small path = enqueue only.
- Client send→receive-return additionally contains IPC delivery,
  scheduling, **and the services thread's remaining in-pass work**
  (which §3 shows can be a full paced display update — dequeue recurs
  only at the next `mach_msg`), not "pure kernel queue residence".

## Precise missing observation

One-variable: timestamp pair `t_recv` (`0x185421978`/`0x1854219a8`
receive-trap return) → `t_dispatch` (`0x185421f5c` `blraaz`) for the
same message, optionally joined with a client stamp; to test pacing
coupling, bracket `macws_coexist_wait_for_completion_slot` entry/exit on
that thread and compare `t_dispatch − t_recv` on the first request
dequeued after a paced update. Defined only — no instrumentation
authorized. Bonus unknown: the exact `CC9C81E9` build is needed to name
the inline libmachook frames.

No device, no probes, no source edits, no builds/restarts/Git.

(handover to codex now)

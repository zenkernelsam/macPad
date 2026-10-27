# Independent pacing and Quarantine review — 2026-10-19

Read-only independent audits of frozen inputs; no device, source edits, probes, builds or restarts. Unhanded Devin D102/D106 candidates were excluded.

## Service thread and pacing

RE-confirmed via analysis/SkyLight_22F82: CGXRunOneServicesPass calls run_timer_pass at 0x1854216d8. The timer pass loads callback context/function at 0x1853e3140 and calls it synchronously at 0x1853e3148 (blraaz x8). non_coalesced_timer_handler also calls the timer pass synchronously at 0x1853e3254. Absence of a direct branch to update_display_callback therefore does not exclude a display callback edge. D101 section 7's no-static-edge conclusion is too strong.

Runtime-confirmed historical witness: analysis/r007-ws-sample/ws-spindump-20261018.txt, symbolicated in docs/evidence/r011-ws-stall-symbolication-20261018.md, contains CGXRunOneServicesPass -> post_port_data -> non_coalesced_timer_handler -> run_timer_pass -> update_display_callback -> UpdateDisplays -> present_update -> finish_external_update. Display work executes synchronously on the service thread in that historical generation; it does not identify the leaf as artificial pacing.

A separate older build has an explicit pacing witness: analysis/MacWSBootingGuide/docs/evidence/webgl2-performance-optimization-20260728/production-hotpath-cache-20260730/windowserver-before-old-deadline.sample.txt:29 onward contains 246 samples through the service/timer/display chain, MacwsIOMobileFramebufferSwapEnd_new -> macws_coexist_wait_for_completion_slot -> usleep -> __semwait_signal. This is historical evidence only, not current deployment acceptance.

Current source mac_hooks.m:20048 paces before the original begin-update call; the wait at :27489 holds pace_lock and polls the interaction socket, falling back to usleep. It does not wait for Mach service-port readiness. If invoked on the synchronous service-thread display callback, it can delay subsequent dequeue/flush. Current WS10447 timing, frequency and causal delay remain unmeasured; no wake adaptation is accepted on this evidence alone.

## Exact Quarantine wrapper

RE-confirmed via analysis/r005-responsibility-inputs/libquarantine.dylib: SHA256 f5989c8f4afc9985e95795bf683679eed629af8b5ae288a0fb87317ba0e844f2; UUID A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3. Getter 0x18b320b00 returns the helper result unchanged. Helper sets selector 0xb6 at 0x18b320b94 and calls stub 0x18b321bc0 at 0x18b320b98; subsequent instructions only control optional output copying and do not overwrite w0 or assign errno/103 in this range. The authenticated stub jumps through GOT 0x1dfef8f88.

The exact imported provider remains unresolved. Wrapper evidence excludes a local synthetic 103 in the inspected range, but cannot identify no-match versus foreign precheck or matched-handler errno. Adjacent-version symbol names are not exact 22F82 provider evidence.

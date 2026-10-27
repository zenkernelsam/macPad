# D094 — layered acceptance reconciliation (2026-10-19)

Read-only reconciliation of existing evidence plus the D094 VNC frame. This
does not redo provider/F1 or reopen-freshness implementation work.

| Layer / question | Status | Evidence and limit |
|---|---|---|
| Decode / asset | **Accepted** | D037/D039 show the static wallpaper payload and visible PNG; the dynamic HEIC's black decode was handled at the asset producer. D094 frame shows rendered wallpaper again, but does not independently test the decoder. See `docs/evidence/devin-d071-frontend-20261019.md` §3. |
| Provider identity / actual load | **Accepted at isolated-probe boundary** | D071 backend §1 / R010 record merged ancestors, package 0.3.4, installed libmachook UUID and byte-identical callsite; provider probe reports `INSTALLED-ORIGINAL-CALLABLE`; frozen F1 contract probe is 9/9 PASS. D070/D071 record provider/libmachook mapped in later WS 10447 generation. This is not proof of F1 firing in live WindowServer. |
| Live hook execution | **Partial** | `CGXSYNTH` sender gate has live firing evidence. F1 direct-drawable route firing is still unobserved; D037's PAC/trampoline/desktop ALL PASS is not an F1-route trigger. D094 zero install-log matches are only missing witnesses because current diagnostic output is filtered; do not infer hook absence. No provider/F1 contract reimplementation is needed. |
| Producer / final composite | **Accepted, current generation** | D094 backend: WS PID 10447, same lstart, `ready`/`validated-final-composite`, seq 12384; paired pixel read brackets seq 12526→12530. This proves advancing producer state, not by itself visible pixels. |
| VNC framebuffer pixels | **Accepted, current generation** | [D094 PNG](devin-d094-frame-20261019.png): 1366×1024, 98.6064% sampled nonblack (`max(R,G,B)>8`), mean RGB 70.547/82.507/54.429; visually inspected Finder/menu bar/wallpaper. PID/lstart and state sequence bracket the capture. |
| MacWSHost / iPad panel pixels and interaction | **Panel witnessed at seq~1176; later panel freshness open; interaction partial** | D041 captured six 2732×2048 MacWSHost/iPad panel frames (`docs/evidence/devin-d041-input-boundary-20261019/`) with WS PID 10447 and state sequence 695→1176; D094 adds a separate VNC framebuffer frame with state 12526→12530. The composite stream remained ready/advancing, but that state file does not prove a later panel raster. D041 demonstrates exact-AppInput Finder interaction (Cmd-F and text) and restored the desktop. A repeat `macws_ipados_capture` correlated to a later sequence would close panel freshness; no new capture is authorized in this heartbeat. Global input/TCC remains open. |
| App reopen freshness | **Source contract accepted; runtime witness open** | Reuse `macwshostd/main.m`: snapshot prior metrics generation, send the exact reopen request, require `generation > previous` and a visible-window flag. The D094 frame does not exercise reopen. R016 and D092/D093 detail the accepted source contract. |
| Responsibility / TCC upstream cause | **Unresolved** | R005/R006: initial empty replies came from invalid XPC connection and fail-closed default, not a TCC policy verdict. R007/R008: compatible transport now returns dictionaries, but accessor-zero does not prove an explicit policy deny. D060 maps observed #103 to the last responsibility lookup, while registration absence vs provider/query resolution remains open. No local exact 22F82 dyld cache/fixups or matching T8103 kernelcache is available to close it. `macws-policy-snapshot-v3` only answers whether Quarantine is registered/slots populated at snapshot time; it does not identify which historical #103 arm fired. |
| Artificial frame wait vs service messages | **Not measured causally** | Source uses completion pacing and an interaction wake socket; that socket is not evidence of waking for the original service/Mach messages. Historical mutex contention is not current starvation evidence. No correlated request enqueue→dispatch→reply measurement exists in this WS generation. Existing `MACWS_MACH_MSG_ALL` is too broad and does not correlate one request; old R011 spindump is a different WS generation. D088/D090 sentinel is not implemented. A causal diagnostic needs dedicated instrumentation and rebuild/redeploy/WS restart; no restart-free measurement is established. |
| IconServices crash identity / trust / directories | **Exclusion audit accepted; upstream compiler candidate identified, production causality open** | D071 backend §2 / R005 identifies the stock chroot `iconservicesagent`, uid 0, trusted daemon and injected dependency; sibling `iconservicesd` stable as 240:240 with 0700 store; agent HOME cache directory absent. New offline audit: converted F11H AIR reserialization by the MacWS LLVM16 path emitted attribute wire ID 71 that the target LLVM reader rejects; isolated compiler replay reproduced `Unknown attribute kind (71)`. R031 canonical candidate changes the triple only and parses offline, but has not been replayed against the private compiler. R005 lacks exact production request/reply bytes, so connection to that crash remains a THEORY, not the confirmed root cause. See R014/R028/R031/R032 evidence. |

## Responsibility/TCC and frame-wait findings

The exact tccd disassembly (D060) localizes observed `#103` to the final
responsibility lookup after path validation. D096 confirms the target
SkyLight has no responsibility-registration imports/callers in that image;
however Ventura launchd/tccd implementations are absent locally, so no
caller in SkyLight/project sources does not prove the target PID has no
responsibility registration. The registration-vs-query-resolution split
remains open. Independently, the exact Quarantine stub's import provider
cannot be named from the sparse local dylib because its GOT target is outside
the extracted segments; matching Ventura cache/fixup records and target
T8103 kernelcache are absent. The `macws-policy-snapshot-v3` can establish
whether a Quarantine policy exists and has slots, but not why a historical
`#103` was returned; D060/D069/R014 preserve the three possible kernel arms.
See [D096 review](devin-d096-review-20261019.md), R013/R014 and
`devin-d069-policy-snapshot-independent-review-20261019.md`.

The frame-wait source audit found an interaction-driven wake path, but no
current measurement that service messages wait behind the artificial frame
pace. The screenshot/advancing sequence does not provide message latency.
Do not adapt the wait to Mach-port polling until a correlated measurement
shows a delay.

IconServices now has a concrete upstream compiler-failure candidate: the
LLVM16 AIR round-trip can encode attribute ID71 that the exact reader rejects.
R031's corrected candidate is only offline-validated; production request
lineage and private-endpoint replay are still missing. Do not call this a
production crash fix until the exact request or an authorized controlled
replay links it to the R005 F11H abort.

## Authorization boundary

The user-authorized D091/D094 SSH access covered bounded reads and a single
frame capture. It does not authorize the separate named
`macws-policy-snapshot-v3` kernel snapshot, nor any frame-pacing source
change, build, deployment, trustcache operation or WindowServer restart.
R031's earlier conditional replay permission was superseded by the worker
switch and the later D033 instruction to park the candidate; it is not
currently authorized. Any replay must be explicitly reauthorized, restore an
isolated endpoint, and preserve the no-hook confirmation gate. A successful
private replay alone would still not prove the R005 production crash cause.

## D107 kernel inventory correction (2026-10-19)

Earlier statements that the target kernelcache is absent are superseded by
D107: analysis/kernelcache_16.3_T8112.img4 has D064's device-file SHA256
78959dd973ed3584532d51424274e183d843a2add801bd017f37d64e7b62dcc2 and
its manifest identifies T8103 despite the basename. The local raw image
has SHA256 6673ce2889604cbfb7a9dc938000be0c48486874c9aebae0cdd4870c514f2b38
and matching kernel build text; raw-to-IMG4 decompressed-payload byte linkage
is not yet verified (D108 tasked). Live kernel patches remain a separate limit.
Ventura cache/fixup records for the unresolved libquarantine GOT import are
still missing. All three static errno103 return arms remain possible.

mac_late rejects only NOTLATE policies; unload depends on UNLOADOK. A live
snapshot establishes policy state at its instant. Do not assert whole-boot
absence merely from no literal name in a file or no observed kext directory;
dynamic naming and runtime loader history are additional unknowns.

## D108 payload provenance closure

D108 decoded pinned IMG4 IM4P LZFSE payload with the host system
compression_decode_buffer API. The 80,052,224-byte output and existing
kc_raw both hash to 6673ce2889604cbfb7a9dc938000be0c48486874c9aebae0cdd4870c514f2b38;
coordinator independently rehashed both files. This closes the previous
IMG4-to-raw-image byte-linkage gap. It does not prove live mapped kernel
bytes are unpatched or that a Quarantine policy is registered. Snapshot
state is instant state; historical registration/unload remains separate.

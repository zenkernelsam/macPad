# devin-048-backend-result — frozen candidate + isolated probe

Packet: `devin-048-backend-task.md`. Scope: frozen-source validation +
disposable arm64e helper probe. No production deploy, no WS restart,
no session interruption, no markers, no 4th hook. Desktop untouched:
`ready`, seq 2383→2443, watchdog 9845 alive, zero sentinels pre/post.

## Frozen identities (D047 reviewed stale `.c d43c65d9`)

Delta reviewed→final `6641d9df` = one line: `**env->audit_slot`
double-deref fix (cell address vs contents — caught by the same
D046 host test; everything else byte-identical).

- h `a7c8438b…` / c `6641d9df…` / mac_hooks.m `7321d92c…` /
  test `39d6791e…` / fat dylib `fddc64c1…` (arm64 `5B9F5C19`,
  arm64e `353600B3`) / probe src `f435057a…` / probe bin `f00fc48c…`
- Frozen host suite re-run: **ALL PASS**.

## Isolated probe — 15/15 PASS on device

`misc/macws_sender_obs_probe.c`, arm64e iOS build, `-DMACWS_SENDER_OBS_
TEST` on the probe compile only (production object macro-free per
build log). Real `*_invoke` bodies + mock originals; nested fake gate
original calls disp-invoke exactly as the real gate calls the hooked
dispatcher — PAC fn-ptr chain verified end-to-end. Packet-owned
`/tmp/macws_d048_probe_*` paths, self-cleaned.

Verified: default-off (orig runs, errno intact, zero captures), armed
nested chain (shared invocation id, CHAIN_ROOT gate-only, verbatim
144B fields, fn-ptr identity), errno both directions over clobbering
hooks, 0600 regular log, disarm ceases capture while orig still runs.

**Ordering note for live analysis:** nested records emit inner-first —
live chains arrive impl→disp→gate sharing one invocation id.

## Trustcache findings (admissions recorded)

- `/tmp` execs die SIGKILL even adhoc-signed + cdhash-admitted
  (`jbctl trustcache add` attempted `331fa007…` — absent from
  `trustcache info` afterward; no residue). `macws_boot_trust.py`
  only restores prior registrations.
- `/var/jb/tmp` is path-trusted — identical binary runs after
  `ldid -S` alone. Disposable helpers belong under `/var/jb`.
- Cleanup verified: binaries + sentinel/log paths removed; no
  background jobs. Endpoint `sshpass -e` + transient `SSHPASS` only.

## Deployment plan — produced, NOT executed

§4 of the evidence doc: freeze→stage (sign+readd)→atomic publish to
NEW inode (per-vnode signature cache rule)→sentinel arm in chroot
only→watchdog-owned generation→ONE bounded `SLSPostEventRecord`→match
discrimination matrix→disarm→verify restored pixels. Budget = one
bounded post + read + disarm. `raw1` remains ambiguous — **3 hooks
cannot prove explicit policy**; 4th TCC-image site stays unauthorized
frontend design.

`docs/evidence/devin-d048-isolated-observer-validation-20261019.md`

(handover to codex now)

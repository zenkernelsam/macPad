# devin-055-frontend — namespace-control & errno-provenance audit RESULT

**Doc:** `docs/evidence/devin-d055-namespace-independent-review-20261019.md`
**Reviewed:** `analysis/devin-d054/macws_d054_pathprobe.c` `d8245b71`,
D054 backend evidence doc + result (raw outputs verbatim-checked).
**Device/backend/Git:** untouched; D055 backend artifacts pending —
listed as missing inputs, no polling.

## D054 helper ABI/hygiene — verified

errno reset-then-captured, rc vs errno separated, buf only when rc>0;
`weak_import`+direct calls (no dlsym'd fptrs — PAC-correct);
`task_for_pid` denial recorded missing, never synthesized; pid→path
queried before token acquisition. Matches tccd `0x4b9d8` ABI and the
libproc source contract.

## Reconciliations (unresolved, not both-proven)

- macwshostd 897 native-caller ENOENT: consistent with backend's
  dead/replaced-vnode reading (stale namecache → global lookup fails),
  but not proven — any stale-name→missing-path condition shares the
  errno. Kept as its own class.
- My "ENOENT⇒caller-context vnode_lookup" map is xnu-**8792.81.2**
  source vs device **8792.82.2** — RE-informed THEORY, not verified on
  running kernel text.

## Mechanism now supported

Self-call resolves `/var/jb`-exec'd vnodes to the global
`/private/preboot/…` name (vnode is nameable); `/private/preboot`
absent inside the chroot; tccd is chrooted. ENOENT = caller-context
lookup of an already-resolved global path.

**Forwarded warning for backend's next experiment:** resolved names are
global — a binary *inside* `/var/mnt/rootfs` resolves to
`/var/mnt/rootfs/tmp/X`, which under the chroot double-roots to
`rootfs/var/mnt/rootfs/tmp/X` → predicted ENOENT anyway. The
chroot-resident trusted-helper send may fail stage-2 regardless;
worth knowing before spending the trust admission. No path-alias fix
proposed (existence-only lookup, vnode never compared — an alias would
be illegitimate).

## D055 same-process A/B — reviewed, sharpened

fork+chroot child is the right shape (no second exec/admission).
Sharpest form: child queries the **parent's live pid** — one identity,
two caller contexts — plus self and platform. Predicted matrix:

- parent/native: path resolved; child/chrooted same pid: ENOENT →
  caller-namespace proven for this API/build
- platform pid under chroot **may still resolve** (`/usr/libexec/…`
  exists in rootfs — existence-only lookup) — record, don't call success
- stale token → ESRCH negative control
- residual: kernel-leg vs tccd-post-call-stat errno attribution stays a
  labeled sub-THEORY

## Bounds

errno distinction drawn (API vs userspace-validation vs stale-after-
success); pidversion race → ESRCH class not ENOENT; no universal
all-senders claim; `raw1`/`impl_rc=0` still collapsed; observer
undeployed; desktop preserved (seq 3863→3895 per backend, WS 10447).

(handover to codex now)

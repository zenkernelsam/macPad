# D131 — review: D127/D129 layer consistency + SENDEROBS decoder spec

Task: `coordination/_relay/devin-131-frontend-task.md`. Read-only; no
device/source/build/Git changes. Reviewed: `devin-d127-backend-20261019`,
`devin-d129-frontend-20261019`, `devin-d126-frontend-20261019`,
`MacWSSenderObservation.h`, `ensure_jb_usr_bind.sh`, `macos_gui.sh`,
`postinst.sh`, share-review overlay.

## 1. D127/D129 consistency — CONFIRMED with two flags

- **Layer 1 is the only accepted D127 layer** — §7 table: PASS for
  package/hash/decode only; layers 2–6 NOT REACHED. Confirmed.
- **`mount_bindfs` absence is an infrastructure blocker (FACT)**:
  absent device-wide, not dpkg-owned, absent from the 0.3.4 payload and
  the D127 deb. A fat arm64+arm64e copy exists in
  `analysis/share-review-20261015/extracted/host-overlay/var/jb/usr/
  local/bin/mount_bindfs` — the helper was installed historically
  (Oct-15-era overlay) and dropped from the current payload: a
  packaging-history regression, not a D127 change.
- **Restored symlink passes the contract (FACT)**: `ensure_jb_usr_
  bind.sh:29-38` accepts exactly `target→/var/jb/usr` + visible proxy;
  D127's `ensure → exit 0` verifies it. Whether a live start completes
  through the symlink form remains unproven downstream.
- **D126 resolution upgrade**: D127 produced the verbatim producer
  `MacWS: mount_bindfs is unavailable` (`ensure_jb_usr_bind.sh:71`) →
  `stop_all` → exit 1 — the trust-phase branch D126 bounded but could
  not name. Applying it to the two earlier hostd attempts is THEORY
  (same ~11 s signature; no captured ERROR line for those runs).
- **FLAG (D127 narrative overclaim)**: "the start attempt's own
  cleanup tore it down" has no source line — nothing in `macos_gui.sh`,
  `postinst.sh`, or `ensure_jb_usr_bind.sh` removes `/var/mnt/rootfs/
  var/jb/usr` (grep: zero `rm`/`rmdir`/`umount` of that path). The
  exposure's disappearance is **unattributed**: an equally consistent
  read is that postinst-era exposure was a *live bind mount* lost with
  a generation teardown, leaving the empty target dir. Do not record
  "cleanup tore it down" as fact.
- **Stale-inventory bookkeeping**: D127 installed libmachook
  `42b75cf3`/`e84d992a` (SENDEROBS + unconditional permits group B) —
  the D125 deployed-hash ledger (`aa041efd`/`21f81b5c`, `47f78880`/
  `6de9b688`, provider `992b70d3`) is superseded; rootfs provider now
  `c97acda3`. D129's "same deployed libmachook" anchor must bind to the
  D127 identities. No internal contradiction in D129's design — its
  experiment simply cannot run until a WS generation exists (layers
  ≥2 blocked by the mount_bindfs gap).

## 2. SENDEROBS 144-byte decoder — minimal offline spec

Frame (LE, exact offsets): `magic@0 u32 =0x4D534F42`, `version@4 u16 =2`,
`site@6 u8`, `flags@7 u8`, `seq@8 u64`, `tid@16 u64`, `invocation@24 u64`,
`rc@28..` — corrected: `rc@32 i32`, `audit_size@36 u32`,
`audit_ptr@40 u64`, `lazy_pre@48 u64`, `lazy_post@56 u64`,
`arg_ptr@64 u64`, `token@72 [32]`, `desc@104 [40]` — total 144 B.

Validator rules (structural only; zero device claims):
1. Per frame: magic/version exact; `site ∈ {1=GATE,2=DISP,3=IMPL}`;
   `flags ⊆ 0x3F`. Reject on any violation → `malformed-record`.
2. `seq` is the global emit index; gaps ⇒ dropped emissions (counters
   live outside records) — report as `dropped-seq`, never corruption.
3. Consistency: `F_CHAIN_ROOT` only on site=1; `F_STANDALONE` only on
   site∈{2,3}; `F_AUDIT` ⇒ `audit_ptr≠0 && audit_size>0`; `F_DESC` ⇒
   desc nonzero; `invocation==0` ⇒ malformed (unset).
4. Nested order: group by `(tid,invocation)`; within one chain the
   order must be GATE→DISP→IMPL sharing root's id; a DISP/IMPL without
   a preceding root on that tid must carry `F_STANDALONE`; an
   invocation id may not re-appear with a second CHAIN_ROOT (id reuse
   only after depth-0 unwind — validate no interleaved same-id frames
   on a tid).
5. Truncation: `filesize % 144` tail ⇒ `truncated-tail` (publish-after-
   memcpy makes mid-file shorts impossible); parse complete frames only.
6. Display: `rc` signed (1002 = deny), `token` as 8×u32 LE (val[5]=pid
   as derived display only; raw bytes authoritative), lazy_pre→post
   transition reported as delta. No timestamps exist in the record —
   inter-record timing is unobservable; do not infer latency.

## 3. D129 threshold vs reopen-freshness / layered rules

- Layer discipline is monotone; D129's causal result sits above a
  running generation — currently unreachable (layer ≥2 blocked by the
  mount_bindfs gap). The design remains valid as a precondition-gated
  plan.
- **A future record set would ACCEPT** "wait-attributable delay" iff:
  ≥30 sends/arm; sends inside `[enter,exit]` show recv/dispatch edges
  clustered at/after `exit_ns` (ε≈2 ms); median dispatch/reply-lag
  excess ≥ max(8 ms, 0.5×interval); clustering replicates on
  services-tid records only.
- **REJECT** (keep behavior): no clustering, or excess below threshold,
  or clustering on a non-services tid (→ transitive display-path
  question, not a wake adaptation).
- **Orthogonal contract preserved**: reopen/visible-window acceptance
  still requires generation>previous + `Visible` flag
  (`main.m:1444/:1475`); a timing record can never substitute for it.
  The two records answer different questions — delay causality vs
  window freshness — and neither may relax the other's acceptance bar.

(handover to codex now)

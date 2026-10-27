# D075 — audit of the D074 operator runbook

Offline review only. The runbook remains **NOT APPROVED**; this audit
lists concrete corrections required before it can even be *submitted*
for authorization. Nothing here executes commands.

## C1 — Script is not pinned (blocking defect)

The runbook pipes `run_snapshot.py` to `python3 -` but never defines
the file's content or hash. An "exact packet" is unverifiable without
it — the authorized packet must embed the script **inline with a
sha256**, so reviewers sign off on bytes, not prose. Every other
D-series packet that ran helpers pinned hashes; this one pins none.

## C2 — Identity-verification reads are outside the authorized read set

The D069 task's read set is literal: *"Read only mac_policy_list header
and bounded entries."* The runbook's verification does two extra kernel
reads (`kread64(base)` magic + `kread64(version_va)` string). These are
legitimate and required for fail-closed identity — but they must be
**declared** in the packet as the "identity-verification read set"
(exactly: 8 bytes at kernbase + ≤112 bytes at the `version[]` VA), or
the run self-violates its own contract on the first check.

## C3 — Init call not gated; sysinfo type not gated

`jbclient_initialize_primitives()`'s return is printed but ignored, and
the sysinfo call's rc/dict-type check isn't listed as a gate before
key extraction. The fail-closed chain must be: init rc → physrw rc →
sysinfo rc + `xpc_get_type==dict` → staticBase/base/magic/version —
in that order, each emitting `STOP <name>`.

## C4 — Post-witness drops `lstart`

The pre-witness captures `lstart`; the post-check uses
`ps -axo pid,comm` only. A reused pid across a restart would pass a
pid-only comparison — the post-check must capture `lstart` and require
**equality**, not just same-pid.

## C5 — `$SSH` command-in-variable fragility

`SSH="sshpass -e ssh …"` expanded unquoted works only while no var
contains spaces/metachars and depends on the calling shell. Concrete
fix: spell out the full command per step
(`sshpass -e ssh -p "$MACWS_DEVICE_PORT" -o … "$MACWS_DEVICE" 'cmd'`)
or define a shell function with quoted args. Also add
`-o ConnectTimeout=8` — a silent hang is a worse failure mode than a
rejection.

## C6 — `SSHPASS` export line lands in shell history

`export SSHPASS="<…>"` as written gets typed → history file. Fix:
`read -s SSHPASS; export SSHPASS` (or `read -s` equivalent), and state
the password may not be pasted into any file/doc/transcript.

## C7 — Cleanup glob-anchors wrong target

The `find` pattern `-name "*snap*" -o -name "*run_*"` guards names that
this packet doesn't create (the runner goes over stdin — zero device
files). Cleanup should instead assert **zero new files**: capture a
`find /var/jb/tmp /tmp -maxdepth 1 -newer <marker>` (or explicit
baseline listing) before and after, and diff. Pattern-matching names
can silently pass over a stray file with an unexpected name — the
D069 glob-abort lesson applies to assertions, not just `rm`.

## C8 — seq-delta semantics unstated

`seq_delta` is recorded but never interpreted. Specify: `state=ready` +
same WS pid/lstart is the health predicate; delta is informational only
(the producer writes per frame, so a healthy generation advances —
delta=0 with same pid is acceptable data, delta<0 impossible).

## C9 — `python3` resolution unpinned

`python3 -` depends on remote PATH. Pin `/var/jb/usr/bin/python3`
explicitly (verified present, trusted, carries the procursus
entitlements the arm sequence needs).

## C10 — minor: remote command quoting

`grep -E "WindowServer|tccd"` inside the single-quoted remote string is
fine; keep it but add `-o UserKnownHostsFile=/dev/null` to the ssh
options so a rotated host key can't turn the run into an interactive
prompt (which would hang, not fail).

## Consistency with AGENTS.md

- Placeholders used correctly; no credentials/hostnames in file ✓.
- No source/build/Git change ✓. "NOT APPROVED" marker present ✓.
- The runbook is honest about what it can't prove ✓.
- Missing vs AGENTS.md spirit: the packet must state its **hash
  manifest** — script sha256 (C1) plus the existing verified constants
  (list VA, version VA, ops offsets) — so authorization binds to exact
  bytes. Folded into C1.

## Disposition

All corrections are mechanical. After C1–C10 the packet is internally
consistent and contract-faithful — still requiring explicit user
authorization before any execution.

# D074 — operator runbook: one bounded read-only `mac_policy_list` snapshot

> **STATUS: NOT APPROVED FOR EXECUTION.** This runbook is a prepared
> specification only. It may be executed only after the user explicitly
> authorizes *this exact packet* by name. Until then it is documentation.
> Any run that deviates from this file is out of scope and inadmissible
> (D069 precedent).

Scope of the run: **one** bounded, read-only kernel-memory snapshot of
`mac_policy_list` via existing Dopamine primitives. It answers exactly
one question — *is a `Quarantine`-named MAC policy registered at
snapshot instant* — bounded by the D069-frontend acceptance contract.

## Hard prohibitions (verbatim)

- No `__mac_syscall` / MAC syscalls / TCC requests / setters / policy
  registration.
- No kernel or file writes on the device except staging/removal of the
  run script itself.
- **No trustcache mutation** — `jbctl trustcache add|clear` forbidden.
  If the run would need a new signed binary admitted, STOP (§7).
- No restart of any service; no observer deployment; desktop/watchdog
  untouched.
- No raw KASLR values (kernbase, slide, `entries`, `conf`, `ops`,
  name pointers) in any saved transcript or doc.
- No argv credentials — **ever** (D069 recorded violation).
- No unrelated reads: header + bounded entries + names only.

## Credentials

```sh
export MACWS_DEVICE="<authorized-device>"        # e.g. root@<ip>
export MACWS_DEVICE_PORT="<authorized-port>"
export SSHPASS="<transient-password>"            # env only, never argv
SSH="sshpass -e ssh -p $MACWS_DEVICE_PORT \
     -o StrictHostKeyChecking=no \
     -o PreferredAuthentications=password -o PubkeyAuthentication=no \
     $MACWS_DEVICE"
```

The password string must appear nowhere in argv, docs, or transcripts.

## Step 0 — pre-read generation witness (required)

```sh
$SSH 'ps -axo pid,lstart,comm | grep -E "WindowServer|tccd";
      cat /var/mnt/rootfs/private/tmp/macws_final_composite.state'
```

Record: WS pid + lstart, both tccd pids, `state`/`sequence`. The run
aborts if the generation is not `ready`.

## Step 1 — stage the runner (no new executable)

Use the already-trusted procursus python3 — **zero binaries, zero
trustcache change**. The script arrives over the SSH channel (stdin),
leaving no file:

```sh
cat run_snapshot.py | $SSH 'python3 -'
```

`run_snapshot.py` does exactly, in order:

1. `ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")`
2. `jbclient_initialize_primitives()`
3. `jbclient_root_get_physrw()` → **fail-closed**: rc≠0 → print
   `STOP physrw-unarmed` and exit (nothing else is read or printed).
4. `jbclient_root_get_sysinfo(&dict)` → XPC dict.
   **Fail-closed checks**, each printed by name only:
   - `kernelConstant.staticBase == 0xfffffe0007004000`
   - `kernelConstant.base == staticBase + kernelConstant.slide`
   - `kread64(base) & 0xffffffff == 0xfeedfacf`   (MH_MAGIC_64)
   - `kread64(0xfffffe0007e95ee5 + slide)` first 40 bytes ==
     `Darwin Kernel Version 22.3.0: Wed Jan  4`  (image-verified
     `version[]` static VA)
   Any failure → `STOP <check-name>`; nothing else is emitted.
5. Read `mac_policy_list` at `0xfffffe00079e1750 + slide`:
   - header double-read; any field differs → `STOP torn-header`
   - `numloaded≤max≤0x400`, `maxindex≤0x40`, `entries` low56-nonzero +
     kernel-range — else `STOP bad-header`
   - entries `i < min(maxindex+1, 0x40)`: read `mpc*` (range-checked),
     `mpc_name` (≤0x80, printable, NUL before cap), `mpc_ops+0x388`
     and `+0x3a8` presence via `& 0x00ffffffffffffff`
   - any range/printable/NUL failure → `STOP entry-<i>`
6. Print the sanitized report (below) and exit.

## Exact sanitized output contract

Only this format may be emitted/saved — slide-relative or booleans,
no raw kernel addresses:

```
kident: staticBase-ok base-ok magic-ok version-ok
hdr: numloaded=<n> max=<n> maxindex=<n> staticmax=<n> chunks=<n> freehint=<n> entries_off=<va-slide>
[<i>] name="<name>" ops=<set|null> precheck=<0|1> syscall=<0|1>
post: ws_same=<0|1> tccd_same=<0|1> seq_delta=<n> state=<state>
```

## Step 2 — post-read witness + cleanup

```sh
$SSH 'cat /var/mnt/rootfs/private/tmp/macws_final_composite.state;
      ps -axo pid,comm | grep -E "WindowServer|tccd";
      find /var/jb/tmp /tmp -maxdepth 1 \( -name "*snap*" -o -name "*run_*" \)'
```

Required for acceptance: identical WS/tccd pids, `state=ready`, seq
still advancing, `find` returns nothing (bounded cleanup — **no globs**;
D069's glob-abort bug is the recorded precedent), no owned process
remains.

## Stop conditions (any → halt immediately, emit check name only)

- Any Step-0/1 precondition fails; `get_physrw` rc≠0; any identity
  check fails; torn header; bounds/pointer/name violation.
- **Trustcache stop**: if `kread64` cannot be armed under python3's
  entitlements, the only alternative is a signed helper whose cdhash
  admission is a trustcache mutation → `STOP missing-authorization`.
  Never improvise a workaround.

## Interpretation bounds (recorded with results)

- `Quarantine` absent ⇒ no handler arm at snapshot instant;
  with `mac_late=1`, closed for this boot epoch. Does **not** identify
  the historical #103 producer (precheck arm stays open; Sandbox's
  `ops+0x388` is set in-image).
- `Quarantine` present + `ops+0x3a8` ⇒ handler possible; does not prove
  dispatch, selector semantics, or return values.
- Either way: point-in-time; userspace legs (does denial reach TCC)
  belong to the separate frontend observation packet.

## Provenance

Built from: D072 approval packet, D073 go/no-go memo, D069-frontend
acceptance contract (A1–A8 + sanitization), D069 backend violations
record, D067/D068 verified offsets (`0xfffffe00079e1750` list,
`+0x388`/`+0x3a8` slots, `version[]` at `0xfffffe0007e95ee5`), and the
D069-established arming mechanics (`get_physrw` → `kread64`,
procursus-style entitlements, `/var/jb` path trust, XPC sysinfo
`kernelConstant.base`).

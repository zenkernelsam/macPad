# D076 — operator runbook v2: bounded read-only `mac_policy_list` snapshot

> **STATUS: NOT APPROVED FOR EXECUTION.** This is a prepared
> specification only. It may be executed only after the user explicitly
> authorizes *this exact packet* — identified by the runner hash below.
> Any deviation makes the run out-of-scope and inadmissible (D069
> precedent). This revision applies the ten D075 corrections.

Scope: **one** bounded read-only snapshot answering only *whether a
`Quarantine`-named MAC policy is registered at snapshot instant*.

## Hash manifest (authorization binds to these bytes)

| Item | Value |
|---|---|
| Runner script (below) | sha256 `96e47c3b530684d8af1243c19faa34bce351a4d417c868174a26ba705b0f5da5` over the fenced content **plus one trailing newline** (i.e., `shasum -a256` of the saved file) |
| `mac_policy_list` static VA | `0xfffffe00079e1750` |
| `version[]` static VA | `0xfffffe0007e95ee5` |
| Kernel static base | `0xfffffe0007004000` |
| Ops slots | precheck `+0x388`, policy_syscall `+0x3a8` |
| Kernel image UUID | `F52569DD-064E-D996-B066-22EBCF95FF23` |
| Device | iPad13,11 T8103, kernel `xnu-8792.82.2~1/RELEASE_ARM64_T8103` |

## Prohibitions (verbatim)

No MAC/`__mac_syscall`/TCC/setters/registration; no kernel or device
file writes (script arrives over stdin — nothing is staged); **no
trustcache mutation** — if execution would need it, STOP
(`missing-authorization`); no restarts; no observer deployment; no raw
KASLR values in any emitted/saved line; no argv credentials; no reads
outside the declared read set.

## Authorized read set (declared, per C2)

- Identity verification: 8 bytes at `kernelConstant.base` + 112 bytes
  at `version[]` (14 qwords).
- Snapshot: `mac_policy_list` header ×2 (56 bytes each),
  ≤ `0x40` entries (`mpc*` + `conf` + name ≤0x80 + `ops` +2 slots).
- Nothing else.

## Credentials (C6)

```sh
read -s SSHPASS; export SSHPASS        # typed invisibly; never in argv/history/files
export MACWS_DEVICE="root@<authorized-device>"
export MACWS_DEVICE_PORT="<authorized-port>"
```

## Step 0 — pre-read generation witness (C4)

```sh
sshpass -e ssh -p "$MACWS_DEVICE_PORT" \
  -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no \
  "$MACWS_DEVICE" \
  'ps -axo pid,lstart,comm | grep -E "WindowServer|tccd";
   cat /var/mnt/rootfs/private/tmp/macws_final_composite.state;
   find /var/jb/tmp /tmp -maxdepth 1 -type f | sort'
```

Capture: WS pid+**lstart**, tccd pids, `state`/`sequence`, and the
**baseline file list** (for post-run diff — C7). Abort unless
`state=ready`.

## Step 1 — run the pinned runner (C5, C9)

Verify local hash first, then stream it:

```sh
shasum -a256 run_snapshot.py   # must equal the manifest hash
cat run_snapshot.py | sshpass -e ssh -p "$MACWS_DEVICE_PORT" \
  -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no \
  "$MACWS_DEVICE" '/var/jb/usr/bin/python3 -'
```

Fail-closed order (each emits only `STOP <name>`): `sym-*` →
`init-prims` → `physrw-unarmed` → `sysinfo` → `sysinfo-type` →
`base-slide` → `magic` → `version` → `torn-header` → `bad-header` →
`entry-*`/`nameptr-*`/`name-*`/`name-nul-*`/`ops-*`.

## Runner script (pinned bytes — the hash above covers this exactly)

```python
# run_snapshot.py — D076 pinned runner: ONE bounded read-only
# mac_policy_list snapshot via existing Dopamine libjailbreak primitives.
# Reads: kernbase magic (8B) + version[] (112B) for identity, then
# mac_policy_list header (2x) + <=0x40 entries + names + 2 ops slots.
# Emits ONLY sanitized output. Fail-closed on every check.
import ctypes, struct, sys

def stop(name):
    print("STOP %s" % name); sys.exit(9)

lib = ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")
def sym(n):
    f = getattr(lib, n, None)
    if f is None: stop("sym-%s" % n)
    return f
kread64 = sym("kread64"); kread64.restype = ctypes.c_uint64; kread64.argtypes = [ctypes.c_uint64]
if sym("jbclient_initialize_primitives")() != 0: stop("init-prims")
gp = sym("jbclient_root_get_physrw"); gp.restype = ctypes.c_int
if gp() != 0: stop("physrw-unarmed")

xpc = ctypes.CDLL("libxpc.dylib")
xpc.xpc_get_type.restype = ctypes.c_void_p; xpc.xpc_get_type.argtypes = [ctypes.c_void_p]
xpc.xpc_dictionary_get_uint64.restype = ctypes.c_uint64
xpc.xpc_dictionary_get_uint64.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
DICT_T = ctypes.c_void_p.in_dll(xpc, "_xpc_type_dictionary").value

out = ctypes.c_void_p(0)
si = sym("jbclient_root_get_sysinfo"); si.restype = ctypes.c_int; si.argtypes = [ctypes.c_void_p]
if si(ctypes.byref(out)) != 0: stop("sysinfo")
o = out.value
if not o or xpc.xpc_get_type(o) != DICT_T: stop("sysinfo-type")
k = lambda key: xpc.xpc_dictionary_get_uint64(o, key)
base, slide, staticb = k(b"kernelConstant.base"), k(b"kernelConstant.slide"), k(b"kernelConstant.staticBase")
SB = 0xfffffe0007004000
if staticb != SB or not slide or base != SB + slide: stop("base-slide")
if (kread64(base) & 0xffffffff) != 0xfeedfacf: stop("magic")
want = b"Darwin Kernel Version 22.3.0: Wed Jan  4 21:25:19 PST 2023; root:xnu-8792.82.2~1/RELEASE_ARM64_T8103"
buf = b"".join(struct.pack("<Q", kread64(0xfffffe0007e95ee5 + slide + i*8)) for i in range(14))
if not buf.startswith(want): stop("version")
print("kident: staticBase-ok base-ok magic-ok version-ok")

KV = lambda v: (v >> 32) == 0xfffffe00          # live kernel VA range
lva = 0xfffffe00079e1750 + slide
k32 = lambda a: kread64(a) & 0xffffffff
h1 = [k32(lva + i*4) for i in range(6)] + [kread64(lva + 0x18)]
h2 = [k32(lva + i*4) for i in range(6)] + [kread64(lva + 0x18)]
if h1 != h2: stop("torn-header")
nl, mx, midx, smax, chunks, fhint, ent = h1
if nl > mx or mx > 0x400 or not KV(ent): stop("bad-header")
print("hdr: numloaded=%d max=%d maxindex=%d staticmax=%d chunks=%d freehint=%d entries_off=%#x"
      % (nl, mx, midx, smax, chunks, fhint, ent - base))
for i in range(min(midx + 1, 0x40)):
    e = kread64(ent + i*8)
    if not e: print("[%d] <empty>" % i); continue
    if not KV(e): stop("entry-%d" % i)
    np_ = kread64(e); ops = kread64(e + 0x20)
    nm = ""
    if np_:
        if not KV(np_): stop("nameptr-%d" % i)
        for j in range(0x80):
            w = kread64(np_ + (j & ~7)); c = (w >> (8*(j & 7))) & 0xff
            if c == 0: break
            if c < 32 or c > 126: stop("name-%d" % i)
            nm += chr(c)
        else: stop("name-nul-%d" % i)
    pre = sy = 0
    if ops:
        if not KV(ops): stop("ops-%d" % i)
        pre = 1 if (kread64(ops + 0x388) & 0x00ffffffffffffff) else 0
        sy  = 1 if (kread64(ops + 0x3a8) & 0x00ffffffffffffff) else 0
    print("[%d] name=%s ops=%s precheck=%d syscall=%d"
          % (i, '"%s"' % nm if nm else "<null>", "set" if ops else "null", pre, sy))
```

## Step 2 — post-read witness + cleanup (C4, C7, C8)

```sh
sshpass -e ssh -p "$MACWS_DEVICE_PORT" \
  -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no \
  "$MACWS_DEVICE" \
  'ps -axo pid,lstart,comm | grep -E "WindowServer|tccd";
   cat /var/mnt/rootfs/private/tmp/macws_final_composite.state;
   find /var/jb/tmp /tmp -maxdepth 1 -type f | sort'
```

Acceptance: WS/tccd **pid AND lstart identical** to Step 0;
`state=ready`; **post `find` output byte-identical to the baseline**
(diff, not pattern match). Sequence semantics (C8): the predicate is
same-generation + `ready`; `seq_delta` is informational — a healthy
producer advances it continuously, delta=0 with same pid is valid data.

## Stop conditions

Any `STOP <name>`; any Step-0 precondition failure; and explicitly:
if `physrw-unarmed` fires, the helper-binary fallback would require a
trustcache admission → the run ends `missing-authorization`. No
improvised workarounds.

## Interpretation bounds (record with results)

`Quarantine` absent ⇒ handler arm closed at snapshot instant (whole
boot epoch via `mac_late=1`); does not identify which arm produced
historic #103 (precheck-veto arm stays open). Present ⇒ dispatch arm
possible only. Userspace legs belong to the separate frontend packet.

## Provenance / corrections applied

D075 C1–C10: runner pinned inline + hash manifest; identity-read bytes
declared; all return/type checks gated; post-witness compares
pid+lstart; full quoted SSH commands with ConnectTimeout and
host-key isolation; `read -s` credential intake; baseline-diff cleanup;
sequence semantics defined; `/var/jb/usr/bin/python3` pinned. Verified
constants from D067/D068 (layout/offsets), D069 (arming mechanics:
`get_physrw` → `kread64`, XPC dict `kernelConstant.*`, procursus
entitlement path).

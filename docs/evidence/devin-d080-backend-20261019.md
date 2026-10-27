# D080 — operator runbook v3 (runner v3): bounded `mac_policy_list` snapshot

> **STATUS: NOT APPROVED FOR EXECUTION.** Third hash ⇒ third
> authorization cycle required; neither the D076 nor D078 hash covers
> this packet. Documentation only until the user authorizes it by name.

v3 applies the one required D079 fix; every other contract element is
unchanged from D076/D078 (credentials `read -s SSHPASS`/`sshpass -e`,
quoted ssh commands with `ConnectTimeout=8` +
`UserKnownHostsFile=/dev/null`, pid+lstart pre/post witnesses,
baseline `find` diff, seq semantics, pinned `/var/jb/usr/bin/python3`,
fail-closed STOP names, sanitized output, stop-on-trustcache-need).

## Hash manifest v3

| Item | Value |
|---|---|
| Runner script v3 (below) | sha256 `8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc` over fenced content + trailing newline |
| `mac_policy_list` static VA | `0xfffffe00079e1750` |
| `version[]` static VA | `0xfffffe0007e95ee5` |
| Kernel static base | `0xfffffe0007004000` |
| Ops slots | precheck `+0x388`, policy_syscall `+0x3a8` |
| Kernel image UUID | `F52569DD-064E-D996-B066-22EBCF95FF23` |

## v2 → v3 change (the only delta)

`DICT_T` is now the **address** of the `_xpc_type_dictionary` global —
what `xpc_get_type` returns (host-verified match on a live dictionary:
`get_type(d) == addressof(global)`), not the type object's first
qword. Without this the runner always halted at `STOP sysinfo-type`.

## Runner v3 (pinned bytes)

```python
# run_snapshot_v3.py — D080 pinned runner v3: ONE bounded read-only
# mac_policy_list snapshot via existing Dopamine libjailbreak primitives.
# v3 fix (D079): DICT_T is the ADDRESS of _xpc_type_dictionary (what
# xpc_get_type returns), not the type object's first qword.
# Reads: kernbase magic (8B) + version[] (112B) for identity, then
# mac_policy_list header (2x) + <=0x40 entries + names + 2 ops slots.
# Emits ONLY sanitized output. Fail-closed on every check.
import ctypes, struct, sys

def stop(name):
    print("STOP %s" % name); sys.exit(9)

try:
    lib = ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")
    xpc = ctypes.CDLL("/usr/lib/system/libxpc.dylib")
except OSError:
    stop("dlopen")
def sym(h, n):
    f = getattr(h, n, None)
    if f is None: stop("sym-%s" % n)
    return f
kread64 = sym(lib, "kread64"); kread64.restype = ctypes.c_uint64; kread64.argtypes = [ctypes.c_uint64]
kread32 = sym(lib, "kread32"); kread32.restype = ctypes.c_uint32; kread32.argtypes = [ctypes.c_uint64]
if sym(lib, "jbclient_initialize_primitives")() != 0: stop("init-prims")
gp = sym(lib, "jbclient_root_get_physrw"); gp.restype = ctypes.c_int
if gp() != 0: stop("physrw-unarmed")

xpc.xpc_get_type.restype = ctypes.c_void_p; xpc.xpc_get_type.argtypes = [ctypes.c_void_p]
xpc.xpc_dictionary_get_uint64.restype = ctypes.c_uint64
xpc.xpc_dictionary_get_uint64.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
DICT_T = ctypes.addressof(ctypes.c_void_p.in_dll(xpc, "_xpc_type_dictionary"))

out = ctypes.c_void_p(0)
si = sym(lib, "jbclient_root_get_sysinfo"); si.restype = ctypes.c_int; si.argtypes = [ctypes.c_void_p]
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
h1 = [kread32(lva + i*4) for i in range(6)] + [kread64(lva + 0x18)]
h2 = [kread32(lva + i*4) for i in range(6)] + [kread64(lva + 0x18)]
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

## Remaining defects / residual risks (carried into v3)

- Physrw arm under python's entitlements — still the single unverified
  runtime link; `STOP physrw-unarmed` is the correct terminal.
- `in_dll`/`getattr` failures raise exceptions rather than `STOP`
  names — cosmetic, emits nothing sensitive.
- `KV` range check is the kernel's known VA band only — adequate here;
  anything else stops cleanly.
- Unaligned `version[]` read tolerated by the physrw path
  (D069-empirical); noted so future reviewers don't re-flag it.

## Provenance

D076 runbook → D075 audit (10 corrections) → D078 v2 → D079 defect →
v3 (this). Constants from D067/D068; arming mechanics from D069
(`get_physrw` → `kread64`, XPC `kernelConstant.*`, procursus
entitlements, `/var/jb` trust). Host-verified DICT_T fix.

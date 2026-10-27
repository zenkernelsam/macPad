# D078 — operator runbook v2 (runner v2): bounded `mac_policy_list` snapshot

> **STATUS: NOT APPROVED FOR EXECUTION.** New authorization cycle
> required: the runner bytes changed (sha256 below differs from v1), so
> any authorization of the D076 hash does **not** cover this packet.
> This is documentation until the user authorizes this exact packet.

This revision applies both D077 defects; all other D075/D076 contract
elements are unchanged and incorporated by reference (credentials via
`read -s SSHPASS`/`sshpass -e`, full quoted ssh commands with
`ConnectTimeout=8` + `UserKnownHostsFile=/dev/null`, pre/post
pid+lstart witnesses, baseline `find` diff, seq semantics,
`/var/jb/usr/bin/python3`, fail-closed STOP names, sanitized output,
stop-on-trustcache-need).

## Hash manifest v2 (authorization binds to these bytes)

| Item | Value |
|---|---|
| Runner script v2 (below) | sha256 `c1c7783fd32f81bb8353c30420b6718453b862cac87e577faee2ba6df7b2a341` over the fenced content **plus one trailing newline** |
| `mac_policy_list` static VA | `0xfffffe00079e1750` |
| `version[]` static VA | `0xfffffe0007e95ee5` |
| Kernel static base | `0xfffffe0007004000` |
| Ops slots | precheck `+0x388`, policy_syscall `+0x3a8` |
| Kernel image UUID | `F52569DD-064E-D996-B066-22EBCF95FF23` |

## v1 → v2 changes

1. `ctypes.CDLL("libxpc.dylib")` →
   `ctypes.CDLL("/usr/lib/system/libxpc.dylib")` — absolute install
   path verified resolving on dyld-cache systems; `_xpc_type_dictionary`
   export confirmed present.
2. Six u32 header fields read via exported **`kread32`** (4-byte
   aligned offsets `+0x00…+0x14`); `kread64` retained only for
   8-byte-aligned reads (`entries` ptr, entry ptrs, conf fields,
   name qwords, ops slots). Matches the path that produced a valid
   header in the D069 C helper.
3. `dlopen` calls wrapped → `STOP dlopen` instead of bare traceback
   (D077 minor item).

## Authorized read set (unchanged)

8 B at kernbase + 112 B at `version[]` (identity) + header ×2 +
≤0x40 entries with names ≤0x80 and two ops slots. Nothing else; no
writes anywhere.

## Runner v2 (pinned bytes)

```python
# run_snapshot_v2.py — D078 pinned runner v2: ONE bounded read-only
# mac_policy_list snapshot via existing Dopamine libjailbreak primitives.
# v2 fixes (D077): libxpc loaded by absolute install path; kread32 for
# all 4-byte-aligned fields; kread64 only on naturally aligned addrs.
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
DICT_T = ctypes.c_void_p.in_dll(xpc, "_xpc_type_dictionary").value

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

## Execution steps (unchanged from D076, verbatim contract)

Pre-witness (`ps pid,lstart` + state file + baseline `find`), then
`shasum -a256` local check → stdin-pipe to
`/var/jb/usr/bin/python3 -` via `sshpass -e` with quoted full command,
then post-witness pid+lstart equality + `ready` + identical `find`
baseline. Interpretation bounds unchanged (snapshot answers only the
registration arm; precheck veto stays open; userspace legs belong to
the frontend packet).

## Remaining defects / residual risks

- **Physrw arm under python's entitlement set** — still unverified; the
  runner correctly `STOP physrw-unarmed` if jailbreakd refuses. That is
  the packet's only remaining runtime unknown; it is fail-closed.
- Uncaught `ctypes` exceptions mid-walk (e.g., a kread that
  long-faults) would produce a traceback instead of `STOP` — still
  emits nothing sensitive (local paths only); accepted residual.
- `KV` accepts any `0xfffffe00_xxxxxxxx` — adequate for this kernel's
  layout (all image data and conf/ops/name objects live in that range);
  a pointer into a different kernel submap would stop cleanly.
- `entries_off` leaks the list-array's kernbase-relative offset —
  allowed by the sanitization contract (slide-relative).

## Provenance

D076 runbook, D077 audit findings (both applied), libxpc absolute-path
resolution verified on host, `kread32` path = the empirically-working
D069 C helper read, manifest verified by re-extraction.

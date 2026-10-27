# D079 — static ABI review of runner v2 ctypes declarations

Offline only. Checked every ctypes declaration in
`devin-d078-backend-20261019.md` against the saved
`libjailbreak.dylib` (sha256 `e249e475…`) export table and the D069 C
helper's verified usage. One **new fail-closed defect found**; runner
v2 as pinned cannot complete a run.

## Verdict: NOT clean — defect in the XPC type check

```python
DICT_T = ctypes.c_void_p.in_dll(xpc, "_xpc_type_dictionary").value
```

`xpc_get_type()` returns a pointer **to** the type object — i.e., the
address of the `_xpc_type_dictionary` global. `.value` on `in_dll`
reads the type object's *first qword* (its contents), not its address.
Verified live on host libxpc:

```
xpc_get_type(dict)          = 0x7ff85c374188
&_xpc_type_dictionary       = 0x7ff85c374188   (match)
(_xpc_type_dictionary).value= 0x7ff85c3741b0   (≠)
```

So `xpc_get_type(o) != DICT_T` is always true → the runner stops at
`STOP sysinfo-type` before any list read. Fail-closed — emits nothing
sensitive — but produces no snapshot.

**Required fix (v3)**: `DICT_T = ctypes.addressof(ctypes.c_void_p.in_dll(
xpc, "_xpc_type_dictionary"))` — compare addresses, not contents. New
bytes ⇒ new sha256 ⇒ new authorization cycle; this packet remains
not-approved either way.

## Checked and consistent

| Item | v2 declaration | Verified against | Result |
|---|---|---|---|
| `kread64` | `(u64)->c_uint64` | export `_kread64` both slices; C helper `uint64_t(*)(uint64_t)` | ✓ |
| `kread32` | `(u64)->c_uint32` | export `_kread32` both slices; C helper `uint32_t(*)(uint64_t)` | ✓ |
| `jbclient_initialize_primitives` | `()->int` | `_jbclient_initialize_primitives` exported; D069 run printed `init=0` | ✓ |
| `jbclient_root_get_physrw` | `()->c_int` | `_jbclient_root_get_physrw` exported; D069 run `get_physrw=0` | ✓ |
| `jbclient_root_get_sysinfo` | `(void*)->c_int` | exported; D069 run `sysinfo rc=0 obj=dict` | ✓ |
| `xpc_get_type` | `(void*)->c_void_p` | returns type-object address (host-verified) | ✓ signature |
| `xpc_dictionary_get_uint64` | `(void*,char*)->u64` | standard libxpc signature | ✓ |
| `_xpc_type_dictionary` | symbol name | resolves via `in_dll` (host) | ✓ name, ✗ use (defect above) |
| `libxpc` path | `/usr/lib/system/libxpc.dylib` | resolves in dyld cache (host) | ✓ |
| `libjailbreak` path | `/var/jb/basebin/…` | exists on device (D069 pull) | ✓ |

## Alignment audit (read-by-read)

| Read | Alignment | Primitive | Verdict |
|---|---|---|---|
| `kread64(base)` | page-aligned | kread64 | ✓ |
| `version[]+i*8` | **unaligned** (…ee5) | kread64 | ✓ — D069 diag7 empirically returned the correct string at an unaligned VA, so the physrw path tolerates it |
| header u32s `+0…+0x14` | 4-aligned | **kread32** | ✓ correct choice |
| `entries` `+0x18` | 8-aligned | kread64 | ✓ |
| `entries[i]`/`conf`/`ops` slots | 8-aligned (kernel pointers) | kread64 | ✓ |
| name qwords `np+(j&~7)` | 8-aligned | kread64 | ✓ |
| `ops+0x388`/`+0x3a8` | 8-aligned | kread64 | ✓ |

Correction to D077: its alignment defect was precautionary — the
empirical read shows `kread64` tolerates unaligned addresses, so
switching to `kread32` for the u32 fields is *consistent and harmless*
rather than strictly required. No safety issue either way; keep it.

## Remaining fail-closed defects (running list for v3)

1. `DICT_T` `.value` vs address — **must fix** (this packet).
2. `ctypes.c_void_p.in_dll` failure raises `ValueError`, not `STOP` —
   cosmetic (nothing sensitive emitted); acceptable.
3. Physrw arm under python entitlements — runtime unknown, fail-closed
   `STOP physrw-unarmed`; the packet's only true runtime dependency.

## Provenance

Host-verified XPC type-pointer semantics; libjailbreak export table
(both slices); D069 C helper signatures + the working on-device read
sequence (diag7 unaligned read, kread32 header read).

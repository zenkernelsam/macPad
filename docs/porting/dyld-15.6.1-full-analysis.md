# dyld 15.6.1 arm64e — complete RE analysis

> Binary: `analysis/dyld_15.6.1_arm64e_thin` (arm64e slice, **imagebase 0**,
> 1240752 B, matches the rootfs-tar slice).
> Tool: IDA Pro 9.2 via `ida-pro-mcp`. All facts below are from `decompile`,
> `disasm`, `xrefs_to`, `func_query`, `search_structs`, `get_bytes` produced by
> IDA — no external Python/otool/strings was used.
> All addresses are **thin offsets** (the `sub_XXXXX` / load address shown by IDA).
> Reports only — no binary was modified.

---

## 0. Scope, method, and the pristine-vs-patched caveat

### 0.1 What this document answers

Every branch, path decision and argument construction inside the dyld shared-cache
loader: `loadDyldCache`, `reuseExistingCache`, `mapSplitCacheSystemWide`,
`preflightMainCacheFile` / `preflightSubCacheFile` / `preflightCacheFile`,
`mapSplitCachePrivate`, plus the `DyldSharedCache` / `DynamicRegion` accessors they
use, the complete syscall #536 argument layout, the full `dyld_cache_header` field
table, every env/flag decision point and every failure path.

### 0.2 Pristine vs patched — READ THIS BEFORE TRUSTING ANY BYTE

The IDB at `/Users/ciscohe/Desktop/macPad/analysis/dyld_15.6.1_arm64e_thin.i64`
is **effectively the pristine thin slice**. Verified by `get_bytes`:

| offset | IDB bytes | decodes to | patch ledger claim |
|---|---|---|---|
| `0x3538c` | `7c ca 51 b9` | `LDR W28,[X19,#0x1A8]` (original) | ledger says patched to `MOV W28,#1` — **not present in IDB** |
| `0x50dfc` | `08 f8 40 f9` | `LDR X8,[X0,#0x1F0]` (original) | ledger says patched to `mov x8,xzr` — **not present** |
| `0x76270` | `01 10 00 d4  c0 03 5f d6` | `SVC #0x80; RET` (original) | ledger says patched to `mov x0,xzr` — **not present** |
| `0x35754` | `6d 0d 00 14` | `B loc_38D08` | ledger says "B → cave 0x38d08" — this **equals** the value in the IDB |

The last row is the important subtlety: `0x35754` is an **unconditional `B` that
already targets the cold block at `0x38D08` in the pristine binary** (IDA resolved
the label `loc_38D08`). So the ledger's "tramp written into a NOP cave" was in fact
re-discovering pre-existing dyld code (see §9.3). Treat `0x38D08` as **genuine dyld
code, not a scratch cave**.

Consequence for the reader: instruction *semantics* reported here (branch structure,
register flow, arg construction) are trustworthy everywhere. Only the four ledger
sites above have any ambiguity, and all four are documented explicitly. The lead
then maps thin→fat with the arm64e slice offset from the live fat header.

---

## 1. `loadDyldCache` decision tree — `IDA 0x34240`

Signature (from the C++ mangling used by the single caller
`dyld4::SyscallDelegate::getDyldCache` @ `IDA 0x2fe34`):

```c
bool dyld3::loadDyldCache(const SharedCacheOptions& options,   // x0 = a1
                          SharedCacheLoadInfo*      results);  // x1 = a2
```

Full pseudocode (disasm-verified, `IDA 0x34240..0x342d8`):

```c
results->loadAddress  = 0;     // a2[0]
results->slide        = 0;     // a2[1]
results->errorMessage = 0;     // a2[2]
if ( options->forcePrivate /* options+0x04 */ == 1 ) {          // 0x34268
    return mapSplitCachePrivate(options, results);             // 0x3428c
} else if ( reuseExistingCache(options, results) ) {          // 0x34298
    return results->errorMessage == 0;                        // 0x342a8
} else {
    return mapSplitCacheSystemWide(options, results);         // 0x342d8
}
```

(The `__break(0xC471)` at `0x34288`/`0x342d4` is a decompiler artifact of the
pointer-authentication prologue, not real logic.)

### 1.1 Branch table

| # | Condition (IDA) | Test site | Taken → | Fall-through → |
|---|---|---|---|---|
| B1 | `*(u8)(options+4) == 1` | `0x34268` | `mapSplitCachePrivate` `0x342dc` | B2 |
| B2 | `reuseExistingCache() != 0` | `0x34298` | return `errorMessage==0` | `mapSplitCacheSystemWide` `0x352bc` |

### 1.2 `SharedCacheLoadInfo` (the `results` out-struct)

Observed writes only (producer = dyld, consumer = `ProcessConfig::DyldCache`):

| off | size | field | written where |
|---|---|---|---|
| `+0x00` | 8 | `loadAddress` (DyldSharedCache*) | `0x34258`; reuse `0x3520c`; private/systemwide |
| `+0x08` | 8 | `slide` | `0x34258`; reuse `0x35218` |
| `+0x10` | 8 | `errorMessage` (const char*) | `0x3425c`; errors everywhere (`+0x10`) |
| `+0x18` | 1 | `bool foundCacheFile` | `preflightCacheFile`? set at `+0x19`=bit; see §5 |
| `+0x19` | 1 | `dyldCacheDisabled` flag | `preflightCacheFile` tail `0x3600c` (`v9+25`) |
| `+0x1C` | 16 | `FileIdTuple` (fsid+inode) | `preflightCacheFile 0x35b34`; `DynamicRegion::getDyldCacheFileID` |

`ProcessConfig::DyldCache` (@ `IDA 0xbc9c`) then treats an all-zero `FileIdTuple`
as fatal: `FileIdTuple::operator bool` false → `halt("dyld shared region dynamic
config data was not set")` (`0x8b6ea`). This matters for the `dynamicRegion()==NULL`
question — see §10/§11.

---

## 2. `SharedCacheOptions` + every flag / env decision point

The options blob passed down is a **packed** ~40-byte struct. It is built inline in
`ProcessConfig::DyldCache::DyldCache` (`IDA 0xbc9c`, locals `v40..v53`) and copied by
`SyscallDelegate::getDyldCache` (`IDA 0x2fe34`) into a local before the call. The
copy uses odd offsets (`*(qword*)(a2+6)`, `a2+16`, `a2+24`, `a2+32`) → **packed**, so
byte offsets are exact.

### 2.1 Observed option fields

| off | type | name (observed role) | who writes | evidence |
|---|---|---|---|---|
| `+0x00` | int | **cacheDirFd** — directory fd for the dyld cache dir | `CacheFinder` `this[0]` (`0xbd8c`) | `preflightMainCacheFile` `0x357ac` `v10 = (dyld*)*a1` then `fstatat/openat` on it |
| `+0x04` | bool | **forcePrivate** | `= (Security.allowEnvVars/*a3+20*/ && getenv("DYLD_SHARED_REGION")=="private")` `0xbdac..0xbdd4` | consumed at `loadDyldCache 0x34268` |
| `+0x05` | bool | **hiddenFromFlat** | `PrebuiltLoader::hiddenFromFlat` `0xbe04` | **zeroed** by `getDyldCache` at `0x2fe84` (`BYTE5(v10[0])=0`) |
| `+0x06` | bool | **verbose** | `Logging.verbose` `*(a4+1)` `0xbde0` | every `if (*(u8)(v+6)&1) console(...)`; `0x3531c` etc. |
| `+0x07` | bool | (unused/0) | `v44 = 0` `0xbe08` | — |
| `+0x08` | bool | **enableReadOnlyDataConst-ish** | `Process+190` `0xbe10` | `mapSplitCachePrivate` `v32[8]` gates `forEachRegion` block callbacks |
| `+0x09` | bool | **page-in-linking / TPRO** | `= !sandboxBlockedPageInLinking` `0xbe04..` (only if `Process+49>=2`) | `mapSplitCachePrivate` `*(v+9)&1` → adds `0x200` to prot and `524306=0x80012` to mmap flags |
| `+0x0A` | bool | — | `v47 = v14&1` | — |
| `+0x0B` | bool | — | `v48 = v15` | — |
| `+0x0C` | bool | — | `v49 = Process+188` | — |
| `+0x0D` | bool | **slide-version gate** | `v50 = (Sandbox..)` | `mapSplitCachePrivate` `v49 = v32[13]` merged with slideInfo version check |
| `+0x10` | void* | (process / fs handle) | `v51 = *(a2+10)` `0xbe4c` | — |
| `+0x18` | int | — | `v52 = *(int*)(a2+22)` `0xbe54` | — |
| `+0x20` | void* | `ProcessConfig*` | `v53 = a7` `0xbe58` | used at `0x35474`/`0x35494` `LDR X0,[X21,#0x20]` |

### 2.2 Environment variables that reach this path

`_simple_getenv` @ `IDA 0x4a08` is the accessor; environ comes from `Process+0x98`
(a2+19) / `Process+0xA0` (a2+20).

| env var | parsed at | effect |
|---|---|---|
| `DYLD_SHARED_REGION` | `DyldCache::DyldCache 0xbd68` | only value that has any effect is the literal `"private"` (`_platform_strcmp(v17,"private")==0`, `0x8b6cd`). Sets `options.forcePrivate` (+4) **only if `Security.allowEnvVars` (`*(a3+20)==1`)`. Non-`"private"` values are ignored. If `DYLD_SHARED_REGION=private` but the private cache cannot be found → `halt("dyld private shared cache could not be found")` (`0x8b73e`). |
| `DYLD_SHARED_CACHE_DIR` | `CacheFinder::CacheFinder 0xb8b8` | `open()` that dir; its fd becomes `options.dirFd` (+0). Used instead of `/System/Library/dyld/`. |
| `dyld_hw_tpro_pagers` | `DyldCache 0xbe2c` | `!=NULL` → sets options `+9` (page-in linking). |
| `dyld_hw_tpro` | `DyldCache 0xc024` | `==NULL` → runs `forEachTPRORegion`. |
| `DYLD_PRINT_*` / `DYLD_INSERT_LIBRARIES` / `DYLD_IMAGE_SUFFIX` / `DYLD_ROOT_PATH` / `DYLD_USE_CLOSURES` / `DYLD_AMFI_FAKE` / … | various | general dyld, not shared-cache-path specific. Full string set at `0x8ab93..0x8d40a`. |
| `DYLD_IN_CACHE` | string `0x8aba6` | not on the shared-cache-mapping path. |

There is **no** `DYLD_SHARED_REGION` value other than `private`; there is **no**
env var that selects "map the cache as a plain file outside the shared region".

### 2.3 CacheFinder — how `dirFd` is chosen (`IDA 0xb854`)

```
this[0] = -1
if ((v8 = getenv("DYLD_SHARED_CACHE_DIR"))) { this[0] = open(v8); if (this[0]!=-1) return this; }
if (Process+128 & 8) goto use_default_dir;      // 0xb8e4
ignite(...)   // APFS cryptex ignition; status 8/19/85/89/90 handled
   ...
if (this[0] != -1) return this;
use_default_dir:
   dir = (Process+22 /*platform*/ || Process+10 == driverKit) ? "/System/DriverKit/System/Library/dyld/"
                                                             : "/System/Library/dyld/";   // 0xbabc
   this[0] = open(dir);
```

So the cache directory is `/System/Library/dyld/` unless overridden. On the chroot
this is exactly the symlinked directory in the state doc. `preflightMainCacheFile`
then looks for the file **`dyld_shared_cache_arm64e`** (no suffix, no `.development`
fallback — `dyld3::openat` @ `IDA 0x36030` only retries on `EAGAIN`=35/`EINTR`=4).

### 2.4 The `security` argument — `dyld4::ProcessConfig::Security`

There is **no** `security` parameter in `loadDyldCache(options, results)` itself.
Security reaches the cache path only as the **3rd ctor parameter** (`x2` = `a3`) of
`ProcessConfig::DyldCache::DyldCache` (@ `IDA 0xbc9c`), where it is consulted twice:
`a3+0x14` (20) gates `DYLD_SHARED_REGION=private` (`0xbdac`), and `a3+0x1A` (26) is
copied to `DyldCache[+171]` (`0xbee4`). The struct is built by
`Security::Security(Process*, SyscallDelegate*)` @ `IDA 0xb1a4`.

Fields (from the ctor decompile, disasm-confirmed at `IDA 0xb1cc..0xb2bc`):

| off | source | meaning (observed) |
|---|---|---|
| `+0x00` | `internalInstall` | internal/development build |
| `+0x01` | `internalInstall` | same |
| `+0x02` | 0 (word) | — |
| `+0x08` | 0 (qword) | — |
| `+0x10` | `inLockdownMode` | Lockdown Mode |
| `+0x11` | `AMFI bit0` | `AND W8,W0,#1; STRB [X19,#0x11]` |
| `+0x12` | `AMFI bit3` | SIMD extraction `0xb270..0xb294` |
| `+0x13` | `AMFI bit2` | " |
| `+0x14` | `AMFI bit1` | **allowEnvVars** — gates `DYLD_SHARED_REGION=private` |
| `+0x15` | `AMFI bit4` | " |
| `+0x16` | `AMFI bit5` | `UBFX w0,#5,#1` `0xb298` |
| `+0x17` | `AMFI bit6` | `UBFX #6` `0xb2a0` |
| `+0x18` | `AMFI bit7` | `UBFX #7` `0xb2a8` |
| `+0x19` | `AMFI bit8` | `UBFX #8` `0xb2b0` |
| `+0x1A` | `AMFI bit9` | `UBFX #9` `0xb2b8` (copied to `DyldCache[+171]`) |
| `+0x1B` | `DYLD_SKIP_MAIN` | only when internal |
| `+0x1C` | `DYLD_JUST_BUILD_CLOSURE` | |

The `+0x12..+0x15` group is produced by one `DUP/BIC/USHL/BIC/UZP1/STUR S0` sequence
(`0xb274..0xb294`) that bit-extracts 4 AMFI bits; emulating that sequence confirms
`+0x12=bit3, +0x13=bit2, +0x14=bit1, +0x15=bit4`.

**Restricted-process behaviour** (`IDA 0xb320..0xb35c`): if `platform <= 0xA` and
`(1<<platform) & 0x442` (platforms 1, 6, 10) **and** none of `Security[+0x12]`,
`Security[+0x13]`, `Security[+0x14]` is set, then `Security::pruneEnvVars`
(@ `IDA 0xb510`) **strips every `DYLD_*` variable from `environ`** in place. So a
restricted process loses `DYLD_SHARED_REGION` / `DYLD_INSERT_LIBRARIES` / `DYLD_PRINT_*`
*before* dyld reads them.

Chroot relevance: the launcher sets `DYLD_INSERT_LIBRARIES` on the iOS side and it
survives into the chroot (state doc "BIG finding") — i.e. this dyld does **not** prune,
so `Security[+0x14]` (AMFI bit1) is set for the chroot process. Nothing here needs
patching for the cache path.

---

## 3. `mapSplitCacheSystemWide` — end to end — `IDA 0x352bc`

This is the syscall-#536 submission side. Frame (all relative to `X19`, the local
frame base):
* `X19+0x0020`  `std::array<char[32],128>` — subcache path suffixes (4096 B)
* `X19+0x1020`  `CacheInfo mainCacheInfo` (448 B, ends `X19+0x11E0`)
* `X19+0x15E0`  `CacheInfo subcacheInfos[128]` (128·448 = 0xE000 B, ends `X19+0xF5E0`)
* `X19+0xF5E0`  256-B path buffer

`X21 = options`, `X20 = results` (`MOV X20,X1; MOV X21,X0` at `0x352fc/0x35300`).

### 3.1 Preflight phase (`0x35364..0x35430`)

```
memset(X19+0x0020, 0, 0x1000)            // 0x35324..0x35364  (subcache path array)
mainCacheInfo[+0x185] = 0; mainCacheInfo[+0x188] = 0    // 0x3531c/0x35320
if ( !preflightMainCacheFile(options, results, &mainCacheInfo,
                             pathbuf(=X19+0xF5E0), &subcachePaths(=X19+0x20)) )  // 0x3537c
    return 0;                             // 0x35388 CBZ W8 -> 0x3571C
W28 = mainCacheInfo[+0x1A8]               // = numSubCaches+1    0x3538c
if ( W28 != 0 && (mainCacheInfo[+0x185] & 1) == 0 ) {           // 0x35390/0x35394/0x35398
    results->errorMessage = "shared cache is too old, missing subcache suffixes";
    return 0;                             // 0x35570
}
for (i=0; i<128; i++) subcacheInfos[i][+0x185]=0, [+0x188]=0;   // 0x3539c..0x353bc
if ( W28 >= 2 ) {                         // 0x353c0 CMP W28,#2 ; B.CC -> 0x35434
    for (i=0; i<W28-1; i++)               // 0x353e8..0x35430
        if ( !preflightSubCacheFile(options, &subcacheInfos[i], &subcachePaths[...], suffix, pathbuf) )
            return 0;
}
```

> **Important decompiler correction.** Hex-Rays renders this as
> `if (v62) { error("too old"); }`. The disasm shows the real guard is nested:
> `W28 != 0 && (mainCacheInfo[+0x185] & 1) == 0`. `mainCacheInfo[+0x185]` is set by
> `preflightCacheFile` to `(mappingOffset > 0x1C8)` (`0x35f4c`). A modern 15.6.1
> header has a large `mappingOffset`, so the bit is 1 and the gate passes. The gate
> is a *minimum-header-size* check ("cache too old → no subcache suffixes"), **not**
> a live error for this cache.

### 3.2 DynamicRegion creation (`0x35434..0x354c0`)

```
v14 = DynamicRegion::make(0)              // 0x35434  -> vm_allocate(0x4000)+strcpy "dyld_data    v3"
if (!v14) { results->errorMessage = "Could not vm_allocate dynamic config memory"; return 0; }  // 0x35580
bzero(pathbuf,0x400); FileIdTuple::getPath(&results->fileId, pathbuf)      // 0x35450..0x35460
v14->setDyldCacheFileID(results->fileId)                                   // 0x35470
v14->setProcessorFlags(evaluateProcessorSpecificFunctionVariantFlags(cfg)) // 0x35478..0x35490
v14->setSystemWideFlags(evaluateSystemWideFunctionVariantFlags(cfg))       // 0x35494..0x354b0
v14->setCachePath(pathbuf)                                                 // 0x354c0
memmove(&subcacheInfos[0], &mainCacheInfo, 0x1C0)   // 0x354e0  (main becomes file[0])
```

### 3.3 `files[]` construction (`0x354e4..0x355b8`)

`files[]` entry stride = **12 bytes**; count = `W28+1` (`ADD W9,W28,#1` `0x354ec`).

```
files[0]        = copy of subcacheInfos[0] (= mainCacheInfo)
files[i]        = subcacheInfos[i]                  for i in 0..W28-1
files[W28]      = { 0xFFFFFFFF, 1, 0 }              // fd=-1 placeholder, 1 mapping
```

Per-entry layout (verified from `0x3552c..0x35550` and the placeholder `0x355b4`):

| off | size | field | source |
|---|---|---|---|
| `+0x00` | u32 | **fd** | `subcacheInfos[i][+0x188]` (`LDR W12,[X10]`, `X10=&infos[i]+0x188`) |
| `+0x04` | u32 | **mappingsCount** | `subcacheInfos[i][+0x180]` (`LDUR W13,[X10,#-8]`) |
| `+0x08` | u32 | **reserved** — nonzero **only** for `files[0]` | `= subcacheInfos[0][+0x1A0]` (`LDR W9,[X19,#0x1780]`) when `i==0`, else `0` (`CSEL W12,W9,WZR,EQ` `0x3554c`) |

`W23` accumulates `Σ mappingsCount` (`ADD W23,W13,W23` `0x35544`).

> The `+0x08` field: `subcacheInfos[0][+0x1A0]` is written by `preflightCacheFile`
> as `header[0xF0]` (the `maxSlide` slot in `dyld_cache_header`). Because it is set
> only for the main cache, its exact name in the kernel's `shared_file_np_t` was not
> confirmed from the dyld side alone — treat it as "reserved / main-only value".
> For the fd=-1 placeholder it is 0.

### 3.4 `mappings[]` construction (`0x355a0..0x35680`)

`mappings[]` entry stride = **48 bytes**; count = `W23 + 1` (`ADD W25,W23,#1` `0x355bc`).

The array is a flat concatenation of every `subcacheInfos[i]`'s mounting entries,
followed by one synthetic entry for the dynamic region. Copy loop
(`0x3561c..0x3565c`): for each cache `i`, read its `+0x180` count, then copy
`count × 48` bytes verbatim (`LDP Q0,Q1 / STP Q0,Q1 / LDR Q0 [x15,#0x20] /
STR Q0 [x16,#0x20]` = 16+16+16 = 48).

Per-entry layout (verified from `preflightCacheFile 0x35ea0..0x35ef8` and the
synthetic entry `0x35660..0x35680`):

| off | size | field | source (in `CacheInfo`) |
|---|---|---|---|
| `+0x00` | u64 | **address** | header `mapping[i].address` |
| `+0x08` | u64 | **size** | header `mapping[i].size` |
| `+0x10` | u64 | **fileOffset** | header `mapping[i].fileOffset` |
| `+0x18` | u64 | **slideInfoSize** | `0`, or `v40` when the extended entry exists |
| `+0x20` | u64 | **slideInfoFileOffset** | `0`, or `v39 - regionStart + mapping[0].address` |
| `+0x28` | u32 | **maxProt** | `mapping[i].maxProt` (on-disk `dyld_cache_mapping_info+0x18`) |
| `+0x2C` | u32 | **initProt** | `mapping[i].initProt` (`+0x1C`) |

Synthetic dynamic-region entry (`0x35660..0x35680`):

```
mappings[W23].address            = X24 = mainCacheInfo[+0x1B0]      // 0x35660/0x35670
mappings[W23].size               = DynamicRegion::size(v14)         // 0x35664/0x35670
mappings[W23].fileOffset         = v14 (the DynamicRegion*)         // 0x35678  <-- pointer, not a file offset
mappings[W23].slideInfoSize      = 0                                 // 0x35674
mappings[W23].slideInfoFileOffset= 0                                 // 0x35674
mappings[W23].maxProt            = 1                                 // 0x35680 (D0={1,1})
mappings[W23].initProt           = 1                                 // 0x35680
```

**`mainCacheInfo[+0x1B0]`** is set in `preflightCacheFile` (`0x35fdc`) as
`header[0x1F0] (dynamicDataOffset) + header[0xE0] (sharedRegionStart)` =
`0x180000000 + 0x12c75c000 = 0x2ac75c000`. This is the **fd=-1 dynamicRegion VA**
submitted to the kernel.

### 3.5 syscall #536 submission (`0x35684..0x35698`)

`__shared_region_map_and_slide_2_np` @ `IDA 0x76df8` is a thin wrapper:

```
76DF8: MOV X16, #0x218     ; 536
76DFC: SVC #0x80           ; x0..x3 = incoming a1..a4
76E00: B.CC  locret_76E20  ; success -> return
76E04: ... cerror_nocancel ; errno path
```

The call site sets **only x0..x3**:

| reg | arg | value at call (`0x35684..0x35690`) |
|---|---|---|
| `x0` | `files_count` | `W0 = [X19+0x14]` = `W28+1` (saved at `0x354f4`) |
| `x1` | `files[]` | `X1 = [X19+8]` = files array base (saved at `0x355a4`) |
| `x2` | `mappings_count` | `X25 = W23+1` |
| `x3` | `mappings[]` | `X26` = mappings array base |

**There is no user slide argument.** `x4` and beyond are never set for this call, so
the kernel's 5th+ args are undefined/garbage and the kernel itself decides the slide
from the supplied (unslid) mapping addresses. `DyldSharedCache::slide()` @ `IDA 0x4f240`
(`= this - mapping[0].address`) is used only for verbose output, never for the syscall.

### 3.6 Post-syscall logic (`0x35698..0x35714`)

```
W23 = syscall return              // 0x35698
v14->free()                       // 0x356a0
for each file[i] with fd != -1: close(fd)            // 0x356a4..0x356cc
W0 = reuseExistingCache(options, results)            // 0x356d8
if (W23 == 0) {                   // syscall OK
    if (W0 & 1) goto ret1;                            // 0x356f4 TBNZ W0,#0
    if (options->verbose==1) console("mapped dyld cache file system wide\n");  // 0x35704
    goto ret1;
}
if (W0 & 1) goto ret1;            // syscall FAILED but reuse OK -> still success  // 0x356e0
if (results->errorMessage == 0)   // 0x356e4/0x356e8
    JUMPOUT(0x38D08);             // set errorMessage (cold block, see §9.3)
return 0;                         // 0x356f0 -> 0x35714
ret1: return 1;                   // 0x35710
```

Key consequence: **a failed syscall is masked whenever `reuseExistingCache` then
succeeds** — this is why a broken syscall can still yield "dyld mapped the cache".
That reuse call re-runs `__shared_region_check_np` and will happily latch onto the
kernel-prebound iOS cache (see §4 and §10).

---

## 4. `reuseExistingCache` — `IDA 0x351a8`

```c
bool dyld3::reuseExistingCache(const SharedCacheOptions& options, SharedCacheLoadInfo* results) {
    __s1 = 0;                                                    // 0x351c8
    if ( __shared_region_check_np(&__s1) ) return 0;             // 0x351d0  (errno -> fail)
    const char* cache = __s1;                                    // 0x351e0  (shared-region base)
    if ( _platform_strcmp(cache, "dyld_v1  arm64e") ) {          // 0x351f0  <-- ONLY identity check
        results->errorMessage = "existing shared cache in memory is not compatible"; // 0x35204
        return 0;
    }
    results->loadAddress = cache;                                             // 0x3520c
    results->slide       = DyldSharedCache::slide(cache);                     // 0x35218
    v6 = *(u64*)(cache + 0x68);              // header.cacheType            // 0x3521c
    results->[+0x19] = (v6==0) || (v6==2 && *(u32*)(cache+0x1C8)==0);         // 0x35230..0x3524c
    if ((dr = DyldSharedCache::dynamicRegion(cache)) != 0) {                   // 0x35254
        DynamicRegion::getDyldCacheFileID(dr, &results->fileId);               // 0x35264
        if ( options->verbose == 1 ) console("re-using existing shared cache (%s):\n",
                                             dr->osCryptexPath());              // 0x35278..0x35290
    } else {
        console("mapped cache does not contain dynamic config data\n");        // 0x352a0
    }
    return 1;                                                                 // 0x352a4
}
```

### 4.1 Checks it performs (answers Q3)

* **magic only.** `_platform_strcmp(regionBase, "dyld_v1  arm64e")` @ `0x351f0`.
  **No** UUID check, **no** path-prefix check, **no** file-identity check.
* `cacheType` (`header+0x68`) is read (`0x3521c`) but only to set a `results` flag
  (`scale`/`dylibsExpectedOnDisk`-style), never to reject.
* `shared_region_check_np` argument: a single `char*` out-pointer (`&__s1`), returns
  the **shared-region base VA** the kernel pre-bound to this task.

Because the iOS 16.3 kernel pre-binds *its own* cache (same `dyld_v1  arm64e` magic,
same region base `0x180000000`) at exec, this function accepts it — the macOS dyld
then runs on iOS libSystem. **The single bypass point is the magic `strcmp` at
`0x351f0`.**

### 4.2 Return-value semantics

| return | when |
|---|---|
| `0` | `check_np` failed, **or** magic mismatch (sets `errorMessage="existing shared cache in memory is not compatible"`) |
| `1` | magic matched → `loadAddress`/`slide`/`fileId` populated from the in-memory region |

### 4.3 The suspected-fault dereference (Q2)

`DynamicRegion::getDyldCacheFileID` is called on the result of
`DyldSharedCache::dynamicRegion` (`IDA 0x50dfc`):

```c
DyldSharedCache::dynamicRegion(this) {
    off = *(u64*)((char*)this + 0x1F0);            // header.dynamicDataOffset
    if ( *(u64*)(this+off) ^ "dyld_data    v" ... ) return 0;   // magic compare
    else                                            return this + off;
}
```

So `dynamicRegion()` **dereferences `this + header[0x1F0]`**, i.e.
`regionBase + 0x12c75c000 = 0x2ac75c000`. If the kernel did not map the dynamic
region there (because the syscall failed / it lies past the iOS 4 GB region), this
read **faults** — the post-map SIGSEGV the state doc chased. Patching
`dynamicRegion()` to `return 0` does *not* remove the fault chain, because the
scratch `getDyldCacheFileID` then never runs, leaving `results->fileId == 0`, which
makes `ProcessConfig::DyldCache` `halt(...)` (§1.2, §10).

---

## 5. The preflight chain (cache discovery + validation)

### 5.1 `preflightMainCacheFile` — `IDA 0x3576c`

```c
bool preflightMainCacheFile(const SharedCacheOptions& options,   // a1 (dirFd = *a1)
                            SharedCacheLoadInfo* results,        // a2
                            CacheInfo* cacheInfo,                // a3
                            char* pathBuf,                       // a4  (256 B)
                            std::array<char[32],128>* paths) {    // a5
    strcpy(__source, "dyld_shared_cache_arm64e");                     // 0x357bc  (NO suffix)
    if ( !fstatat(*a1, __source, &st, 0, a5) )                        // 0x357d4
        strlcpy(pathBuf, __source, 0x100);                           // 0x357e8
    fd = dyld3::openat(*a1, __source, ...);                          // 0x357f4
    if (fd == -1) {
        results->[+0x18] = 0;                                        // 0x35838
        results->errorMessage = (errno==ENOENT) ? "no shared cache file"
                                                : "shared cache file open() failed";
        return 0;
    }
    results->[+0x18] = 1;                                            // 0x35808
    return preflightCacheFile(options, results, cacheInfo, fd, paths);
}
```

Filename is **exactly** `dyld_shared_cache_arm64e` — `dyld3::openat` (`IDA 0x36030`)
retries only on `EAGAIN(35)`/`EINTR(4)`, there is no `.development` / cryptex-name
fallback in this build. Directory comes from `options.dirFd` (§2.3).

### 5.2 `preflightSubCacheFile` — `IDA 0x3588c`

```c
strlcpy(__dst, arg4, 0x100); strlcat(__dst, arg5, 0x100);   // 0x358d8/0x358e8  filename = base + suffix
if ( openat(dirFd, __dst) == -1 ) {
    results->[+0x18] = 0;
    results->errorMessage = (errno==ENOENT) ? "no shared cache file" : "shared cache file open() failed";
    return 0;
}
results->[+0x18] = 1;                       // 0x35908
return preflightCacheFile(...);             // same validator as the main file
```

So subcache filenames are `dyld_shared_cache_arm64e` + suffix (`.01`, `.02`, …) where
the suffix comes from the `subCacheArray` entries of the **main** header.

### 5.3 `preflightCacheFile` — `IDA 0x35a98` (the real validator)

Reads a 0x4000 header, validates, and fills one `CacheInfo`. Every reject point:

| # | IDA | condition | error string |
|---|---|---|---|
| 1 | `0x35b00` | `fstat64(fd)` fails | `shared cache file stat() failed` |
| 2 | `0x35b54` | `pread(fd,&hdr,0x4000,0) != 0x4000` | `shared cache file pread() failed` |
| 3 | `0x35b84` | `magic != "dyld_v1  arm64e"` (qwords `0x2031765F646C7964`,`0x6534366D726120`) | `shared cache file has wrong magic` |
| 4 | `0x35c18` | `mappingOffset>=0xE0` and `platform` mismatch (see below) | `shared cache file is for a different platform` |
| 5 | `0x35c04` | `mappingCount==0 || mappingCount>16` (`(count-9) <= -9`) | `shared cache file mappings are invalid` |
| 6 | `0x35c48` | `mapping[0].fileOffset != 0` | `shared cache text file offset is invalid` |
| 7 | `0x35c88` | `codeSignatureOffset+codeSignatureSize != st_size` | `shared cache code signature size is invalid` |
| 8 | `0x35c9c` | `mappingCount>=2 && *(u32*)(last_mapping-2) != 1` | `shared cache linkedit permissions are invalid` |
| 9 | `0x35cd8` | `mapping[0].maxProt != 5` | `shared cache text permissions are invalid` |
| 10 | `0x35d1c` | mappings overlap / fileOffset not contiguous | `shared cache mappings overlap` |
| 11 | `0x35d70` | `fcntl(fd, F_ADDFILESIGS=97, &{0,csOff,csSize}) == -1` | `code signature registration for shared cache failed` |
| 12 | `0x35d80` | `sigCovered < codeSignatureOffset` | `code signature does not cover entire shared cache file` |
| 13 | `0x35db4` | `mmap(0,0x4000,PROT_READ\|PROT_EXEC,2,fd,0)` fails | `first page of shared cache not mmap()able` |
| 14 | `0x35dd0` | `memcmp(mapped, hdr, 0x4000)` | `first page of mmap()ed shared cache not valid` |
| 15 | `0x35f1c` | `numSubCaches > 0x7F` | `shared cache file subcache count exceeds limit` |
| 16 | `0x35f30` | `paths==NULL && numSubCaches` | `no shared cache subcache indices` |
| 17 | `0x35f74` | `pread(fd, buf, 0x1C00, hdr[0x188]) != 7168` | `shared cache file pread() failed, could not read subcache entries` |

Platform check detail (`0x35c24`): `plat = mach_o::Platform::value(options.platform)`;
accept if `hdr.platform(+0xD8)==plat && (hdr[+0xDD] & 2)==0`, **or** `hdr[+0x170]!=0 &&
==plat`. `+0xDD & 2` is the simulator bit. So a *macOS* platform tag is required for a
macOS cache — the iOS 16.3 kernel doesn't care about this (it's dyld's own check).

**`CacheInfo` fields written by `preflightCacheFile`** (448 B struct, §8.1):

| off | value | IDA |
|---|---|---|
| `+0x00..0x17F` | `mapping[0..7]` × 48 B (see §3.4) | `0x35eac` |
| `+0x180` | `mappingCount` | `0x35e00` |
| `+0x184` | `options[12]` (byte) | `0x35fb0` |
| `+0x185` | `(mappingOffset > 0x1C8)` | `0x35f4c` |
| `+0x188` | **fd** (the open fd; used as `files[i].fd`) | `0x35f10` |
| `+0x190` | `header[0xE0..0xEF]` = `{sharedRegionStart, sharedRegionSize}` | `0x35fa0` |
| `+0x1A0` | `header[0xF0]` (= `maxSlide`) | `0x35fa8` |
| `+0x1A8` | `numSubCaches + 1` | `0x35fc4` |
| `+0x1B0` | `header[0xF0? no: 0x1F0] (=dynamicDataOffset) + header[0xE0] (=sharedRegionStart)` → **dynregion VA** | `0x35fdc` |
| `+0x1B8` | `header[0x1F8]` | `0x35fdc` |

and `results->[+0x19] = (cacheType==0) || (cacheType==2 && header[0x1C8]==0)` (`0x3600c`).

### 5.4 Header-field usage gates (layout-version detection)

`preflightCacheFile` gats every later field on `mappingOffset`:

| compare | IDA | meaning |
|---|---|---|
| `mappingOffset >= 0xE0` | `0x35bf4` | platform/biometric fields present |
| `mappingOffset > 0x138` | `0x35e3c` | extended 56-byte mapping entries present |
| `mappingOffset >= 0x18D` | `numSubCaches 0x4f22c` | `subCacheArrayCount` present |
| `mappingOffset > 0x1C8` | `0x35f4c` | `CacheInfo[+0x185] = 1` (the "not too old" bit) |
| `mappingOffset >= 0x1C9` | `0x35f54` | extended subcache entries pread'ed |

### 5.5 Does dyld itself reject a cache that exceeds the 4 GB iOS region? — **No**

Decompile-verified: neither `preflightCacheFile` nor `mapSplitCacheSystemWide` ever
compares `sharedRegionStart` (`+0xE0`) or `sharedRegionSize` (`+0xE8`) against any
bound. `preflightCacheFile` only *copies* them into `CacheInfo[+0x190]` (`0x35fa0`);
the systemwide path hands the mapping addresses to syscall #536 unchanged. There is
**no `0x280000000` comparison in the cache-load path** (an unaligned qword scan for
`0x280000000`/`0x100000000`/`0x180000000` in the whole image found no cache-path
constant to xref).

The only hardcoded shared-region address is the **base `0x180000000`**, in the
`mapSplitCachePrivate` cleanup `mmap(0x180000000, 0x180000000, PROT_NONE, 0x1012, -1,
0)` @ `IDA 0x35088` (`MOV X0,#0x180000000; MOV X1,#0x180000000; MOV W3,#0x1012`). That
base equals the iOS base, so no translation is needed at the base — only the
**extent** differs.

**Answer to the handover's "which field decides whether this cache is legal for the
4 GB region":** *none in dyld.* The legality test is the kernel's
`shared_region_map_and_slide_2_np` range check on each submitted `mapping[i]`; dyld
submits and waits. That is why there is no dyld-side constant to patch and the
breakpoint is a kernel return code (§10).

### 5.6 On-disk subcache array entry layout (`IDA 0x35f58`, `0x4f4ec`, `0x51184`)

`preflightCacheFile` reads the subcache array from the **main** header's file:
`pread(fd, buf, 0x1C00, hdr[0x188])` (128 × 56 = 7168 B; `0x35f58..0x35f74`). It then
copies, per entry, **32 bytes starting at entry+0x18** into the `std::array<char[32],128>`
(`LDP Q0,Q1,[x9]; STP Q0,Q1,[x23],#0x20; x9 += 0x38` at `0x35f88..0x35f90`).

Two independent consumers pin the entry fields:

| entry off | field | evidence |
|---|---|---|
| `+0x00` | `uuid[16]` | not copied; consistent with `dyld_subcache_entry` |
| `+0x10` | `cacheVMOffset` (u64) — added to the shared-region base to locate the subcache header (`&a1[*v9]`) | `forEachCache 0x4f568` (`&v6[56*i+16]`; legacy `&v6[24*i+16]`) |
| `+0x18..0x37` | 32 B copied verbatim into the path array; concatenated after the main cache name to form the subcache filename | `0x35f88`; `preflightSubCacheFile 0x358d8/0x358e8` (`strlcpy`+`strlcat`) |

Entry stride is **56 B** for the extended format (`mappingOffset >= 0x1C9`) and **24 B**
for the legacy format; `numSubCaches` is guarded by `mappingOffset >= 0x18D` (`0x4f22c`).

`DyldSharedCache::isSubCachePath` @ `IDA 0x51184`: a path is a subcache iff it contains
a `.` and is **not** exactly `.development`.

> Field names beyond `+0x00`/`+0x10` are inferred from the copy ranges; the exact v2
> member names were not recoverable from the stripped binary — the **offsets** above are
> load/copy-confirmed.

---

## 6. `dyld_cache_header` (DSC header) — full field table (15.6.1)

`struct DyldSharedCache` embeds `dyld_cache_header` as its only member, so C++ object
offsets == header offsets. Total size **0x228**. Authoritative layout source: the block
type-encoding string at **`IDA 0x8b989`** (parsed symbol by symbol) cross-checked
against accessor immediates (`numSubCaches 0x4f224`, `slide 0x4f240`, `dynamicRegion
0x50dfc`, `imagesCount 0x4f954`, `objcHeaderInfoRO 0x4fdc0`, `forEachPrewarmingEntry
0x4f250`, `addressInText 0x50d94`, `swiftOpt 0x4ff78`, `getIndexedImagePath 0x4f9ec`, …).

| off | size | field | evidence |
|---|---|---|---|
| `0x000` | 16 | `magic[16]` = `"dyld_v1  arm64e"` | `preflightCacheFile 0x35b84` |
| `0x010` | 4 | `mappingOffset` | `slide 0x4f240`, `numSubCaches 0x4f22c`, `0x35bf4` |
| `0x014` | 4 | `mappingCount` | `preflightCacheFile 0x35c34` |
| `0x018` | 4 | `imagesOffsetOld` | `getIndexedImagePath 0x4f9ec` |
| `0x01C` | 4 | `imagesCountOld` | `imagesCount 0x4f954` |
| `0x020` | 8 | `dyldBaseAddress` | type-encoding |
| `0x028` | 8 | `codeSignatureOffset` | `0x35c88` |
| `0x030` | 8 | `codeSignatureSize` | `0x35c88` |
| `0x038` | 8 | `slideInfoOffsetUnused` | `0x35c48` region |
| `0x040` | 8 | `slideInfoSizeUnused` | same |
| `0x048` | 8 | `localSymbolsOffset` | encoding |
| `0x050` | 8 | `localSymbolsSize` | encoding |
| `0x058` | 16 | `uuid[16]` | `getUUID 0x2998` |
| `0x068` | 8 | **`cacheType`** | `preflightCacheFile 0x35ff0` (`==0` / `==2`) |
| `0x070` | 4 | `branchPoolsOffset` | encoding |
| `0x074` | 4 | `branchPoolsCount` | encoding |
| `0x078` | 8 | `accelerateInfoAddr` | encoding |
| `0x080` | 8 | `accelerateInfoSize` | encoding |
| `0x088` | 8 | `imagesTextOffset` | `addressInText 0x50d94` |
| `0x090` | 8 | `imagesTextCount` | `addressInText 0x50d94` |
| `0x098` | 8 | `patchInfoAddr` | `patchTable 0x4ffa0` |
| `0x0A0` | 8 | `patchInfoSize` | encoding |
| `0x0A8` | 8 | `otherImageGroupAddrUnused` | encoding |
| `0x0B0` | 8 | `otherImageGroupSizeUnused` | encoding |
| `0x0B8` | 8 | `progClosuresAddr` | encoding |
| `0x0C0` | 8 | `progClosuresSize` | encoding |
| `0x0C8` | 8 | `progClosuresTrieAddr` | encoding |
| `0x0D0` | 8 | `progClosuresTrieSize` | encoding |
| `0x0D8` | 4 | **`platform`** | `mach_o::Platform::value 0x35c18` |
| `0x0DC` | 4 | bitfield `formatVersion:8, dylibsExpectedOnDisk:1, simulator:1, locallyBuilt:1, builtFromChainedFixups:1, :1, pad:19` | `0x35c2c` reads `0xDD & 2` = simulator |
| `0x0E0` | 8 | **`sharedRegionStart`** | `preflightCacheFile 0x35fa0` |
| `0x0E8` | 8 | **`sharedRegionSize`** | `0x35fa0` |
| `0x0F0` | 8 | `maxSlide` | `0x35fa8` (→ `CacheInfo[+0x1A0]`) |
| `0x0F8` | 8 | `dylibsImageArrayAddr` | encoding |
| `0x100` | 8 | `dylibsImageArraySize` | encoding |
| `0x108` | 8 | `dylibsTrieAddr` | encoding |
| `0x110` | 8 | `dylibsTrieSize` | encoding |
| `0x118` | 8 | `otherImageArrayAddr` | encoding |
| `0x120` | 8 | `otherImageArraySize` | encoding |
| `0x128` | 8 | `otherTrieAddr` | encoding |
| `0x130` | 8 | `otherTrieSize` | encoding |
| `0x138` | 4 | `subCacheArrayOffset` (legacy 24-byte-entry form) | `forEachCache 0x4f55c`; `0x35e3c`; `preflightCacheFile 0x35f74` |
| `0x13C` | 4 | `subCacheArrayCount` (legacy) | encoding |
| `0x140` | 8 | `mappingWithSlideOffset` | encoding |
| `0x148` | 8 | `mappingWithSlideCount` | encoding |
| `0x150` | 8 | `dylibsPBLStateArrayAddrUnused` | encoding |
| `0x158` | 8 | `dylibsPBLSetAddr` | encoding |
| `0x160` | 8 | `programsPBLSetPoolAddr` | encoding |
| `0x168` | 4 | `programsPBLSetPoolSize` | encoding |
| `0x16C` | 4 | — | encoding |
| `0x170` | 4 | **secondary platform** (gadget/alt platform tag) | `preflightCacheFile 0x35c68` |
| `0x174` | 4 | — | encoding |
| `0x178` | 8 | `swiftOptsOffset` | `swiftOpt 0x4ff78` |
| `0x180` | 8 | `swiftOptsSize` | encoding |
| `0x188` | 4 | **`subCacheArrayOffset`** (extended 56-byte-entry form; also the file offset the subcache array is `pread` from) | `numSubCaches 0x4f238` reads `0x18C`; `preflightCacheFile 0x35f74`; `forEachCache 0x4f55c` |
| `0x18C` | 4 | **`subCacheArrayCount` = numSubCaches** | `numSubCaches 0x4f238`; `forEachCache 0x4f540` |
| `0x190` | 16 | `symbolFileUUID[16]` | encoding |
| `0x1A0` | 8 | — | encoding |
| `0x1A8` | 8 | — | encoding |
| `0x1B0` | 8 | — | encoding |
| `0x1B8` | 8 | — | encoding |
| `0x1C0` | 4 | `imagesOffset` | `getIndexedImagePath 0x4f9ec` |
| `0x1C4` | 4 | `imagesCount` | `imagesCount 0x4f954` |
| `0x1C8` | 4 | `cacheSubType` flag | `preflightCacheFile 0x35ff0` |
| `0x1CC` | 4 | **implicit alignment pad** | — |
| `0x1D0` | 8 | `objcOptsOffset` | `objcHeaderInfoRO 0x4fdc0` |
| `0x1D8` | 8 | `objcOptsSize` | encoding |
| `0x1E0` | 8 | `swiftOptsOffset` (alt) | encoding |
| `0x1E8` | 8 | — | encoding |
| `0x1F0` | 8 | **`dynamicDataOffset`** | `dynamicRegion 0x50e2c`; `preflightCacheFile 0x35fdc` |
| `0x1F8` | 8 | `dynamicDataMaxSize` | `preflightCacheFile 0x35fdc` (→ `CacheInfo[+0x1B8]`) |
| `0x200` | 4 | — | encoding |
| `0x204` | 4 | — | encoding |
| `0x208` | 8 | `tproDataOffset` | encoding |
| `0x210` | 8 | — | encoding |
| `0x218` | 8 | `prewarmingOffset` | `forEachPrewarmingEntry 0x4f250` |
| `0x220` | 8 | — | encoding |

Confirmed ground-truth values: `sharedRegionStart=0x180000000`, `sharedRegionSize=
0x12c760000`, `dynamicDataOffset=0x12c75c000`, `subCacheArrayOffset=0x333e8`,
`subCacheArrayCount=1`, `cacheType=0`.

---

## 7. `mapSplitCachePrivate` — `IDA 0x342dc`

The `DYLD_SHARED_REGION=private` / `forcePrivate` path. Prologue is identical to the
systemwide path (`preflightMainCacheFile` then, if `W28>=2`, `preflightSubCacheFile`
loop with verbose per-file logs). Then it diverges:

```c
subcacheInfos[0] = mainCacheInfo                        // memmove 0x1C0  (0x344e0)
deallocateExistingSharedCache()                          // 0x344ec  -> check_np(0)/unmap  (IDA 0x3420c)
results->loadAddress = mainCacheInfo[0]; results->slide = 0; // 0x344f8
// ---- plain mmap loop over the dynamic mappings list ----
for each CacheInfo i, for each mapping m (0x34518..0x345c0):
    addr = base + m.address - firstAddr + results->slide;      // 0x34544
    prot = m.prot & 7;  flags = 0x12;                          // MAP_PRIVATE|MAP_FIXED (0x34558)
    if (options[+9] /*TPRO*/ & 1)
        if (m.flags & 0x200) { prot = (m.prot & 5)|2; flags = 0x80012; }   // 0x34558/0x34570
    if ( mmap(addr, m.size, prot, flags, fd, m.fileOffset) == -1 )   // 0x345b4
        goto mmap_failed;
```

* `0x12` = `MAP_PRIVATE(0x2) | MAP_FIXED(0x10)`. `0x80012` adds `0x80000`
  (the TPRO / page-in-linking flag; enabled by `options[+9]`, from
  `!sandboxBlockedPageInLinking`).
* `deallocateExistingSharedCache` (`IDA 0x3420c`) is `check_np(0)` + unmap of the
  kernel's pre-bound region — this is what detaches the iOS cache before mapping the
  macOS one.
* On mmap failure (`0x35018`): verbose log
  `mmap(%d, %d) the shared cache region failed due to: %d`; set
  `errorMessage = "mmap() the shared cache region failed"` if empty; close all fds;
  return 0.
* Then `DynamicRegion::make(results->slide + mainCacheInfo...)` (`0x345e8`); if NULL →
  `mmap(0x180000000, 0x180000000, 0, 0x1012, 0, 0)` + `errorMessage = "could not
  mmap() dynamic config memory"` (`0x350a0`).
* Then per-cache page-in-linking via `__map_with_linking_np` (`IDA 0x769cc`) with
  `slideInfoHeader->version == 5` asserted at `0x3511c`/`0x3513c`/`0x3515c`
  (assembly-discipline: these are real `__assert_rtn`, not to be NOPed).
* Return value = `v147`/`v6` (1 on success).

**Failure branches inside the mmap loop:** on the first failing mapping it leaves the
loop (break), logs, deallocates nothing further, closes fds, and returns 0 — it does
**not** continue. (`memmove X87` etc. are just loop bookkeeping.)

This path never calls syscall #536; it mmaps the DSC executable pages as ordinary
PROT_EXEC private pages **outside** the shared region. On iOS 16.3 those pages are not
in the trust cache's covered region → CS kills the process (137). Dead end confirmed.

---

## 8. Key data structures & globals

### 8.1 `CacheInfo` (dyld internal, 448 B) — `IDA 0x35a98`

| off | size | field |
|---|---|---|
| `0x000` | 8·? | `MappingInfo mappings[8]` (48 B each, max 8 — `mappingCount<=16` guard is separate) |
| `0x180` | 4 | `mappingCount` |
| `0x184` | 1 | `options[12]` copy |
| `0x185` | 1 | `(mappingOffset > 0x1C8)` — "not too old" |
| `0x188` | 4 | `fd` |
| `0x190` | 8 | `sharedRegionStart` |
| `0x198` | 8 | `sharedRegionSize` |
| `0x1A0` | 8 | `maxSlide` |
| `0x1A8` | 4 | `numSubCaches + 1` |
| `0x1B0` | 8 | `dynamicRegionVA = sharedRegionStart + dynamicDataOffset` |
| `0x1B8` | 8 | `dynamicDataMaxSize` |

### 8.2 `DyldSharedCache` accessors used by the loader

| fn | IDA | body |
|---|---|---|
| `slide()` | `0x4f240` | `this - mapping[0].address` |
| `numSubCaches()` | `0x4f224` | `mappingOffset>=0x18D ? *(u32*)(this+0x18C) : 0` |
| `dynamicRegion()` | `0x50dfc` | `off=*(u64*)(this+0x1F0); mem-prefix "dyld_data    v"? this+off : 0` |
| `unslidLoadAddress()` | `0x4f2f0` | reads `+0x10` |
| `mappedSize()` | `0x4fa98` | guarded by `mappingOffset>=0x18C`, reads `+0xE8` |
| `forEachCache()` | `0x4f4ec` | callback(main); then for `i<numSubCaches` callback(`this + *(u64*)(this + subCacheArrayOffset + entry_i)`) — entries 24 B (old) or 56 B (new) |
| `forEachRegion()` | `0x4f3c4` | iterates header mappings; 32 B entries (old) or 56 B entries at `this+*(u32*)(this+0x138)` (new) |
| `getUUID()` | `0x2998` | reads `+0x58` |

### 8.3 `DynamicRegion` (`IDA 0x511c0..0x5135c`)

| fn | IDA | body |
|---|---|---|
| `make(this)` | `0x511c0` | `this ? mmap(this,0x4000,RW,0x1012,-1,0) : vm_allocate(0x4000)` then `strcpy(..,"dyld_data    v3")` |
| `size()` | `0x51244` | returns 0x4000 |
| `free()` | `0x51258` | vm_deallocate |
| `set/getDyldCacheFileID` | `0x51270/0x512f4` | FileIdTuple in/out |
| `setCachePath` | `0x51278` | strlcpy of path |
| `osCryptexPath` | `0x512c4` | returns stored path |
| `setProcessorFlags/setSystemWideFlags` | `0x512ec/0x512e4` | store `unsigned __int128` flag pair |
| `setReadOnly` | `0x512d8` | sets a byte |

### 8.4 Globals / helper addresses

| symbol | IDA |
|---|---|
| `errno` (`_errno`) | `0xa9b10` |
| `dyld4::console` | `0xa2f4` |
| `dyld4::halt` | `0xbb04` |
| `_simple_getenv` | `0x4a08` |
| `__shared_region_map_and_slide_2_np` | `0x76df8` (SVC #0x80, X16=0x218=536) |
| `__shared_region_check_np` | `0x76dcc` |
| `crossarch_trap` stub | `0x76270` (`SVC #0x80; RET` — syscall 38) |
| `__map_with_linking_np` | `0x769cc` |
| `dyld3::deallocateExistingSharedCache` | `0x3420c` |
| `dyld3::openat` / `fstatat` | `0x36030` / `0x52700` |
| `FileIdTuple::getPath/inode/fsID` | `0x51398` / `0x51388` / `0x51390` |
| `ProcessConfig::DyldCache::DyldCache` | `0xbc9c` |
| `CacheFinder::CacheFinder` | `0xb854` |
| `SyscallDelegate::getDyldCache` | `0x2fe34` (only caller of `loadDyldCache`) |

### 8.5 `ProcessConfig::DyldCache` — runtime consumer fields (`IDA 0xbc9c`)

Built once from the `SharedCacheLoadInfo`; the `this` fields populated and later read
(from the ctor decompile):

| this+ | value | IDA |
|---|---|---|
| `0x00` | `DyldSharedCache*` (`loadInfo+0`) | `0xbef0` |
| `0x08` | `FileIdTuple` (`loadInfo+0x1C`, 16 B) | `0xbeec` |
| `0x18` | `loadInfo+8` (slide) | `0xbf0c` |
| `0x20` | `unslidLoadAddress()` | `0xbf24` |
| `0x30` | `objcHeaderInfoRO()` | `0xbf30` |
| `0x38` | `objcHeaderInfoRW()` | `0xbf3c` |
| `0x40` | `objcSelectorHashTable()` | `0xbf48` |
| `0x48` | `objcClassHashTable()` | `0xbf54` |
| `0x50` | `objcProtocolHashTable()` | `0xbf60` |
| `0x68` | `swiftOpt()` | `0xbf6c` |
| `0x80` | `patchTable()` | `0xbfb0` |
| `0x88` | `cache[0x98]` (patchInfoVersion source) | `0xbfb0` |
| `0xA4` | `imagesCount()` (u32) | `0xbf78` |
| `0xA8/168` | `HIBYTE(loadInfo[+0x19])` (cache-type-ish flag) | `0xbef8` |
| `0xA9/169` | `header[0xDD] & 1` | `0xbf04` |
| `0xAA/170` | `options.forcePrivate` | `0xbe74` |
| `0xAB/171` | `Security[+0x1A]` | `0xbee4` |

If `FileIdTuple` is invalid (`FileIdTuple::operator bool` false) the ctor
`halt("dyld shared region dynamic config data was not set")` (`0xc1f8`) — the
constraint behind Q2 (§11).

### 8.6 Where the shared-cache runtime state lives

The loader path analysed here touches only `CacheInfo` (a stack struct, §8.1), the
mapped `DyldSharedCache` header, and `ProcessConfig::DyldCache` (§8.5).
`dyld4::KernelArgs` / `DyldRuntimeState` do **not** appear in
`loadDyldCache` / `mapSplitCache*` / `reuseExistingCache` / `preflight*` — they reach
this path only indirectly, via `Process` (env, platform) feeding
`Security`/`Logging`/the options blob (§2).

---

## 9. Failure-path complete map

### 9.1 Error strings → trigger sites (return-0 path)

All set `results->errorMessage` and return 0 unless noted.

| IDA | module | string |
|---|---|---|
| `0x35574` | mapSplitCacheSystemWide | `shared cache is too old, missing subcache suffixes` (gate §3.1) |
| `0x35580` | mapSplitCacheSystemWide | `Could not vm_allocate dynamic config memory` |
| `0x35758` | mapSplitCacheSystemWide | `syscall to map cache into shared region` (see §9.3) |
| `0x35704` | mapSplitCacheSystemWide | `mapped dyld cache file system wide` (verbose, success) |
| `0x35204` | reuseExistingCache | `existing shared cache in memory is not compatible` |
| `0x352a0` | reuseExistingCache | `mapped cache does not contain dynamic config data` (verbose, still returns 1) |
| `0x35288` | reuseExistingCache | `re-using existing shared cache (%s):` (verbose) |
| `0x35838` | preflightMainCacheFile | `no shared cache file` (ENOENT) |
| `0x3584c` | preflightMainCacheFile | `shared cache file open() failed` |
| `0x35938/0x3594c` | preflightSubCacheFile | same two |
| `0x35b08` | preflightCacheFile | `shared cache file stat() failed` |
| `0x35b98` | preflightCacheFile | `shared cache file pread() failed` |
| `0x35b8c` | preflightCacheFile | `shared cache file has wrong magic` |
| `0x35c6c` | preflightCacheFile | `shared cache file is for a different platform` |
| `0x35c08` | preflightCacheFile | `shared cache file mappings are invalid` |
| `0x35c50` | preflightCacheFile | `shared cache text file offset is invalid` |
| `0x35cb0` | preflightCacheFile | `shared cache code signature size is invalid` |
| `0x35ca8` | preflightCacheFile | `shared cache linkedit permissions are invalid` |
| `0x35ce4` | preflightCacheFile | `shared cache text permissions are invalid` |
| `0x35d34` | preflightCacheFile | `shared cache mappings overlap` |
| `0x35d90` | preflightCacheFile | `code signature registration for shared cache failed` |
| `0x35d84` | preflightCacheFile | `code signature does not cover entire shared cache file` |
| `0x35de4` | preflightCacheFile | `first page of shared cache not mmap()able` |
| `0x35dd8` | preflightCacheFile | `first page of mmap()ed shared cache not valid` |
| `0x35f20` | preflightCacheFile | `shared cache file subcache count exceeds limit` |
| `0x35f34` | preflightCacheFile | `no shared cache subcache indices` |
| `0x36018` | preflightCacheFile | `shared cache file pread() failed, could not read subcache entries` |
| `0x35018/0x35054` | mapSplitCachePrivate | `mmap(%d, %d) the shared cache region failed due to: %d` / `mmap() the shared cache region failed` |
| `0x350b4` | mapSplitCachePrivate | `could not mmap() dynamic config memory` |
| `0x34ffc` | mapSplitCachePrivate | `shared cache is too old, missing subcache suffixes` |
| `0xbaf4/0xbafc` | CacheFinder | `no shared cache in cryptex` → `halt()` |
| `0xbae8/0xbaf0` | CacheFinder | `ignition failed` → `halt()` |
| `0xc208/0xc210` | DyldCache ctor | `dyld private shared cache could not be found` → `halt()` |
| `0xc1f8/0xc200` | DyldCache ctor | `dyld shared region dynamic config data was not set` → `halt()` |

### 9.2 Asserts / aborts / traps (do NOT blanket-bypass — see CLAUDE.md)

| IDA | assert |
|---|---|
| `0x3511c` | `mapSplitCachePrivate` SharedCacheRuntime.cpp:1081 `slideInfoHeader->version == 5` |
| `0x3513c` | mapSplitCachePrivate SharedCacheRuntime.cpp:1116 same |
| `0x3515c` | mapSplitCachePrivate SharedCacheRuntime.cpp:1142 same |
| `0x3519c` | `growTo` Array.h:187 `0` (vm_allocate failed) |
| `0x3517c` | `push_back` Array.h:67 `_usedCount < _allocCount` |
| `0x351a0` | `__break(1u)` — PAN/SPRR check in `withWritableMemoryInternal` |
| `0x35768` | `__stack_chk_fail` |
| `0x3602c` | `std::__throw_bad_optional_access` (optional mapping entry) |

### 9.3 The `0x38D08` cold block (exit paths)

`0x35754: B loc_38D08`. `0x38D08` is **genuine dyld cold code**, not a cave:

```
38D08: ADRP X8, #_errno ; LDR W1,[X8,#_errno]      ; W1 = errno
38D10: ADRL X0, "with errno=%d"
38D18: BL   0x10102F4                                ; log/format
38D1C: MOV  W0, #0
38D20: ADRL X8, "syscall to map cache into shared region"
38D28: STR  X8, [X20,#0x10]                          ; results->errorMessage
38D2C: B    loc_35714                                ; epilogue
38D40: PACIBSP  (next function)
```

The block is exactly 56 bytes (`0x38D08..0x38D40`). The state-doc ledger's "tramp
written into the NOP cave" therefore duplicated code that already existed here. The
only extra info the block gives over `0x35758` is the `errno` value (which is how
`errno=12/ENOMEM` was observed).

---

## 10. RE conclusion — the exact incompatibility breakpoint

The incompatibility is a **single syscall**: `__shared_region_map_and_slide_2_np`
(`IDA 0x76df8`, `X16=0x218=536`), called from `mapSplitCacheSystemWide 0x35694`.

Chain of facts (each backed):

1. **What dyld submits.** `files[0..W28]` = main + subcaches + one `fd=-1`
   placeholder; `mappings[]` = every cache's 48-byte mapping entries **plus** one
   synthetic entry whose `address = CacheInfo[+0x1B0] = sharedRegionStart +
   dynamicDataOffset` (`0x35660`/`0x35670`; `CacheInfo[+0x1B0]` set at `0x35fdc`).
   *(RE-confirmed via IDA 0x35fdc, 0x35660, 0x35670)*
2. **The addresses.** With `sharedRegionStart=0x180000000` and
   `dynamicDataOffset=0x12c75c000`, the dynamic-region entry is at
   **`0x2ac75c000`**. The `.01` subcache's mappings sit past the single-cache extent
   as well. *(ground truth + IDA 0x35fdc)*
3. **The kernel limit.** iOS 16.3 (xnu-8792) fixes `SHARED_REGION_BASE=0x180000000`,
   `SHARED_REGION_SIZE=0x100000000` → the legal range is `[0x180000000,
   0x280000000)`. `0x2ac75c000 > 0x280000000`. *(xnu source, cited in state doc)*
4. **Observed result.** The syscall returns nonzero; the errno-print cold block
   (`0x38D08`) reports `errno = 12 (ENOMEM / KERN_NO_SPACE)`.
   *(runtime-confirmed via the state-doc errno tramp)*

**Therefore the precise breakpoint is step 3 inside the kernel's
`shared_region_map_and_slide_2_np` handling: at least one submitted mapping
(`0x2ac75c000`, and `.01`'s out-of-range mappings) lies outside the 4 GB region, so
the whole call is rejected and *no* part of the cache is mapped.**

Two structural consequences that the naive one-byte patches miss:

* **A failed syscall is masked** when `reuseExistingCache` then succeeds
  (`0x356e0`), and `reuseExistingCache` accepts the iOS cache on the magic alone
  (§4). So the process can appear to "work" yet be running the wrong libraries.
* **Forcing `files_count` to 1 does not remove the out-of-range address.** The
  `fd=-1` dynamic-region mapping (`0x2ac75c000`) is appended *independently* of the
  subcache count (`0x355b4`/`0x35660`), so `W28=1` still submits `0x2ac75c000`. This
  is why the state-doc `0x3538c` patch alone did not fix ENOMEM.

---

## 11. Answers to the five key questions

**Q1 — fate of `.01` after `files_count` is reduced.** `preflightCacheFile` builds
`CacheInfo` per `numSubCaches` from the main header (`subCacheArrayCount`, `0x18C`),
and `mapSplitCacheSystemWide` **only** preflights `.01` when `W28>=2` (`0x353c0`).
But the subcache is still referenced elsewhere: `numSubCaches() 0x4f224`,
`forEachCache 0x4f4ec` (walks `this + subCacheArrayOffset + 56*i` and dereferences
`this + *(u64*)entry` = the subcache header), `forEachRegion`/`forEachDylib`, and any
image lookup. With `files_count=1` (i.e. `.01` never submitted / never mapped), the
first such dereference reads an **unmapped** address → SIGSEGV. So `.01` **cannot**
just be dropped for a fully-functional cache; you must either map it, or also
suppress every subcache enumerator (which then hides images).

**Q2 — the fd=-1 dynamic-region address / `dynamicRegion()==NULL`.** The submitted
address **is** `CacheInfo[+0x1B0]` (`0x35660`), i.e. `sharedRegionStart +
dynamicDataOffset = 0x2ac75c000`. `DyldSharedCache::dynamicRegion()` (`0x50dfc`)
dereferences `this + header[0x1F0]` (same VA) to read the `"dyld_data    v3"` magic.
If the syscall failed, that page is unmapped → SIGSEGV. Patching `dynamicRegion()` to
`return 0` avoids **that** fault, but then `reuseExistingCache 0x35264` never calls
`getDyldCacheFileID`, leaving `results->fileId == 0`, which makes the
`ProcessConfig::DyldCache` ctor `halt("dyld shared region dynamic config data was
not set")` (`0xc1f8`). That is why patching only `0x50dfc` left the crash.

**Q3 — bypass point in `reuseExistingCache`.** A single `_platform_strcmp(regionBase,
"dyld_v1  arm64e")` at `0x351f0`. There is **no** UUID / path / inode check — the
magic is the only identity test. Forcing that compare to "not equal" makes dyld fall
through to `mapSplitCacheSystemWide`.

**Q4 — any legal "small region" mode.** None. The only non-shared-region path is
`mapSplitCachePrivate` (`DYLD_SHARED_REGION=private`, or `options.forcePrivate`),
which plain-mmap's executable pages (CS-killed on iOS). `DYLD_SHARED_CACHE_DIR` only
redirects the directory; there is no env/mode that mmaps the DSC as an ordinary file
inside the shared region.

**Q5 — the `slide` value.** There is **no** user-supplied slide: the call site sets
only `x0..x3` (`0x35684..0x35690`), so the syscall receives only `files_count,
files[], mappings_count, mappings[]`. The kernel computes the slide from the (unslid)
mapping addresses. `DyldSharedCache::slide()` (`0x4f240`) is used only for verbose
output. This corrects the handover's "x4 = slide" (which counted `mac_syscall`
parameters, not raw SVC registers).

---

## 12. Patch candidate list for Devin

> Labelled per CLAUDE.md patch discipline: **[SHIM] legitimate platform shim ·
> [DIAGNOSTIC] marker/symptom-suppresser, not a fix · [FIX] addresses a root cause.**
> Thin offsets; original bytes verified from the pristine IDB.

### P1 — `crossarch_trap` — **[SHIM], required, works

* thin `0x76270`; original `01 10 00 d4  c0 03 5f d6` = `SVC #0x80; RET` (syscall 38).
* target `e0 03 1f aa  c0 03 5f d6` = `MOV X0,XZR; RET`.
* basis: iOS 16.3 has no syscall 38 → the `SVC` traps to `SIGSYS(140)`. Returning 0 =
  "not translated". Semantics: a real shim (the call is a probe), not a suppression.
  risk: LOW.

### P2 — `files_count = 1` — **[DIAGNOSTIC]

* thin `0x3538c`; original `7c ca 51 b9` = `LDR W28,[X19,#0x1A8]`;
  target `3c 00 80 52` = `MOV W28,#1`.
* basis: `W28 = numSubCaches+1` drives the `.01` preflight loop (`0x353c0`) and the
  files count (`0x354f0`).
* **why it is only a diagnostic:** (a) it does **not** remove the `fd=-1` mapping at
  `0x2ac75c000`, so ENOMEM persists if that address is out of range; (b) dyld's later
  subcache enumerators (`0x4f4ec`) then dereference the unmapped `.01` → SIGSEGV.
  risk: HIGH.

### P3 — drop the `fd=-1` dynamic-region mapping — **[FIX fragment], multi-site

To remove `0x2ac75c000` from the syscall, patch **all** of:
* `0x354ec` `ADD W9,W28,#1` → `MOV W9,W28` (files_count = W28, no placeholder),
* `0x355a0..0x355b8` — skip the `files[W28] = {0xFFFFFFFF,1,0}` store,
* `0x35660..0x35680` — skip the `mappings[W23] = {0x2ac75c000,…}` store and the
  `+1` mapping count (`0x355bc`).
* **Consequence you must also handle:** the dynamic region is then never kernel-mapped;
  `dynamicRegion()` (`0x50dfc`) faults, and `results->fileId` stays 0 → `halt` at
  `0xc1f8` (see Q2). Requires a coordinated fix of that consumer. risk: HIGH.

### P4 — `dynamicRegion()` → NULL — **[DIAGNOSTIC], insufficient alone

* thin `0x50dfc`; original `08 f8 40 f9` = `LDR X8,[X0,#0x1F0]`;
  target `e8 03 1f aa` = `MOV X8,XZR` (return 0).
* basis: avoids the unmapped deref of `regionBase+0x12c75c000`.
* **why insufficient:** see Q2 — a NULL result skips `getDyldCacheFileID`, and the
  `DyldCache` ctor `halt`s when `fileId` is unset. Explains why "139 persisted".
  risk: HIGH.

### P5 — reject the iOS cache in `reuseExistingCache` — **[FIX fragment]

* thin `0x351f0` (`_platform_strcmp` result test). Force the "not equal" branch so
  dyld proceeds to `mapSplitCacheSystemWide`.
* basis: the magic is the only identity check (Q3).
* **why insufficient:** it merely routes into the syscall that then returns ENOMEM;
  only useful once P3/the region fix lands. risk: MED.

### P6 — make the mapping set fit `[0x180000000, 0x280000000)` — **[FIX], structural

This is the actual root-cause fix and is **not** a single-byte patch:
* **Trim:** emit only mappings fully inside the 4 GB region (and drop the `fd=-1`
  dynregion). But every trimmed subcache/image must then also be excluded from
  lookup, or dyld dereferences unmapped VAs.
* **Relocate:** rewrite the submitted mapping addresses **and** the cache's internal
  relative offsets consistently (very fragile — the DSC header/offsets are baked).
* **Proxy:** the AGX/Metal execution bridge described in `AGENTS.md` (architectural).

### P7 — do NOT patch these

* `0x35390`/`0x35394` — the "too old" gate is a *minimum-header* check that already
  passes for 15.6.1; patching it is pointless.
* `preflightCacheFile 0x35d70` (`F_ADDFILESIGS`) — CS registration for the DSC file;
  keep the DSC pristine (its signature is validated here and again by the kernel on
  exec-mapping). Editing the DSC header invalidates the signature.
* Any `__assert_rtn` from §9.2 — they mark real invariant breaks.

---

## Appendix A — address index (thin offsets)

| addr | symbol |
|---|---|
| `0x47c0` | `__dyld_start` |
| `0x3420c` | `dyld3::deallocateExistingSharedCache` |
| `0x34240` | `dyld3::loadDyldCache` |
| `0x342dc` | `dyld3::mapSplitCachePrivate` |
| `0x351a8` | `dyld3::reuseExistingCache` |
| `0x352bc` | `dyld3::mapSplitCacheSystemWide` |
| `0x3576c` | `dyld3::preflightMainCacheFile` |
| `0x3588c` | `dyld3::preflightSubCacheFile` |
| `0x3598c` | `dyld3::verboseSharedCacheMappings` |
| `0x35a98` | `dyld3::preflightCacheFile` |
| `0x36030` | `dyld3::openat` |
| `0x38D08` | cold error block (`syscall to map cache into shared region`) |
| `0x4f224` | `DyldSharedCache::numSubCaches` |
| `0x4f240` | `DyldSharedCache::slide` |
| `0x4f2f0` | `DyldSharedCache::unslidLoadAddress` |
| `0x4f3c4` | `DyldSharedCache::forEachRegion` |
| `0x4f4ec` | `DyldSharedCache::forEachCache` |
| `0x4f954` | `DyldSharedCache::imagesCount` |
| `0x4fa98` | `DyldSharedCache::mappedSize` |
| `0x50dfc` | `DyldSharedCache::dynamicRegion` |
| `0x511c0` | `DyldSharedCache::DynamicRegion::make` |
| `0x51244` | `DynamicRegion::size` |
| `0x51258` | `DynamicRegion::free` |
| `0x51270/0x512f4` | `DynamicRegion::set/getDyldCacheFileID` |
| `0x51278` | `DynamicRegion::setCachePath` |
| `0x512c4` | `DynamicRegion::osCryptexPath` |
| `0x52700` | `dyld3::fstatat` |
| `0x76270` | `crossarch_trap` stub (SVC #0x80) |
| `0x769cc` | `__map_with_linking_np` |
| `0x76dcc` | `__shared_region_check_np` |
| `0x76df8` | `__shared_region_map_and_slide_2_np` (SVC #0x80, #536) |
| `0xb854` | `dyld4::CacheFinder::CacheFinder` |
| `0xbc9c` | `dyld4::ProcessConfig::DyldCache::DyldCache` |
| `0x2fe34` | `dyld4::SyscallDelegate::getDyldCache` |
| `0xa2f4` | `dyld4::console` |
| `0xa9b10` | `errno` |
| `0xbb04` | `dyld4::halt` |
| `0x8b989` | `dyld_cache_header` block type-encoding |
| `0x905fa` | `"mmap() the shared cache region failed"` |
| `0x8b6ea` | `"dyld shared region dynamic config data was not set\n"` |

## Appendix B — method / reproducibility

* IDA Pro 9.2, `ida-pro-mcp` (`ida-pro-mcp-Instance1`), Hex-Rays ready.
* Every claim above is one of: a Hex-Rays listing from `decompile`, a `disasm`
  instruction, a `get_bytes` word, an `xrefs_to` edge, or an IDA type-encoding parse.
  No external Python/otool/strings.
* Kernel ABI facts (`SHARED_REGION_BASE`/`SIZE`) are cited from the xnu source already
  referenced in `dyld-15.6.1-state.md`; the syscall *rejection* is runtime-confirmed
  (errno=12) in that same document, and the error block that reports it is IDA
  `0x38D08`.


# macOS 15.6.1 local RE analysis — session 2

Date: 2026-09-24. All findings below are RE-confirmed against the actual
15.6.1 binaries (disasm via `otool -tV` / byte reads on ipsw-extracted
DSC images in `/tmp/dsc15` and the on-disk AGX bundle). No IDA was needed;
every `needs-ida` ledger item from the probe round is now resolved.

## 1. SkyLight cursor chain — fully verified

`_CGXHideCursor` @ img+`0x167e3c`, `_CGXShowCursor` @ img+`0x167fac`,
`_CGXForceShowCursor` @ img+`0x168128` (nm on `/tmp/dsc15/SkyLight`,
__TEXT vmaddr `0x1864af000`).

Prologue at `0x167e3c` = `d503237f d10103ff a9024ff4 a9037bfd 9100c3fd`
— **byte-identical** to the 13.4 `expectedPrologue` in
`MacWSSkyLightCursorABIValid` (mac_hooks.m:1551). Earlier ledger note
claiming word-5 differs was a transcription error; corrected.

Disasm chain (CGXHideCursor +0x14..+0x40, CGXShowCursor +0x24..+0x50):

```
adrp x19, 0x1ebf5e000 ; ldr x8, [x19,#0xfe0]   ; x8 = *(0x1ebf5efe0) = WS::Globals*
ldr  x8, [x8,#0x20]                          ; globals+0x20 = CGXSession*   (SAME)
ldr  x8, [x8,#0x100]                         ; session+0x100 = cursorState  (13.4: +0x108)
ldr/str x9/x10, [x8,#0x78]                   ; cursorState+0x78 = hide count (SAME)
```

Port deltas for `MacWSSkyLightCursorHideCount` / `MacWSHideNativeCursorOnMainThread`:

| constant | 13.4 | 15.6.1 |
|---|---|---|
| `kCGXHideCursorOffset` | `0x1322b0` | `0x167e3c` |
| prologue sig | 5 words | unchanged |
| `kWSGlobalsPointerOffset` | `0x53ae8460` | `0x65aaffe0` (= `0x1ebf5efe0 - 0x1864af000`) |
| `globals +` → session | `0x20` | `0x20` unchanged |
| `session +` → cursorState | `0x108` | **`0x100`** |
| `cursorState +` → hide count | `0x78` | `0x78` unchanged |

Post-transition behavior identical: `WS::Displays::CAManager()` →
vtable `+0x300` (`set_cursor_hidden`), `update_session_cursor_window`,
`post_notification(0x5e3)`. No semantic change.

## 2. `MetalContext::EndUpdate` — signature widened, semantics same

15.6.1 symbol: `__ZN12MetalContext9EndUpdateEbb` @ img+`0x1867b8`
(va `0x1866357b8`). Disasm:

```
ldr w8,[x0,#0x178]      ; update depth — SAME field as 13.4
subs w8,w8,#1; str; b.ne ret
x21 = arg1 (x1)         ; gates CARenderOGLEndRendering + Flush arg1
x20 = arg2 (x2)         ; -> Flush arg4
Flush(this, x21, 0, 0, x20)
```

`hooked_skylight_end_update` adapts trivially: typedef gains a second
`bool`, forwards both args; the `self+0x178 == 1` outermost check is
unchanged. The 13.4 name `__ZN12MetalContext9EndUpdateEb` no longer
exists — MSFindSymbol must use the `Ebb` name (or the offset above).

`__ZNSt3__15dequeI11RenderStateNS_9allocatorIS1_EEE8pop_backEv` @
img+`0x1851a0` exists; deque layout (`+0x28` size) must still be verified
at runtime but symbol path is intact.

## 3. QuartzCore `update_image` — layout drift inside Render::Image

`__ZN2CA3OGL12MetalContext12update_imageEPNS0_10MetalImageEPNS_6Render5ImageEjPKc`
@ img+`0x6e9cc` (va `0x1896299cc`, __TEXT vmaddr `0x1895bb000`).
New 9-word prologue: `d503237f d10303ff a9066ffc a90767fa a9085ff8
a90957f6 a90a4ff4 a90b7bfd 9102c3fd`.

Verified field map (x19=MetalImage, x20=Render::Image):

| field | 13.4 | 15.6.1 | evidence |
|---|---|---|---|
| MetalImage+0x40 texture | same | same | `ldr/str [x19,#0x40]` @0x9629b04/0x9629ba0 |
| MetalImage+0x58 descriptor | same | same | `newTextureWithDescriptor:` arg |
| MetalImage+0x78 planeCount | same | same | `ldrh cmp #1` @0x9629b5c |
| MetalImage+0x7b flags | same | same | `ldurh [x19,#0x7b]` @0x9629aac/0x9629c0c |
| flag bit → didModifyData | `0x200` | **`0x200` same** | `tbnz w8,#9 → didModifyData` @0x9629c10 |
| Render::Image+0x10/0x14 w/h | same | same | `ldp w24,w23,[x20,#0x10]` |
| Render::Image+0x60 source | `0x60` | **`0x68`** | `ldr x22,[x20,#0x68]`; x22 → `withBytes:` |
| Render::Image+0x99 mipCount | `0x99` | **`0xa1`** | `ldrb [x20,#0xa1]` cmp 1/2 |
| Render::Image+0xa0 bytesPerRow | `0xa0` | **`0xa8`** | `ldr x6,[x20,#0xa8]` → bytesPerRow arg; per-mip array at `0xa8+mip*8` |

New path note: when texture must be created, 15.6.1 calls
`copy_image_to_texture(image, metalImage)` gated on `x22!=0 &&
planeCount==1 && !(flags & 0x400) && mip==1` — but the *existing-texture*
path the hook repairs (`didModifyData` no-op on IOGPUMetalTexture) is
unchanged. Hook body needs the three Render::Image offsets updated;
flag semantics identical.

## 4. Metal `dyld_get_active_platform` — 5 sites re-derived

`MTLLibraryBuilder::newLibraryWithSource` @ img+`0x138008`
(va `0x18b9aa008`, __TEXT vmaddr `0x18b872000`). Exactly **5** `bl`
calls to the `dyld_get_active_platform` stub inside the function —
same count as 13.4. LR (return-address) image offsets for
`source_builder_platform_returns[]`:

```
0x138908  0x138920  0x138a88  0x139088  0x1390c0
```

Metal UUID: `F83EE1A6-49CC-3A46-80AF-1B8B07EE0322`.

## 5. AGX `objc_msgSendSuper2` stub — solved without IDA

`otool -Iv -arch arm64e AGXMetal13_3` shows two bind slots for
`_objc_msgSendSuper2`: `0x63bc8c` (stub) and `0x69c280` (GOT).
Scanning all 1163 `adrp x17;add x17;ldr x16,[x17];braa x16,x17`
auth-stubs in the arm64e slice (`__auth_stubs` shape verified:
`d0000311 91xx0231 f9400230 d71f0a11`):

```
stub @ img+0x63bc8c  ->  loads GOT 0x69c280  ->  _objc_msgSendSuper2
```

13.4 site was `0x85a628`; 15.6.1 replacement site = **`0x63bc8c`**.

## 6. `MacWSAGXNoCopyABIReady` — all-macOS axis, values re-derived

The gate's two IMPs come from `method_getImplementation` on classes in
the **macOS** images loaded in the chroot — both macOS-side:

| term | 13.4 | 15.6.1 |
|---|---|---|
| agx uuid | `727C250E-554D-3921-A5B3-48DAE6195B79` | `B303B4E8-5F17-39B8-8505-326AAF870F39` |
| agx init offset | `0x1f4bb4` | `0x16c178` (`-[AGXBuffer initWithDevice:bytes:length:options:deallocator:pinnedGPUAddress:]`) |
| iogpu uuid | `CE2B5551-857F-3EDD-9E4F-435215CC8C27` | `68B70E83-BBDA-3411-A487-E52797174518` |
| iogpu init offset | `0x1c24` | `0x1b14` (`-[IOGPUMetalBuffer initWithDevice:pointer:…deallocator:]`, va `0x1a1f9ab14` − vm `0x1a1f99000`) |
| `kern.osversion` | `"20D67"` | **open question — see §8** |

## 7. IOMobileFramebuffer — swap_id field moved (important)

15.6.1 `kern_SwapBegin` @ img+`0x5384`... wait, @ `0x18d793384` →
img+`0x5384`; `kern_SwapEnd` @ img+`0x5450`; public
`_IOMobileFramebufferSwapEnd` wrapper @ img+`0x1750`.

Disasm-verified `fb` (public IOMFB object) layout:

| field | 13.4 | 15.6.1 |
|---|---|---|
| `fb+0x14` io_connect | `0x14` | `0x14` same (`ldr w0,[x0,#0x14]` in both kern_SwapBegin+0x3c and kern_SwapEnd+0x18) |
| `fb+0x18` sel-5 input struct base | `0x18` | `0x18` same (`add x2,x19,#0x18`) |
| input struct size | `0x468` → patched `0x46c` | `0x514` (`mov w3,#0x514` = `0x5280a283` @0x5474) |
| `fb+0x68` active swap id | `0x68` | **`0xb0`** (`str w8,[x19,#0xb0]` in SwapBegin+0x8c) |
| swap id inside input struct | `inStruct+0x50` | **`inStruct+0x98`** (`0xb0-0x18`) |
| gain-map ref | ? | `fb+0xce0` |
| underrun counter | ? | `fb+0x7f0` |
| wrapper conn/fptr field | `fb+0x728` | `fb+0x880` (`f9447001`) |

Consequence: on 15.6.1 the `inStructCnt 0x514→0x46c` patch alone is NOT
sufficient — the iOS kernel still reads the swap id at `inStruct+0x50`
(same iOS 16.3.1 kernel binary, layout fixed) but macOS 15.6.1 writes it
at `inStruct+0x98`. `IOConnectCallStructMethod_new` and
`MacwsIOMobileFramebufferSwapEnd_new` must:
- read real swap id from `fb+0xb0` / `inStruct+0x98` (not `+0x50`/`fb+0x68`),
- place it where the iOS kernel expects (`inStruct+0x50`) before the
  selector-5 call forwards — or translate via SwapCancel path as today.

This is a semantic ABI drift, not a blocker — but it must be handled in
the hook, not just the `mov w3` patch.

## 8. Open architecture question (affects every iOS-axis gate)

`MacWSAGXNoCopyABIReady` ends with `kern.osversion == "20D67"` — a
kernel-build check for iPadOS 16.3.1. Several interposes likewise assume
the kernel underneath is iOS 16.3.

If the planned 15.6.1 container runs on **this VM's macOS 15.6.1 kernel**
(`kern.osversion = 24G90`), every `kern.osversion`-gated feature
self-disables and all iOS-kernel-ABI translations (IOMFB sel-5 struct,
IOGPU selector table, AGX IOC expectations) become wrong or unnecessary.
If the container still runs on the iPad's iOS 16.3 kernel, they stay
required and the values above apply.

This must be answered before writing the port patches: it decides
whether the `20D67` term becomes `24G90`, is dropped, or stays.

## 9. iOS-axis version resolution

Code comments consistently say "iPadOS 16.3/16.3.1 runtime" (dozens of
runtime-confirmed notes). AGENTS.md's "iOS 16.5" refers to the
**iPhoneOS 16.5 Theos SDK** (build-time headers), not the runtime
target. Runtime axis = iPadOS 16.3.1 / kernel `20D67`. Resolved — no
hidden second target.

## 10. Image.qlgenerator

`GenerateThumbnailForURL` @ file-offset **`0x860`** in the 15.6.1 arm64e
slice (`/System/Library/QuickLook/Image.qlgenerator/Contents/MacOS/Image`,
UUID `B6E7BDE8-EFED-3F4B-9992-1CC96483DC8A`). Calls
`QLThumbnailRequestSetImageAtURL` at func+`0x5c` after building a
`kCGImageSourceTypeIdentifierHint` dict — identical shape to 13.4
(entry was `0x3b30`, call also at +`0x5c`).

## Extraction/tooling note

`dyldex` cannot parse the 15.6.1 DSC (newer slide-info format →
`TypeError` in `slide_info.py`). Working extraction command:

```bash
ipsw-a2sb dyld extract <DSC> <dylib-path> --slide -o <outfile>
```

15.6.1 extracted images carry full local symbol tables (`t`/`T`/`s`
symbols), which is what made this whole no-IDA round possible.

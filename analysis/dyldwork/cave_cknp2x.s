// cave_cknp2x.s — @0x35380 (mapSplitCacheSystemWide entry, BEFORE syscall 536).
// Calls shared_region_check_np(294, &base). Writes {ret, base} FIRST (16B),
// then only if ret==0 && base!=0 reads 16B cache magic and writes it.
// Restores x0, replays `mov x8, x0`, branches back to 0x35384.
    .text
    .globl _cave
_cave:
    sub  sp, sp, #0x100
    str  x0, [sp, #0xf8]       // save incoming x0 (replayed into x8 later)
    str  xzr, [sp]             // base = 0 (init so garbage never used)
    mov  x0, sp                // &base
    mov  x16, #294             // shared_region_check_np
    svc  #0x80
    str  x0, [sp, #0x10]       // ret
    mov  w0, #2
    add  x1, sp, #0x10         // &{ret,base}
    mov  x2, #16
    mov  x16, #4
    svc  #0x80                 // write(2, &{ret,base}, 16)   <- REPORT FIRST
    ldr  x9, [sp, #0x10]       // ret
    cbnz x9, _skip             // ret!=0 -> no valid base
    ldr  x9, [sp]              // base
    cbz  x9, _skip
    ldp  x10, x11, [x9]        // cache magic (valid only if ret==0)
    stp  x10, x11, [sp, #0x20]
    mov  w0, #2
    add  x1, sp, #0x20
    mov  x2, #16
    mov  x16, #4
    svc  #0x80                 // write(2, magic, 16)
_skip:
    ldr  x9, [sp, #0xf8]       // orig x0
    add  sp, sp, #0x100
    mov  x8, x9                // replay: MOV X8, X0
    b    . + 4                 // placeholder, fixed to `b 0x35384` by injector

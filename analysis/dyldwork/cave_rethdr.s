// cave_rethdr.s — post-536 landing @0x35698 (x0 = raw syscall ret).
// 1) write(2, &x0, 8)                      — 536 return value
// 2) if ret==0: read 16B at 0x180000000    — proof the cache really mapped
//    (slide0 patch pins base at 0x180000000) and write(2, hdr, 16)
// 3) replay mov x23,x0; b 0x3569c
    .text
    .globl _cave
_cave:
    sub  sp, sp, #0x40
    str  x0, [sp, #8]          // save ret
    mov  w0, #2
    add  x1, sp, #8
    mov  x2, #8
    mov  x16, #4
    svc  #0x80                 // write(2,&ret,8)
    ldr  x9, [sp, #8]          // ret
    cbnz x9, _done
    mov  x8, #0x180000000      // expected base (slide=0)
    ldr  x9, [x8]
    str  x9, [sp, #0x20]
    ldr  x9, [x8, #0x10]
    str  x9, [sp, #0x28]
    mov  w0, #2
    add  x1, sp, #0x20
    mov  x2, #16
    mov  x16, #4
    svc  #0x80                 // write(2,hdr,16)
_done:
    ldr  x0, [sp, #8]          // restore ret
    add  sp, sp, #0x40
    mov  x23, x0               // replay 0x35698 insn
    b    . + 4                 // placeholder -> b 0x3569c (fixed by injector)

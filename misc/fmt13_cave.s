// fmt13_cave.s — DYLD_CHAINED_PTR_ARM64E_SHARED_CACHE (=13) handler for the
// xnu-8792 dyld_pager fixup switch, backporting xnu-12377's
// fixupCachePageAuth64() semantics.
//
// Cave body is dropped over the dead case-2/case-3/case-6 region inside
// dyld_pager_data_request (IDB 0xfffffe0008066848..0x80669f0). The B.HI
// bounds branch at 0xfffffe00080667f4 is redirected here; jump-table slots
// for formats 2/3/6 (0x8066c14/0x8066c18/0x8066c24) are repointed at the
// original default (0x8066670) so dead formats still slide-error.
//
// Register context live at the B.HI site (verified against IDB disasm):
//   X0  = userVA                 (range loop: LDR X11,[X23+X9*8,#0x60]; ADD X0,..)
//   X2  = link_info (mwl hdr)    (LDR X2,[X23,#0x28] at 0x80665d0)
//   X4  = segInfo                (chains base + seg_info_offset[i])
//   W5  = pageIndex              ((userVA-segStart)>>0xE)
//   X8  = contents + 0x4000      (page end)
//   X9  = link_info + link_info_size
//   X23 = pager
//   W10 = mwli_pointer_format, W16 = fmt-1
//   [SP,#0x38] = contents (kernel VA of destination UPL page)
//
// Upstream semantics (xnu-12377 fixupCachePageAuth64):
//   bound: &seg->page_start[idx+1] > link_info_end        -> KERN_FAILURE
//   start == 0xffff                                      -> success
//   per chain entry value:
//     next  = (v>>52)&0x7FF; advance = next*8
//     auth  = v>>63
//     auth=1: target = (v & 0x3FFFFFFFF) + image_address
//             diversity=(v>>34)&0xFFFF; addrDiv=bit50; keyIsData=bit51
//             key = keyIsData ? asda(2) : asia(0)
//             diversifier = addrDiv ? (uVA|diversity<<48) : diversity
//             uVA = userVA + (chain - contents)
//             target==0 -> store 0; a_key==0 -> store raw target
//             else *chain = ppl_sign_op0x3C(target,key,diversifier,a_key)
//     auth=0: *chain = image_address + (v & 0x3FFFFFFFF)
//                       + ((v<<22) & 0xFF00000000000000)
//
// External fixup sites filled by misc/fmt13_patch.py at deploy time:
//   E0 "b.ne DFL"   -> 0xfffffe0008066670 (slide-error block, no stack touched)
//   E1 "b.cc FAIL"  -> FAIL label below   (internal, but emitted via table too)
//   E2 "b.eq OK"    -> OK label           (internal)
//   E3 "b PPL_SIGN" -> 0xfffffe0007f2bff4 (ppl dispatch, op 0x3c)
//   E4 "b RET_OK"   -> 0xfffffe0008066694 (paging_end + UPL completion)
//   E5 "b RET_DFL"  -> 0xfffffe0008066670 (slide-error block)
// Internal-loop branches (auth/next/loop/stor/nosign/OK/FAIL) assemble
// position-independently inside the cave.

	.text
	.align	2
fmt13_cave:
	cmp	w10, #13
	b.ne	fail_nostk			// fmt != 13 -> original default
	stp	x19, x20, [sp, #-48]!
	stp	x21, x22, [sp, #16]
	mov	x19, x0				// userVA
	ldr	x20, [sp, #0x68]		// contents (old_sp+0x38 = sp+0x68)
	mov	x21, x8				// end
	ldr	x22, [x2, #0x20]		// mwli_image_address

	add	w13, w5, #1
	add	x13, x4, w13, uxtw #1
	add	x13, x13, #0x16
	cmp	x9, x13				// li_end < &page_start[idx+1]
	b.cc	fail

	add	x13, x4, w5, uxtw #1
	ldrh	w13, [x13, #0x16]		// page_start[pageIndex]
	mov	w14, #0xffff
	cmp	w13, w14
	b.eq	ok

	add	x14, x20, w13, uxth		// chain = contents + start
loop:
	cmp	x20, x14
	b.hi	fail				// chain < contents
	add	x13, x14, #8
	cmp	x13, x21
	b.hi	fail				// chain+8 > end
	ldr	x11, [x14]			// value
	ubfx	x16, x11, #52, #11		// next
	tbnz	x11, #63, auth

	and	x13, x11, #0x3ffffffff		// runtimeOffset
	add	x13, x13, x22			// + image_address
	lsl	x17, x11, #22
	and	x17, x17, #0xff00000000000000	// high8
	add	x13, x17, x13
	str	x13, [x14]
	b	next

auth:
	sub	x10, x14, x20
	add	x10, x10, x19			// uVA = userVA + (chain - contents)
	ubfx	x12, x11, #34, #16		// diversity
	bfi	x10, x12, #48, #16		// uVA | diversity<<48
	tst	x11, #(1 << 50)			// addrDiv
	csel	x2, x10, x12, ne		// diversifier
	ubfx	x1, x11, #51, #1
	lsl	x1, x1, #1			// key: 0=asia / 2=asda
	and	x10, x11, #0x3ffffffff
	add	x10, x10, x22			// target
	cbz	x10, stor			// target==0 -> store raw zero
	stp	x14, x16, [sp, #-16]!
	mov	x0, x10				// X0 = target
	ldr	x3, [x23, #0xb0]		// pager->dyld_a_key
	cbz	x3, nosign
	bl	PPL_SIGN			// ppl op 0x3c -> X0 = signed ptr
	mov	x10, x0
nosign:
	ldp	x14, x16, [sp], #16
stor:
	str	x10, [x14]
next:
	add	x14, x14, x16, lsl #3		// chain += next*8
	cbnz	x16, loop

ok:
	ldp	x21, x22, [sp, #16]
	ldp	x19, x20, [sp], #48
	mov	w25, #0
	b	RET_OK				// -> 0xfffffe0008066694
fail:
	ldp	x21, x22, [sp, #16]
	ldp	x19, x20, [sp], #48
fail_nostk:
	b	RET_DFL				// -> 0xfffffe0008066670

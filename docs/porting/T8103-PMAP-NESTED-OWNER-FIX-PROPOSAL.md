# T8103 nested-pmap owner-selection fix proposal (2026-10-03)

This is an offline review artifact. It is not a device patch and must not be
implemented by KRW/PAC writes.

## Evidence

A fresh frozen `/bin/echo HI` child on iPad13,11/T8103 was rechecked after the
previous experiment. Runtime output remains:

```text
[exc] type=1 code0=0x32 code1=0x18047dc9c
```

The same child showed:

```text
outer pmap association: [0x180000000,0x1e7f5c000) -> 24G90 CD, trust=8
nested_region: [0x180000000,0x280000000)
nested pmap association-tree root: 0
nested ASID bitmap word[0]: 0xffffffff (target twig bit set)
```

The target cache page, CodeDirectory slot[287], page validation state, and
outer PMAP-CS node are already aligned; see `dyld-15.6.1-state.md` dated
2026-10-03 entries.

## Static contract

RE-confirmed in the actual T8103 IDA database, `sub_FFFFFE00086A0924` does:

1. Start with `owner = pmap`.
2. If `pmap+0xc9 != 3` and `nested_pmap != NULL`, test only
   `nested_region_addr <= va < nested_region_addr + nested_region_size`.
3. On a range hit, set `owner = pmap->nested_pmap` and subtract
   `nested_region_addr` from the VA.

It does not test `nested_pmap->nested_region_asid_bitmap`.

Public XNU `pmap_unnest_options_internal` marks an unnested twig by setting
that bitmap bit, then clears the outer pmap twig PTE. The current runtime
bitmap proves that unnest completed before the cache fault. Therefore the
owner selector must treat a set bitmap bit as “stay in the outer pmap”.

## Review-level pseudocode

```c
owner = pmap;
if (pmap->type != PMAP_TYPE_NESTED && pmap->nested_pmap != NULL &&
    pmap->nested_region_addr <= va &&
    va < pmap->nested_region_addr + pmap->nested_region_size) {
    size_t twig = (va - pmap->nested_region_addr) >> twig_shift;
    if (!testbit(twig, pmap->nested_pmap->nested_region_asid_bitmap)) {
        owner = pmap->nested_pmap;
        va -= pmap->nested_region_addr;
    }
}
```

The exact T8103 field offsets, pointer authentication, lock context, and PPL
mirror must be re-derived from the target kernel source/build before any
implementation. A source-level kernel build and a boot/recovery plan are
required; no binary byte patch is authorized by this document.

## Acceptance and rollback requirements

A future implementation must first pass static checks against the exact
T8103 kernel identity, then a single child-scoped echo A/B. Success requires
real `HI`, no `code0=0x32`, and preserved outer/nested bitmap invariants.
Any failure restores the original kernel/dyld state and reboots before another
trial. Until that review gate exists, the supported device state remains the
original dyld with no kernel modification.

## Exact T8103 instruction window (IDA 13337)

The owner switch is the following 21-instruction function:

```text
0xfffffe00086a0928  LDRB W9, [X0,#0xC9]
0xfffffe00086a092c  CMP  W9, #3
0xfffffe00086a0930  B.EQ 0xfffffe00086a093c
0xfffffe00086a0934  LDR  X8, [X0,#0x58]
0xfffffe00086a0938  CBZ  X8, 0xfffffe00086a0970
0xfffffe00086a093c  LDR  X8, [X0,#0x60]
0xfffffe00086a0940  CMP  X8, X1
0xfffffe00086a0944  B.HI 0xfffffe00086a0970
0xfffffe00086a0948  LDR  X10,[X0,#0x68]
0xfffffe00086a094c  ADD  X10,X10,X8
0xfffffe00086a0950  CMP  X10,X1
0xfffffe00086a0954  B.LS 0xfffffe00086a0970
0xfffffe00086a0958  CMP  W9,#3
0xfffffe00086a095c  B.EQ 0xfffffe00086a096c
0xfffffe00086a0960  LDR  X8,[X0,#0x58]
0xfffffe00086a0964  STR  X8,[X2]
0xfffffe00086a0968  LDR  X8,[X0,#0x60]
0xfffffe00086a096c  SUB  X1,X1,X8
```

A real fix must insert the bitmap test between the bounds hit and the
`LDR/STR owner` sequence, while preserving arm64e control-flow integrity and
all caller ABI/locking assumptions. This listing is an audit precondition,
not a patch authorization.

## Field/layout manifest for implementation review

The public XNU arm pmap layout used to cross-check the T8103 fields is:

```text
pmap+0x58  nested_pmap
pmap+0x60  nested_region_addr
pmap+0x68  nested_region_size
pmap+0x70  nested_region_true_start
pmap+0x78  nested_region_true_end
pmap+0x80  nested_region_asid_bitmap
pmap+0x88  nested_region_asid_bitmap_size
pmap+0xc9  pmap type byte in the T8103 layout
```

The current runtime sample had `nested_region_addr=0x180000000`,
`nested_region_size=0x100000000`, bitmap word zero equal to `0xffffffff`,
and the target VA in twig index zero. The proposed check must use the actual
T8103 page-table attribute/twig shift, not a hard-coded `24`, before reading
the bitmap. It must also preserve the existing `pmap+0xc9 == 3` behavior for
nested pmaps and the current `owner`/relative-VA output ABI.

A review implementation should therefore have these assertions before any
binary work:

1. Exact T8103 kernel UUID/hash and imagebase match Instance1.
2. The instruction window at `0xfffffe00086a0928..0xfffffe00086a0974`
   matches the IDA listing in this document.
3. The bitmap pointer and twig shift are obtained from the target kernel's
   layout/IDA, not inferred from the current child alone.
4. A source-level or emulated unit test covers both bitmap states: bit clear
   selects nested owner; bit set keeps outer owner.
5. Deployment remains prohibited until a signed, reversible kernel artifact,
   recovery image, and child-scoped `/bin/echo HI` acceptance procedure are
   independently reviewed.

## Child-scoped type-gate A/B (2026-10-03)

A reversible KRW A/B changed only the live echo child's outer pmap byte
`pmap+0xc9` from `0` to `3` while the exception runner held the child at the
first fault. The write returned `0` and read back `0x3`. The child then
continued through the runner and still produced:

```text
[exc] type=1 code0=0x32 code1=0x18047dc9c
[exc] type=10 code0=0xa100032 code1=0x18047dc9c
```

There was no `HI`. The byte was restored to `0` with a successful KRW write
before cleanup. **runtime-confirmed**: forcing the visible outer pmap type gate
alone is not sufficient. This does not invalidate the bitmap hypothesis; it
means either the helper receives/uses another pmap at the rejecting stage, or
a later PMAP-CS gate still returns 50. No kernel text, PAC pointer, PTE, or
global policy was modified.

## Additional child-scoped A/B results (2026-10-03)

Two reversible data-only experiments now bound the proposal:

1. Outer pmap `+0xc9` changed `0 -> 3` while the child was held at the first
   fault. The write succeeded, but the child still returned `code0=0x32` and
   then `0xa100032`. The byte was restored to `0`.
2. Outer pmap `nested_region_size` (`+0x68`) changed
   `0x100000000 -> 0` while held at the first fault. The child still returned
   the same 50 path. The original size was restored.

A separate child-scoped `nested_pmap` pointer-null A/B also succeeded as a
write and was restored, with the same 50 result. These are runtime-confirmed
negative A/Bs: changing the visible owner-selector inputs alone is not
sufficient; the rejecting PMAP/PPL gate is later or receives a different
pmap/argument. No global kernel state, kernel text, PAC pointer, or PTE was
left modified.

A direct `kwrite32` attempt at the exact T8103 kernel instruction window was
blocked by PPL/KTRR (SSH call did not return; subsequent read still matched
`0xf9402c08`). No kernel text byte changed.

## Additional diagnostics and text-patch boundary (2026-10-03)

After reboot/re-jailbreak, 24G90 trust was restored via the explicitly
authorized temporary trust diagnostic. A fresh original-dyld echo baseline
still produced `code0=0x32` at `0x18047dc9c`.

A direct `kwrite32` precondition check for T8103 runtime instruction
`0xfffffe002de1c934` (IDA `0xfffffe00086a0934`, original `0xf9402c08`) was
performed. The write call did not return and a subsequent read remained
`0xf9402c08`, confirming the documented PPL/KTRR text-write boundary. No text
byte changed.

Child-scoped data A/Bs for `pmap+0xc9`, `nested_pmap`, and
`nested_region_size` all wrote and restored successfully but left the first
50 fault unchanged. `fmt13_patch.py` was not run; its format-13 pager change
is orthogonal to the now-dominant T8103 PMAP/PPL owner/validation failure.

## Offline kernelcache candidate (not deployed, 2026-10-03)

IDA 13337 maps `sub_FFFFFE00086A0924+0x10` to raw kernelcache file offset
`0x169c934`. The exact original word is `0xf9402c08` (`LDR X8,[X0,#0x58]`).
An offline raw candidate replaced it with `0xd2800008` (`MOV X8,#0`), forcing
this owner selector to retain the outer pmap for non-type-3 pmaps.

Candidate:

```text
/tmp/kc_raw_t8103_owner_typefix.bin
SHA-256 ed42a7ab1b0b63c438c6cd2d026d27629939fdf8cb989b49e688e3afff97f283
```

The candidate is deliberately outside Git. It has not been compressed into a
new signed IMG4 and has not been copied to preboot or booted. The original
IMG4 SHA remains the device baseline. A boot-level trial requires preserving
the original IMG4, rebuilding the payload/container with a valid signing/boot
chain, and an explicit recovery path; simply replacing the preboot file is
expected to fail Apple IMG4/kernel signature validation. This candidate is an
offline review artifact, not runtime evidence or a deployment authorization.

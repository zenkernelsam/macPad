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

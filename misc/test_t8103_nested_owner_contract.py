#!/usr/bin/env python3
"""Model test for the T8103 nested-pmap owner-selection proposal.

This is a source-contract test only. It does not read or write a device kernel
and is not a runtime acceptance test.
"""
import unittest


REGION_START = 0x180000000
REGION_SIZE = 0x100000000
TWIG_SHIFT = 24  # current T8103/16K runtime geometry; implementation must rederive it


def select_owner(*, pmap_type, nested_owner, region_start, region_size,
                 bitmap_words, va):
    """Proposed semantics: set ASID bit means the twig is already unnested."""
    owner = "outer"
    relative = va
    if pmap_type != 3 and nested_owner is not None:
        if region_start <= va < region_start + region_size:
            twig = (va - region_start) >> TWIG_SHIFT
            word = twig >> 5
            bit = twig & 31
            unnested = bool(bitmap_words[word] & (1 << bit))
            if not unnested:
                owner = "nested"
                relative = va - region_start
    return owner, relative


class NestedOwnerContractTests(unittest.TestCase):
    def test_unset_bitmap_selects_nested_owner(self):
        owner, relative = select_owner(
            pmap_type=0, nested_owner=object(),
            region_start=REGION_START, region_size=REGION_SIZE,
            bitmap_words=[0], va=0x18047DC9C)
        self.assertEqual(owner, "nested")
        self.assertEqual(relative, 0x47DC9C)

    def test_set_bitmap_keeps_outer_owner(self):
        owner, relative = select_owner(
            pmap_type=0, nested_owner=object(),
            region_start=REGION_START, region_size=REGION_SIZE,
            bitmap_words=[0xFFFFFFFF], va=0x18047DC9C)
        self.assertEqual(owner, "outer")
        self.assertEqual(relative, 0x18047DC9C)

    def test_outside_region_keeps_outer_owner(self):
        owner, relative = select_owner(
            pmap_type=0, nested_owner=object(),
            region_start=REGION_START, region_size=REGION_SIZE,
            bitmap_words=[0], va=0x280000000)
        self.assertEqual(owner, "outer")
        self.assertEqual(relative, 0x280000000)

    def test_nested_pmap_type_preserves_existing_behavior(self):
        owner, relative = select_owner(
            pmap_type=3, nested_owner=object(),
            region_start=REGION_START, region_size=REGION_SIZE,
            bitmap_words=[0], va=0x18047DC9C)
        self.assertEqual(owner, "outer")
        self.assertEqual(relative, 0x18047DC9C)


if __name__ == "__main__":
    unittest.main()

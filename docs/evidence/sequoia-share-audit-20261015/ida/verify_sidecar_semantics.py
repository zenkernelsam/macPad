"""Offline file audit of the supplied sidecars, derived from IDA evidence.

No executable is run or patched. Layout comes from the actual dyld consumer
at 0x9b680..0x9b910 and XNU dyld_pager.h / fixup-chains.h. Compare all v5
chain entries with format-12 entries, including resolved targets, PAC inputs,
chain progression, page starts, and bytes outside the chain entries.
This validates file semantics, not actual kernel page-in/PAC execution.
"""
from pathlib import Path
import collections
import json
import mmap
import struct
import time

REPO = Path(__file__).resolve().parents[4]
PAYLOAD = REPO / 'analysis/share-review-20261015/extracted/rootfs'
OUT = Path(__file__).with_name('sidecar-semantics.json')
EXPECTED_UUIDS = ['4c1223e5cace3982a0036110a7a8a25c',
                  '2b390646b4b5302b841aefaa5283640d']


def read(fmt, data, offset):
    return struct.unpack_from('<' + fmt, data, offset)


def audit(index):
    cache = PAYLOAD / ('System/Library/dyld/dyld_shared_cache_arm64e' +
                       ('.01' if index else ''))
    sidecar = PAYLOAD / f'usr/local/share/macpad-pager15/sidecar{index}.dylib'
    with cache.open('rb') as cf, sidecar.open('rb') as sf:
        with mmap.mmap(cf.fileno(), 0, access=mmap.ACCESS_READ) as c, \
             mmap.mmap(sf.fileno(), 0, access=mmap.ACCESS_READ) as s:
            assert c[0x58:0x68].hex() == EXPECTED_UUIDS[index]
            magic, blob_len, nregions = read('8sII', s, 0x4000)
            assert magic == b'MPAGER01' and 0 < nregions <= 5
            blob = 0x40b0
            version, page_size, fmt, bind_off, bind_count, chain_off, chain_size, slide, image = read('IHHIIIIQQ', s, blob)
            assert (version, page_size, fmt, slide, image) == (7, 0x4000, 12, 0, 0x180000000)
            assert bind_off + bind_count * 8 <= blob_len
            assert chain_off + chain_size <= blob_len
            map_off, map_count = read('II', c, 0x138)
            assert map_count <= 16
            maps = [read('6QII', c, map_off + j * 56) for j in range(map_count)]
            regions = [read('iIQQQ', s, 0x4010 + j * 32) for j in range(nregions)]
            chains = blob + chain_off
            segment_count, = read('I', s, chains)
            assert segment_count <= 16
            segments = []
            for j in range(segment_count):
                rel, = read('I', s, chains + 4 + j * 4)
                if rel:
                    seg = chains + rel
                    seg_size, ps, pf, seg_off, maxptr, pc = read('IHHQIH', s, seg)
                    assert seg_size >= 22 + pc * 2 and ps == page_size and pf == 12
                    assert seg + seg_size <= chains + chain_size
                    segments.append((seg, seg_off, pc))
            result = {'sidecar': sidecar.name, 'pointer_format': fmt,
                      'regions': [], 'counts': collections.Counter(), 'examples': []}
            for _fd, protection, soff, va, size in regions:
                matching = [m for m in maps if m[0] == va and m[1] == size]
                assert len(matching) == 1
                _va, _size, coff, slide_off, slide_size, _flags, _maxprot, initprot = matching[0]
                assert protection == (initprot & 7) and slide_size > 0
                sv, ps, pc = read('III', c, slide_off)
                value_add, = read('Q', c, slide_off + 16)
                assert sv == 5 and ps == page_size and pc * ps == size
                assert 24 + pc * 2 <= slide_size
                candidates = [x for x in segments if x[1] + image == va]
                assert len(candidates) == 1
                seg, _seg_off, seg_pc = candidates[0]
                assert pc == seg_pc
                rc = collections.Counter()
                for page in range(pc):
                    orig_start, = read('H', c, slide_off + 24 + page * 2)
                    new_start, = read('H', s, seg + 22 + page * 2)
                    assert orig_start == new_start
                    orig_page = c[coff + page * ps:coff + (page + 1) * ps]
                    new_page = s[soff + page * ps:soff + (page + 1) * ps]
                    assert len(orig_page) == len(new_page) == ps
                    reconstructed = bytearray(orig_page)
                    pos = orig_start
                    seen = set()
                    while pos != 0xffff:
                        assert pos % 8 == 0 and pos + 8 <= ps and pos not in seen
                        seen.add(pos)
                        old, = read('Q', orig_page, pos)
                        new, = read('Q', new_page, pos)
                        auth = old >> 63
                        assert new >> 63 == auth
                        old_next = (old >> 52) & 0x7ff
                        assert ((new >> 51) & 0x7ff) == old_next
                        old_target = value_add + (old & ((1 << 34) - 1)) + slide
                        if not auth:
                            old_target |= ((old >> 34) & 0xff) << 56
                        binding = (new >> 62) & 1
                        if binding:
                            ordinal = new & 0xffffff
                            assert ordinal < bind_count
                            new_target, = read('Q', s, blob + bind_off + ordinal * 8)
                            if not auth:
                                addend = (new >> 32) & 0x7ffff
                                if addend & (1 << 18):
                                    addend -= 1 << 19
                                new_target = (new_target + addend) & ((1 << 64) - 1)
                            else:
                                assert (new >> 24) & 0xff == 0
                        elif auth:
                            new_target = image + (new & 0xffffffff)
                        else:
                            new_target = (image + (new & ((1 << 43) - 1))) | (((new >> 43) & 0xff) << 56)
                        assert old_target == new_target, (index, hex(va), page, pos, hex(old_target), hex(new_target))
                        if auth:
                            assert ((old >> 34) & 0xffff) == ((new >> 32) & 0xffff)
                            assert ((old >> 50) & 1) == ((new >> 48) & 1)
                            assert ((old >> 51) & 1) * 2 == ((new >> 49) & 3)
                        kind = ('auth' if auth else 'plain') + ('_bind' if binding else '_rebase')
                        rc[kind] += 1
                        if len(result['examples']) < 12 and (binding or not auth):
                            result['examples'].append({'location': hex(va + page * ps + pos),
                                'v5_word': hex(old), 'format12_word': hex(new),
                                'target': hex(old_target), 'kind': kind})
                        struct.pack_into('<Q', reconstructed, pos, new)
                        if old_next == 0:
                            break
                        pos += old_next * 8
                    assert reconstructed == new_page, ('non-chain bytes differ', index, hex(va), page)
                    rc['pages'] += 1
                result['regions'].append({'address': hex(va), 'size': size,
                    'counts': dict(rc), 'all_page_starts_match': True,
                    'non_chain_bytes_match': True})
                result['counts'].update(rc)
                print(json.dumps({'sidecar': index, 'region': hex(va),
                                  'counts': dict(rc)}), flush=True)
            result['counts'] = dict(result['counts'])
            return result


started = time.monotonic()
results = [audit(0), audit(1)]
output = {'method': 'Offline semantic file comparison derived from exact IDA consumer disassembly; bulk run outside IDA because its trace profiler timed out',
          'not_runtime_acceptance': True, 'results': results,
          'elapsed_seconds': time.monotonic() - started}
OUT.write_text(json.dumps(output, indent=2) + '\n')
print(json.dumps({'results': [{'sidecar': r['sidecar'], 'counts': r['counts']} for r in results],
                  'elapsed_seconds': output['elapsed_seconds'], 'output': str(OUT)}, indent=2))

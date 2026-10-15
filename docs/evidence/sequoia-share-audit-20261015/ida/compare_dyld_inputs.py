"""IDA-hosted inventory/diff of available dyld inputs; never patches bytes.

Names containing original/pristine are provenance claims, not trusted facts.
Compare all file-backed segment payloads other than __LINKEDIT, recording
header/signature differences separately from code/data changes.
"""
from pathlib import Path
import hashlib
import json
import struct
import ida_nalt
import ida_funcs
import idc

REPO = Path(__file__).resolve().parents[4]
WORK = REPO / 'analysis/share-review-20261015'
assert Path(ida_nalt.get_input_file_path()) == WORK / 'share-dyld-arm64e'


def parse(path):
    data = path.read_bytes()
    assert struct.unpack_from('<I', data)[0] == 0xfeedfacf
    cpu_type, cpu_subtype, filetype = struct.unpack_from('<III', data, 4)
    flags, = struct.unpack_from('<I', data, 24)
    ncmds, sizeofcmds = struct.unpack_from('<II', data, 16)
    pos = 32
    segments = {}
    uuid = None
    signature = None
    build_version = None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, pos)
        if cmd == 0x19:
            name, va, vmsize, off, fsize = struct.unpack_from('<16sQQQQ', data, pos + 8)
            segments[name.rstrip(b'\0').decode()] = dict(va=va, vmsize=vmsize, fileoff=off, filesize=fsize)
        elif cmd == 0x1b:
            uuid = data[pos + 8:pos + 24].hex()
        elif cmd == 0x1d:
            signature = struct.unpack_from('<II', data, pos + 8)
        elif cmd == 0x32:
            build_version = struct.unpack_from('<III', data, pos + 8)
        assert size >= 8
        pos += size
    assert pos == sizeofcmds + 32
    return {'path': str(path.relative_to(REPO)), 'sha256': hashlib.sha256(data).hexdigest(),
            'size': len(data), 'uuid': uuid, 'sizeofcmds': sizeofcmds,
            'cpu_type': hex(cpu_type), 'cpu_subtype': hex(cpu_subtype),
            'filetype': filetype, 'flags': hex(flags), 'build_version': build_version,
            'segments': segments, 'signature': signature}, data


def runs(a, b, step):
    start = None
    for off in range(0, min(len(a), len(b)), step):
        diff = a[off:off + step] != b[off:off + step]
        if diff and start is None:
            start = off
        if start is not None and (not diff or off + step >= min(len(a), len(b))):
            end = off if not diff else min(off + step, len(a), len(b))
            yield start, end
            start = None


inputs = [WORK / 'share-dyld-arm64e',
          WORK / 'dyld-packaged-earlier-arm64e',
          WORK / 'dyld-packaged-reserve-arm64e',
          REPO / 'analysis/dyld_15.6.1_arm64e_thin']
parsed = [parse(p) for p in inputs]
current_meta, current = parsed[0]
comparisons = []
for meta, old in parsed[1:]:
    changes = []
    for name, seg in current_meta['segments'].items():
        if name == '__LINKEDIT' or name not in meta['segments']:
            continue
        prev = meta['segments'][name]
        assert seg['va'] == prev['va']
        # Exclude only actual header/load commands. Padding in the first page
        # can contain injected code (the old research used a cave at 0x970).
        skip = max(current_meta['sizeofcmds'], meta['sizeofcmds']) + 32 if name == '__TEXT' else 0
        a = old[prev['fileoff'] + skip:prev['fileoff'] + prev['filesize']]
        b = current[seg['fileoff'] + skip:seg['fileoff'] + seg['filesize']]
        for start, end in runs(a, b, 4):
            va = seg['va'] + skip + start
            f = ida_funcs.get_func(va)
            changes.append({'segment': name, 'start': hex(va), 'end': hex(seg['va'] + skip + end),
                            'bytes': end - start, 'current_function': idc.get_func_name(va) if f else None,
                            'old_hex': a[start:end].hex(), 'current_hex': b[start:end].hex()})
        if len(a) != len(b):
            changes.append({'segment': name, 'size_difference': [len(a), len(b)]})
    comparisons.append({'from': meta['path'], 'to': current_meta['path'], 'changes': changes})
report = {'inputs': [m for m, _ in parsed], 'comparisons': comparisons,
          'limits': 'No independent Apple pristine identity or exact final frozen b8fdbc1b file was established. __LINKEDIT and header metadata are not behavior-diffed.'}
output = Path(__file__).with_name('dyld-input-differences.json')
output.write_text(json.dumps(report, indent=2) + '\n')
for c in comparisons:
    print(c['from'])
    for x in c['changes']:
        print({k: v for k, v in x.items() if k not in ['old_hex', 'current_hex']})

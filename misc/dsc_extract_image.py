#!/usr/bin/env python3
"""Extract a single dylib image from a dyld_shared_cache (dyld4 multi-file).

Runs on-device (procursus python3) or anywhere with plain file access.
Rebuilds a compact standalone Mach-O:
  * non-LINKEDIT segments are copied at (fileoff - __TEXT.fileoff)
  * every linkedit_data / dyld_info blob (function starts, exports trie,
    chained fixups…) is copied into a rebuilt __LINKEDIT at EOF and its
    dataoff field is patched in the emitted load commands
  * __LINKEDIT segment fields are rewritten to cover that region
  * section entries inside LC_SEGMENT_64 keep their addr fields (unslid
    vmaddrs) but get their offset fields rebased to the new layout
usage: dsc_extract_image.py <cache_base> <path_suffix> <out_file>
"""
import struct, sys, os

SEGMENT_64 = 0x19
FUNC_STARTS = 0x26
# all linkedit_data-style commands: (cmd, dataoff@+8, datasize@+12)
LE_CMDS = {0x22,               # LC_DYLIB_CODE_SIGN_DRS
           0x26,               # LC_FUNCTION_STARTS
           0x29,               # LC_DATA_IN_CODE
           0x2B,               # LC_CODE_SIGNATURE
           0x2D,               # LC_SEGMENT_SPLIT_INFO
           0x2E,               # LC_LINKER_OPTIMIZATION_HINT
           0x33,               # LC_DYLD_EXPORTS_TRIE
           0x34}               # LC_DYLD_CHAINED_FIXUPS
# NOTE: 0x32 is LC_BUILD_VERSION (platform/minos — NOT linkedit_data);
# 0x1D/0x1E are LC_DYLD_INFO(_ONLY) handled separately.

CACHE_FILES = []

def rd(f, off, size):
    f.seek(off); return f.read(size)

def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u64(b, o): return struct.unpack_from('<Q', b, o)[0]

def load_cache_file(path):
    f = open(path, 'rb')
    hdr = rd(f, 0, 0x400)
    magic = hdr[:16].rstrip(b' \x00').decode()
    assert magic.startswith('dyld_v'), f'{path}: {magic!r}'
    mo, mc = u32(hdr, 0x10), u32(hdr, 0x14)
    mappings = []
    for i in range(mc):
        m = rd(f, mo + i*32, 32)
        mappings.append((u64(m,0), u64(m,8), u64(m,16)))
    print(f'{os.path.basename(path)}: {magic}, {mc} mappings')
    return {'path': path, 'f': f, 'mappings': mappings, 'hdr': hdr}

def vm2foff(vmaddr):
    for cf in CACHE_FILES:
        for addr, size, foff in cf['mappings']:
            if addr <= vmaddr < addr + size:
                return cf['f'], foff + (vmaddr - addr)
    return None, None

def main():
    base, suffix, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    CACHE_FILES.append(load_cache_file(base))
    for i in range(1, 16):
        p = f'{base}.{i:02d}'
        if os.path.exists(p): CACHE_FILES.append(load_cache_file(p))

    main_f = CACHE_FILES[0]['f']
    main_hdr = CACHE_FILES[0]['hdr']
    it_off, it_cnt = u64(main_hdr, 0x88), u64(main_hdr, 0x90)
    target = None
    for i in range(it_cnt):
        e = rd(main_f, it_off + i*32, 32)
        load_addr, poff = u64(e,16), u32(e,28)
        path = rd(main_f, poff, 512).split(b'\x00')[0].decode(errors='replace')
        if suffix in path:
            target = (load_addr, path); break
    assert target, f'{suffix} not in cache'
    load_addr, path = target
    print(f'image: {path} @0x{load_addr:x}')

    mh_f, mh_off = vm2foff(load_addr)
    mh0 = bytearray(rd(mh_f, mh_off, 0x400))
    assert u32(mh0, 0) == 0xfeedfacf
    ncmds, sizeofcmds = u32(mh0, 16), u32(mh0, 20)
    hdr = bytearray(mh0 + rd(mh_f, mh_off + 0x400, sizeofcmds + 32 - 0x400))

    # pass 1 — inventory
    segs, le_blobs, dyld_info, symtab = [], [], None, None
    dysymtab_lcs = []
    lc_list = []
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = u32(hdr, off), u32(hdr, off+4)
        lc_list.append((off, cmd, cmdsize))
        if cmd == SEGMENT_64:
            name = bytes(hdr[off+8:off+24]).rstrip(b'\x00').decode()
            segs.append({'lc': off, 'name': name,
                         'vmaddr': u64(hdr,off+24), 'vmsize': u64(hdr,off+32),
                         'fileoff': u64(hdr,off+40), 'filesize': u64(hdr,off+48),
                         'nsects': u32(hdr,off+64)})
        elif cmd in (0x1D, 0x1E):
            dyld_info = {'lc': off, 'cmd': cmd,
                         'export_off': u32(hdr,off+32), 'export_sz': u32(hdr,off+36)}
        elif cmd in LE_CMDS:
            le_blobs.append({'lc': off, 'cmd': cmd,
                             'doff': u32(hdr,off+8), 'dsz': u32(hdr,off+12)})
        elif cmd == 0x2:  # LC_SYMTAB
            symtab = {'lc': off, 'symoff': u32(hdr,off+8), 'nsyms': u32(hdr,off+12),
                      'stroff': u32(hdr,off+16), 'strsize': u32(hdr,off+20)}
        elif cmd == 0xB:  # LC_DYSYMTAB — indices into the zeroed symtab
            dysymtab_lcs.append(off)
        off += cmdsize

    text = next(s for s in segs if s['name'] == '__TEXT')
    base_foff = text['fileoff']
    print('segments (cache-absolute fileoff):')
    for s in segs:
        print(f"  {s['name']:14s} vm=0x{s['vmaddr']:x}+0x{s['vmsize']:x} f=0x{s['fileoff']:x}+0x{s['filesize']:x}")

    # layout pass 2 — compact: seg -> fileoff-base_foff; blobs appended at EOF
    out_map = {}   # seg name -> new fileoff
    cursor = 0
    for s in segs:
        if s['name'] == '__LINKEDIT':
            continue
        new_off = s['fileoff'] - base_foff
        out_map[s['name']] = new_off
        cursor = max(cursor, new_off + s['filesize'])
    cursor = (cursor + 0xFFF) & ~0xFFF
    le_new = cursor

    # the image's linkedit data lives in the subcache file that owns the
    # image's __LINKEDIT vmaddr; linkedit_data dataoff values are plain file
    # offsets inside THAT file
    le_seg = next((s for s in segs if s['name'] == '__LINKEDIT'), None)
    le_file = None
    if le_seg:
        le_file, _ = vm2foff(le_seg['vmaddr'])
        print(f"__LINKEDIT owner: {le_file and le_file.name}")

    blob_writes = []   # (lc_off_in_hdr, new_dataoff, data)
    for b in le_blobs:
        if b['dsz'] == 0: continue
        src_f = le_file or CACHE_FILES[0]['f']
        src_off = b['doff']
        data = rd(src_f, src_off, b['dsz'])
        if b['cmd'] in (0x1D, 0x1E):
            pass
        cursor = (cursor + 15) & ~15
        blob_writes.append((b['lc'], b['cmd'], cursor, data))
        cursor += len(data)

    if dyld_info and dyld_info['export_sz']:
        # dyld_info export_off/dyld_info fields are file offsets too; the blob
        # loop above doesn't cover the 5-pair LC_DYLD_INFO — handle exports only
        eo, es = dyld_info['export_off'], dyld_info['export_sz']
        data = rd(le_file or CACHE_FILES[0]['f'], eo, es)
        cursor = (cursor + 15) & ~15
        blob_writes.append(('dyld_info_export', dyld_info['lc'], cursor, data))
        cursor += len(data)

    if symtab and symtab['nsyms']:
        # strtab in cache images is the cache-wide shared string pool (hundreds
        # of MB) — don't ship it; zero the LC_SYMTAB counts so tools skip it
        blob_writes.append(('symtab_zero', symtab['lc'], 0, b''))

    le_size = cursor - le_new
    print(f'new __LINKEDIT @0x{le_new:x} size 0x{le_size:x} ({len(blob_writes)} blobs)')

    # pass 3 — patch header
    for s in segs:
        lc = s['lc']
        if s['name'] == '__LINKEDIT':
            struct.pack_into('<Q', hdr, lc+40, le_new)        # fileoff
            struct.pack_into('<Q', hdr, lc+48, le_size)       # filesize
            struct.pack_into('<Q', hdr, lc+32, le_size)       # vmsize
        else:
            struct.pack_into('<Q', hdr, lc+40, out_map[s['name']])
        # rebase section file-offsets (offset field @ +72 of each section,
        # section struct is 80 bytes starting at lc+72)
        nsects = s['nsects']
        sec_off = lc + 72
        for i in range(nsects):
            s_addr, s_size = u64(hdr, sec_off+32), u64(hdr, sec_off+40)
            s_offset_pos = sec_off + 48
            old_off = u32(hdr, s_offset_pos)
            if old_off == 0 or s['name'] == '__LINKEDIT':
                continue
            new_sec_off = old_off - base_foff
            struct.pack_into('<I', hdr, s_offset_pos, new_sec_off)
            sec_off += 80

    for item in blob_writes:
        if item[0] == 'dyld_info_export':
            _, lc, new_off, _ = item
            struct.pack_into('<I', hdr, lc+32, new_off)   # export_off field
        elif item[0] == 'symtab_zero':
            _, lc, _, _ = item
            struct.pack_into('<IIII', hdr, lc+8, 0, 0, 0, 0)
            for dlc in dysymtab_lcs:
                for foff_field in range(8, 80, 4):
                    struct.pack_into('<I', hdr, dlc+foff_field, 0)
        else:
            lc, cmd, new_off, _ = item
            struct.pack_into('<I', hdr, lc+8, new_off)    # dataoff field

    # pass 4 — emit (segments first: __TEXT contains the original header
    # bytes, so the patched header must be written LAST to win)
    with open(out_path, 'wb') as out:
        for s in segs:
            if s['name'] == '__LINKEDIT' or s['filesize'] == 0:
                continue
            src_f, src_off = vm2foff(s['vmaddr'])
            assert src_f, f"vmaddr 0x{s['vmaddr']:x} unmapped"
            data = rd(src_f, src_off, s['filesize'])
            out.seek(out_map[s['name']]); out.write(data)
        for item in blob_writes:
            new_off, data = item[2], item[3]
            out.seek(new_off); out.write(data)
        out.seek(0); out.write(hdr[:32 + sizeofcmds])
    print(f'done -> {out_path} ({os.path.getsize(out_path):#x} bytes)')

if __name__ == '__main__':
    main()

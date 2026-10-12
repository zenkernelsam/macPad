#!/usr/bin/env python3
"""Locate assert-bearing hidden functions in a dsc-extracted dylib.

Every __assert_rtn call embeds an ADRP+ADD pair materializing the function's
name cstring.  For a hidden C++ method like MetalContext::CopyBufferToTexture,
the containing function of that site IS the method.

usage: dsc_find_hidden_syms.py <dylib>
"""
import struct, sys

f = open(sys.argv[1], 'rb')
D = f.read()
def u32(b,o): return struct.unpack_from('<I',b,o)[0]
def u64(b,o): return struct.unpack_from('<Q',b,o)[0]

# --- parse header -----------------------------------------------------------
assert u32(D,0) == 0xfeedfacf
ncmds = u32(D,16); off = 32
segs = {}; func_starts = None; text = None
for _ in range(ncmds):
    cmd, csz = u32(D,off), u32(D,off+4)
    if cmd == 0x19:
        name = D[off+8:off+24].rstrip(b'\x00').decode()
        segs[name] = dict(vm=u64(D,off+24), foff=u64(D,off+40),
                          fsz=u64(D,off+48))
        if name == '__TEXT': text = segs[name]
    elif cmd == 0x26:
        func_starts = (u32(D,off+8), u32(D,off+12))
    off += csz
assert text and func_starts

def vm2fo(v): return text['foff'] + (v - text['vm'])
def fo2vm(v):
    for n,s in segs.items():
        if n!='__LINKEDIT' and s['foff'] <= v < s['foff']+s['fsz']:
            return s['vm'] + (v - s['foff'])
    return 0

# --- function starts (uleb128 deltas) ---------------------------------------
fofs, fsz = func_starts
blob = D[fofs:fofs+fsz]
addrs = []; v = text['vm']; i = 0
while i < len(blob):
    shift = 0; delta = 0
    while i < len(blob):
        b = blob[i]; i += 1
        delta |= (b & 0x7f) << shift; shift += 7
        if not (b & 0x80): break
    v += delta
    addrs.append(v)
    if v > text['vm'] + text['fsz']: break
import bisect
def func_of(addr):
    i = bisect.bisect_right(addrs, addr) - 1
    return addrs[i] if i >= 0 else 0

# --- scan __TEXT for ADRP+ADD pairs ------------------------------------------
text_fo, text_sz = text['foff'], text['fsz']
code = memoryview(D)[text_fo:text_fo+text_sz]
targets = {}   # str vmaddr -> list of site addrs
for j in range(0, text_sz-8, 4):
    w = u32(code, j)
    if (w & 0x9f000000) != 0x90000000:      # ADRP
        continue
    pc = text['vm'] + j
    imm = ((w >> 3) & 0x1ffffc) | ((w >> 29) & 3)
    if imm & (1<<20): imm -= (1<<21)
    page = (pc & ~0xFFF) + (imm << 12)
    rd1 = w & 31
    w2 = u32(code, j+4)
    if (w2 & 0xff800000) != 0x91000000:     # ADD imm
        continue
    if ((w2 >> 5) & 31) != rd1:             # same base reg
        continue
    imm12 = (w2 >> 10) & 0xfff
    sh = (w2 >> 22) & 3
    t = page + (imm12 << (12 if sh==1 else 0))
    targets.setdefault(t, []).append(pc)

def cstr_at(vm):
    fo = None
    for n,s in segs.items():
        if n!='__LINKEDIT' and s['vm'] <= vm < s['vm']+s['fsz']:
            fo = s['foff'] + (vm - s['vm']); break
    if fo is None: return None
    e = D.index(b'\x00', fo)
    s = D[fo:e]
    try: return s.decode()
    except: return None

# --- report: interesting name strings ---------------------------------------
INTEREST = ['CopyBufferToTexture','StartComposite','EndCurrentComposite',
            'EndUpdate','PrepareForUse','Unbalanced','render_update',
            'BeginUpdate','StartUpdate','CommitUpdate','Flush','Present',
            'Drawable','MetalContext']
seen = {}
for t, sites in sorted(targets.items()):
    s = cstr_at(t)
    if not s: continue
    for key in INTEREST:
        if key in s:
            for site in sites:
                fn = func_of(site)
                seen.setdefault((s, fn), []).append(site)
            break
for (s, fn), sites in sorted(seen.items(), key=lambda x: x[0][1]):
    print(f'func 0x{fn:012x} (img+0x{fn-text["vm"]:x})  "{s}"  sites={len(sites)}')
    for st in sites[:3]:
        print(f'    site 0x{st:x} (img+0x{st-text["vm"]:x})')
print(f'\ntotal functions={len(addrs)}')

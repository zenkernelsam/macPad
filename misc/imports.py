#!/usr/bin/env python3
# imports.py — list (undefined_symbol -> library) imports of a Mach-O / fat arm64 slice.
import struct, sys

LC_SYMTAB=0x2; LC_DYSYMTAB=0xb; LC_LOAD_DYLIB=0xc; LC_LOAD_WEAK_DYLIB=0x18|0x80000000
LC_REEXPORT_DYLIB=0x1f|0x80000000; LC_LOAD_UPWARD_DYLIB=0x23|0x80000000
LC_LAZY_LOAD_DYLIB=0x20; LC_ID_DYLIB=0xd

def parse(d):
    e="<"
    n=struct.unpack(e+"I",d[16:20])[0]; off=32
    symtab=None; dysym=None; strtab=None; libs={}
    cur_lib_ordinal=0; liblist=[]
    for i in range(n):
        cmd,sz=struct.unpack(e+"II",d[off:off+8])
        if cmd==LC_SYMTAB:
            symoff,nsyms,stroff,strsize=struct.unpack(e+"IIII",d[off+8:off+24])
            symtab=(symoff,nsyms); strtab=(stroff,strsize)
        elif cmd==LC_DYSYMTAB:
            v=struct.unpack(e+"18I",d[off+8:off+80])
            dysym=v  # ilocalsym=5 iextdefsym=7 nextdefsym=8 iundefsym=9 nundefsym=10, indirectsymoff=14 nindirect=15? (see below)
        elif cmd in (LC_LOAD_DYLIB,LC_LOAD_WEAK_DYLIB,LC_REEXPORT_DYLIB,LC_LOAD_UPWARD_DYLIB,LC_LAZY_LOAD_DYLIB):
            nameoff=struct.unpack(e+"I",d[off+8:off+12])[0]
            nm=d[off+nameoff:off+sz].split(b"\x00")[0].decode(errors="replace")
            liblist.append(nm)
        elif cmd==LC_ID_DYLIB:
            nameoff=struct.unpack(e+"I",d[off+8:off+12])[0]
            nm=d[off+nameoff:off+sz].split(b"\x00")[0].decode(errors="replace")
        off+=sz
    if not symtab or not dysym: return
    symoff,nsyms=symtab; stroff,strsize=strtab
    iundef=dysym[9]; nundef=dysym[10]
    res=[]
    for i in range(iundef,iundef+nundef):
        so=symoff+i*16
        n_strx,n_type,n_sect,n_desc,n_val=struct.unpack(e+"IBBHI",d[so:so+16])
        nm=d[stroff+n_strx:stroff+strsize].split(b"\x00")[0].decode(errors="replace")
        libord=(n_desc>>8)&0xff
        libname=liblist[libord-1] if 0<libord<=len(liblist) else f"ord{libord}"
        res.append((nm,libname))
    return res

def main(p):
    d=open(p,"rb").read()
    if d[:4]==b"\xca\xfe\xba\xbe":
        n=struct.unpack(">I",d[4:8])[0]
        for i in range(n):
            ct,st,so,sz,al=struct.unpack(">IIIII",d[8+i*20:28+i*20])
            if ct==0x100000c: d=d[so:so+sz]; break
    r=parse(d) or []
    print(f"== {p} : {len(r)} undefined symbols")
    for nm,lib in r:
        print(f"  {nm}   <- {lib}")

for p in sys.argv[1:]:
    main(p)

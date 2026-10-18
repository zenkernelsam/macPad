import collections, gzip, hashlib, io, json, os, pathlib, sys, tarfile, time
BASE = pathlib.Path('/Users/ciscohe/Desktop/macPad')
NEW = pathlib.Path('/Users/ciscohe/Library/Mobile Documents/com~apple~CloudDocs/macPad-15.6.1-分享包-20261013')
RAW = pathlib.Path('/Users/ciscohe/Library/Mobile Documents/com~apple~CloudDocs/15.6提取包')
OLD = BASE / 'analysis/macPad-15.6 2.1-分享包-20261013'
OUT = BASE / 'docs/evidence/sequoia-share-update-20261018'
TMP = pathlib.Path('/tmp/macpad-share-review-20261018')
TMP.mkdir(exist_ok=True)
def save(name, obj):
    (OUT / name).write_text(json.dumps(obj, ensure_ascii=False, indent=2)+'\n')
def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(8*1024*1024), b''): h.update(b)
    return h.hexdigest()
def inventory(p):
    with open(p/'inventory.jsonl') as f: return {r['path']:r for r in map(json.loads,f)}
class HashReader:
    def __init__(self,f): self.f=f; self.h=hashlib.sha256(); self.n=0
    def read(self,n=-1):
        b=self.f.read(n); self.h.update(b); self.n+=len(b); return b
    def readable(self): return True
def checks():
    rows=[]
    for line in (NEW/'CHECKSUMS.sha256').read_text().splitlines():
        expected, name=line.split(None,1); name=name.lstrip('*')
        got=sha(NEW/name); rows.append(dict(path=name,expected=expected,actual=got,matches=got==expected))
        print('checksum',name,got==expected,flush=True)
    save('checksums.json',rows)
    a,b=inventory(OLD),inventory(NEW)
    add=sorted(b.keys()-a.keys()); rem=sorted(a.keys()-b.keys())
    change={}
    for p in sorted(a.keys()&b.keys()):
        keys=[k for k in set(a[p])|set(b[p]) if a[p].get(k)!=b[p].get(k)]
        if keys: change[p]=keys
    important=[p for p in b if p.startswith(('rootfs/usr/lib/dyld','rootfs/System/Library/dyld/','rootfs/usr/local/share/macpad-pager15/')) or 'main15-exec-jitd-dock' in p]
    save('inventory-diff.json',dict(old_entries=len(a),new_entries=len(b),added=add,removed=rem,changed=change,loader_and_pager=[dict(path=p,old=a.get(p),new=b[p],unchanged=a.get(p)==b[p]) for p in sorted(important)]))
def curated():
    sys.path.insert(0,str(TMP/'deps'))
    import zstandard
    inv=inventory(NEW); count=0; types=collections.Counter(); mismatches=[]; selected=[]; seen=set()
    with open(NEW/'payload.tar.zst','rb') as f:
        stream=zstandard.ZstdDecompressor().stream_reader(f)
        with tarfile.open(fileobj=stream,mode='r|') as tf:
            for m in tf:
                p=m.name.removeprefix('./'); count+=1; seen.add(p)
                kind='file' if m.isfile() else 'directory' if m.isdir() else 'symlink' if m.issym() else 'hardlink' if m.islnk() else 'other'
                types[kind]+=1; r=inv.get(p)
                if not r: mismatches.append(dict(path=p,reason='missing inventory'))
                if m.isfile():
                    h=hashlib.sha256(); src=tf.extractfile(m)
                    select=(p.startswith('reference-source/') or p.startswith('host-overlay/var/jb/usr/macOS/bin/') or p.startswith('host-overlay/var/jb/usr/macOS/LaunchDaemons/') or p.startswith('rootfs/usr/local/share/macpad-pager15/')) and m.size<20*1024*1024
                    dest=None
                    if select and '..' not in pathlib.PurePosixPath(p).parts and not p.startswith('/'):
                        target=TMP/'curated'/p; target.parent.mkdir(parents=True,exist_ok=True); dest=open(target,'wb')
                    for chunk in iter(lambda:src.read(4*1024*1024),b''):
                        h.update(chunk)
                        if dest: dest.write(chunk)
                    if dest: dest.close(); selected.append(p)
                    if r and r.get('sha256')!=h.hexdigest(): mismatches.append(dict(path=p,reason='content hash'))
                if count%50000==0: print('curated members',count,flush=True)
        trailing=0; nonzero=False
        for b in iter(lambda:stream.read(4*1024*1024),b''): trailing+=len(b); nonzero|=bool(b.strip(b'\0'))
    save('curated-archive.json',dict(members=count,types=dict(types),mismatches=mismatches,inventory_paths_not_seen=sorted(inv.keys()-seen),selected_paths=selected,trailing_bytes_after_tar=trailing,trailing_nonzero=nonzero,stream_eof=True))
    print('curated complete',count,len(mismatches),flush=True)
def metadata():
    folder=next(RAW.glob('metadata-*')); ops=collections.Counter(); codes=collections.Counter(); types=collections.Counter(); paths=0; error_entries=0; lines=0; digest=hashlib.sha256(); summary=None
    with gzip.open(folder/'metadata.jsonl.gz','rb') as f:
        for line in f:
            digest.update(line); lines+=1; r=json.loads(line); types[r.get('type')]+=1
            if r.get('type')=='entry':
                paths+=1
                if r.get('errors'): error_entries+=1
                for e in r.get('errors',[]): ops[e.get('operation')]+=1; codes[str(e.get('errno'))]+=1
            if r.get('type')=='summary': summary=r
    save('raw-metadata.json',dict(lines=lines,types=dict(types),entry_count=paths,error_entries=error_entries,operations=dict(ops),errnos=dict(codes),uncompressed_sha256=digest.hexdigest(),compressed_sha256=sha(folder/'metadata.jsonl.gz'),summary=summary,supplier_result=json.loads((folder/'result.json').read_text()),gzip_eof=True))
    print('metadata complete',paths,error_entries,flush=True)
def raw():
    count=0; types=collections.Counter(); selected=[]; error=None; trailing=0; nonzero=False
    with open(RAW/'macPad-current.tar.gz.partial','rb') as f:
        reader=HashReader(f)
        try:
            with gzip.GzipFile(fileobj=reader) as gz:
                plain=HashReader(gz)
                with tarfile.open(fileobj=plain,mode='r|') as tf:
                    for m in tf:
                        count+=1; types['file' if m.isfile() else 'directory' if m.isdir() else 'link' if m.islnk() or m.issym() else 'other']+=1
                        p=m.name.removeprefix('./')
                        # Only project implementation sources. Never extract user databases, logs or documents.
                        select=('MacWSBootingGuide/' in p or '/sequoia-hook-source/' in p) and p.endswith(('.c','.m','.h','.x','.py','.sh','.cpp')) and m.isfile() and m.size<8*1024*1024
                        if select and '..' not in pathlib.PurePosixPath(p).parts and not p.startswith('/'):
                            target=TMP/'raw'/p; target.parent.mkdir(parents=True,exist_ok=True)
                            target.write_bytes(tf.extractfile(m).read()); selected.append(p)
                        if count%100000==0: print('raw members',count,flush=True)
                for b in iter(lambda:plain.read(4*1024*1024),b''): trailing+=len(b); nonzero|=bool(b.strip(b'\0'))
                plain_bytes=plain.n
        except Exception as e: error=repr(e); plain_bytes=plain.n if 'plain' in locals() else None
        for b in iter(lambda:reader.read(8*1024*1024),b''): pass
    save('raw-archive.json',dict(members=count,types=dict(types),compressed_bytes=reader.n,compressed_sha256=reader.h.hexdigest(),uncompressed_bytes=plain_bytes,error=error,gzip_eof=error is None,trailing_bytes_after_tar=trailing,trailing_nonzero=nonzero,selected_project_source_paths=selected,restore_tested=False,atomic_snapshot=False))
    print('raw complete',count,error,flush=True)
def extras():
    sys.path.insert(0,str(TMP/'deps'))
    import zstandard
    wanted={
      'host-overlay/var/jb/usr/local/libexec/main15-exec-jitd-dock-reopen-20261010',
      'rootfs/usr/local/lib/libmachook.dylib', 'rootfs/usr/local/lib/libmachook_arm64.dylib',
      'rootfs/private/var/db/macws/native15-installed-20261005.json',
      'rootfs/private/var/db/macws/metalfx-ane-policy-v1',
    }
    rows=[];inv=inventory(NEW)
    with open(NEW/'payload.tar.zst','rb') as f:
        with tarfile.open(fileobj=zstandard.ZstdDecompressor().stream_reader(f),mode='r|') as tf:
            for m in tf:
                p=m.name.removeprefix('./')
                if p not in wanted or not m.isfile():continue
                data=tf.extractfile(m).read();target=TMP/'curated'/p;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
                rows.append(dict(path=p,sha256=hashlib.sha256(data).hexdigest(),matches=hashlib.sha256(data).hexdigest()==inv[p].get('sha256')))
    save('extra-extraction.json',rows);print('extras complete',len(rows),flush=True)
def raw_compare():
    inv=inventory(NEW)
    targets={
      'var/mnt/rootfs/usr/lib/dyld':'rootfs/usr/lib/dyld',
      'var/mnt/rootfs/System/Library/dyld/dyld_shared_cache_arm64e':'rootfs/System/Library/dyld/dyld_shared_cache_arm64e',
      'var/mnt/rootfs/System/Library/dyld/dyld_shared_cache_arm64e.01':'rootfs/System/Library/dyld/dyld_shared_cache_arm64e.01',
      'var/mnt/rootfs/usr/local/lib/libmachook.dylib':'rootfs/usr/local/lib/libmachook.dylib',
      'var/mnt/rootfs/usr/local/lib/libmachook_arm64.dylib':'rootfs/usr/local/lib/libmachook_arm64.dylib',
      'var/jb/usr/local/libexec/main15-exec-jitd-dock-reopen-20261010':'host-overlay/var/jb/usr/local/libexec/main15-exec-jitd-dock-reopen-20261010',
    }
    for r in inv.values():
        p=r['path']
        if p.startswith('rootfs/usr/local/share/macpad-pager15/') and r['type']=='file':targets['var/mnt/'+p]=p
        if p.startswith('host-overlay/var/jb/usr/macOS/bin/') and (p.endswith(('.py','.sh')) or p.endswith('/macwshostd')) and r['type']=='file':targets[p.removeprefix('host-overlay/')]=p
    rows=[];hardlinks=[];remaining=set(targets)
    with tarfile.open(RAW/'macPad-current.tar.gz.partial',mode='r|gz') as tf:
        for m in tf:
            p=m.name.removeprefix('./')
            if p not in targets:continue
            key=targets[p];remaining.discard(p)
            if m.islnk():hardlinks.append(dict(path=key,target=m.linkname));continue
            if not m.isfile():continue
            h=hashlib.sha256();src=tf.extractfile(m);dest=None
            if m.size<8*1024*1024 and p.endswith(('.py','.sh')):
                target=TMP/'raw-selected'/key;target.parent.mkdir(parents=True,exist_ok=True);dest=open(target,'wb')
            for b in iter(lambda:src.read(4*1024*1024),b''):
                h.update(b)
                if dest:dest.write(b)
            if dest:dest.close()
            rows.append(dict(path=key,sha256=h.hexdigest(),curated_sha256=inv[key].get('sha256'),same_as_curated=h.hexdigest()==inv[key].get('sha256')))
            print('raw compare',key,rows[-1]['same_as_curated'],flush=True)
    save('raw-runtime-comparison.json',dict(rows=rows,hardlinks=hardlinks,missing=sorted(remaining),scope='Selected implementation/runtime only. No user documents or raw logs.'))
    print('raw comparison complete',len(rows),flush=True)
def raw_additions():
    wanted={
      'var/jb/usr/macOS/bin/macws_windowmanagement15.py',
      'var/mnt/rootfs/usr/lib/macws-sequoia15.6-original/preboot-layout-20261006/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e',
      'var/mnt/rootfs/usr/lib/macws-sequoia15.6-original/preboot-layout-20261006/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e.01',
    }
    rows=[]
    with tarfile.open(RAW/'macPad-current.tar.gz.partial',mode='r|gz') as tf:
        for m in tf:
            p=m.name.removeprefix('./')
            if not m.isfile() or p not in wanted:continue
            h=hashlib.sha256();src=tf.extractfile(m);chunks=[]
            for b in iter(lambda:src.read(4*1024*1024),b''):
                h.update(b)
                if p.endswith('.py'):chunks.append(b)
            if chunks:
                target=TMP/'raw-selected'/p;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b''.join(chunks))
            rows.append(dict(path=p,sha256=h.hexdigest(),size=m.size))
            print('raw additional',p,flush=True)
    save('raw-additional-files.json',rows)
if __name__=='__main__': globals()[sys.argv[1]]()

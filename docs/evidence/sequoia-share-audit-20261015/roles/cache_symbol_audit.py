# Read-only metadata audit; offsets validated against dyld1286.10 C headers.
# Run from repository root. Writes only local analysis and evidence files.
from pathlib import Path
import mmap,struct,json,contextlib
root=Path('analysis/share-review-20261015/extracted/rootfs/System/Library/dyld')
with contextlib.ExitStack() as stack:
 maps=[];data=[]
 for suffix in ['', '.01']:
  f=stack.enter_context((root/('dyld_shared_cache_arm64e'+suffix)).open('rb'));b=stack.enter_context(mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ));data.append(b)
  off,n=struct.unpack_from('<II',b,16)
  for j in range(n):
   va,size,fo,mp,ip=struct.unpack_from('<QQQII',b,off+32*j);maps.append((va,va+size,b,fo))
 def readva(addr,size):
  chunks=[]
  while size:
   found=next((x for x in maps if x[0]<=addr<x[1]),None)
   assert found,(hex(addr),size)
   va,end,b,fo=found;n=min(size,end-addr);chunks.append(b[fo+addr-va:fo+addr-va+n]);addr+=n;size-=n
  return b''.join(chunks)
 b=data[0];images,ni=struct.unpack_from('<II',b,448);found=[]
 for i in range(ni):
  va,mt,inode,pathoff,pad=struct.unpack_from('<QQQII',b,images+32*i);end=b.find(b'\0',pathoff);path=b[pathoff:end].decode()
  if path.endswith('/SkyLight.framework/Versions/A/SkyLight'):
   found.append((va,path))
 assert len(found)==1,found
 va,path=found[0];header=readva(va,32);magic,ct,cst,ft,ncmd,sc,flags,res=struct.unpack('<8I',header);assert magic==0xfeedfacf
 cmds=readva(va+32,sc);pos=0;segments=[];sym=None;uuid=None
 for _ in range(ncmd):
  cmd,size=struct.unpack_from('<II',cmds,pos)
  if cmd==0x19:
   name,sva,svs,fo,fs=struct.unpack_from('<16sQQQQ',cmds,pos+8);segments.append({'name':name.rstrip(b'\0').decode(),'va':sva,'size':svs,'fileoff':fo,'filesize':fs})
  if cmd==2:sym=struct.unpack_from('<IIII',cmds,pos+8)
  if cmd==0x1b:uuid=cmds[pos+8:pos+24].hex()
  pos+=size
 le=next(s for s in segments if s['name']=='__LINKEDIT');syoff,nsy,stroff,strsize=sym
 assert syoff>=le['fileoff'] and stroff>=le['fileoff']
 symbols=readva(le['va']+syoff-le['fileoff'],nsy*16);strings=readva(le['va']+stroff-le['fileoff'],strsize)
 hits=[];allnames=[]
 for i in range(nsy):
  ix,typ,sect,desc,value=struct.unpack_from('<IBBHQ',symbols,i*16)
  if ix>=strsize:continue
  end=strings.find(b'\0',ix);name=strings[ix:end].decode(errors='replace');allnames.append((value,name))
  if 'audit_token_check_tcc_access' in name or 'CGXSenderCanSynthesizeEvents' in name:hits.append({'name':name,'type':hex(typ),'address':hex(value),'first64':readva(value,64).hex()})
 out=Path('docs/evidence/sequoia-share-audit-20261015/roles');r={'source_main_uuid':b[0x58:0x68].hex(),'source_subcache_uuid':data[1][0x58:0x68].hex(),'image_path':path,'image_address':hex(va),'uuid':uuid,'symtab_entries':nsy,'strings_size':strsize,'matches':hits,'metadata_layout_source':'dyld1286.10 C header offsetof compiled: imagesOffset=448 imagesCount=452','provider_comparison':'ElleKit findSymbol accepts ordinary local N_SECT and does not require N_EXT; compare its exported decompilation','not_runtime_hook_installation':True}
 (out/'skylight-symbol-availability.json').write_text(json.dumps(r,indent=2)+'\n')
 print(json.dumps(r,indent=2))
 for h in hits:
  addr=int(h['address'],16);nextaddr=min(x[0] for x in allnames if x[0]>addr);body=readva(addr,nextaddr-addr)
  target=Path('analysis/share-review-20261015')/(h['name'].strip('_')+'-24G90.raw');target.write_bytes(body)
  h.update(raw_path=str(target),raw_size=len(body))
 (out/'skylight-symbol-availability.json').write_text(json.dumps(r,indent=2)+'\n')

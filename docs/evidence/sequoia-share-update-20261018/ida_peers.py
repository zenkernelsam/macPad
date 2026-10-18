import ida_auto,ida_bytes,idautils,idc,json,pathlib
ida_auto.auto_wait()
ea=next((a for a,n in idautils.Names() if 'identity_allowed' in n and 'peers' in n),idc.BADADDR)
assert ea!=idc.BADADDR
rows=[]
for i in range(12):
    target=ida_bytes.get_qword(ea+8*i)
    s=ida_bytes.get_strlit_contents(target,-1,idc.STRTYPE_C)
    rows.append({'index':i,'pointer_address':hex(ea+8*i),'target':hex(target),'path':s.decode() if s else None})
out=pathlib.Path('/Users/ciscohe/Desktop/macPad/docs/evidence/sequoia-share-update-20261018/ida/exec')
(out/'allowed-peers.json').write_text(json.dumps(rows,indent=2)+'\n')
idc.qexit(0)

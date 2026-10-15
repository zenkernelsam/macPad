import ida_auto,ida_nalt,ida_hexrays,ida_funcs,ida_bytes,ida_lines,idautils,idc,pathlib,json
assert pathlib.Path(ida_nalt.get_input_file_path()).name=='dyld_15.6.1_arm64e_thin'
assert '--batch-export' in idc.ARGV, 'Batch-only: pass --batch-export'
assert ida_nalt.retrieve_input_file_sha256().hex() == '12dc97d541939a8e05d58f265f62eaef93fbce63740d7eefaee434cea7acbac5'
out=pathlib.Path(__file__).resolve().parent/'reference-dyld';out.mkdir(parents=True,exist_ok=True)
ida_auto.auto_wait();ready=ida_hexrays.init_hexrays_plugin()
meta={'engine':'IDA Pro 9.3 batch + Hex-Rays','input':ida_nalt.get_input_file_path(),'sha256':ida_nalt.retrieve_input_file_sha256().hex(),'imagebase':hex(ida_nalt.get_imagebase()),'hexrays_ready':ready,'provenance':'Historical local comparison input only; Apple pristine not independently established'}
(out/'identity.json').write_text(json.dumps(meta,indent=2)+'\n')
res=[]
for ea in idautils.Functions():
 name=idc.get_func_name(ea)
 if name not in ['_rosetta_dyld_is_translated','__crossarch_trap']:continue
 lines=[]
 for p in idautils.FuncItems(ea):lines.append(hex(p)+' '+ida_bytes.get_bytes(p,idc.get_item_size(p)).hex()+' '+ida_lines.tag_remove(idc.generate_disasm_line(p,0) or ''))
 try:code=str(ida_hexrays.decompile(ea))
 except Exception as e:code=str(e)
 res.append({'address':hex(ea),'name':name,'code':code,'instructions':lines})
(out/'rosetta-query.json').write_text(json.dumps(res,indent=2)+'\n')
idc.qexit(0)

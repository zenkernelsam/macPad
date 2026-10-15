import ida_auto, ida_nalt, ida_hexrays, ida_funcs, ida_lines, ida_bytes, idautils, idc, json, pathlib
out=pathlib.Path(__file__).resolve().parent/'exec-jitd';out.mkdir(parents=True,exist_ok=True)
assert '--batch-export' in idc.ARGV, 'Batch-only exporter: pass --batch-export'
assert pathlib.Path(ida_nalt.get_input_file_path()).name == 'main15-exec-jitd-dock-reopen-20261010'
ida_auto.auto_wait()
meta={'engine':'IDA Pro 9.3 batch + Hex-Rays','input':ida_nalt.get_input_file_path(),'imagebase':hex(ida_nalt.get_imagebase()),'sha256':ida_nalt.retrieve_input_file_sha256().hex(),'hexrays_ready':ida_hexrays.init_hexrays_plugin()}
(out/'identity.json').write_text(json.dumps(meta,indent=2)+'\n')
functions=[]
for ea in idautils.Functions():
 name=idc.get_func_name(ea)
 if name not in ['_main','_watch','_stopped','_grant','_identity_allowed']:continue
 f=ida_funcs.get_func(ea);lines=[]
 for p in idautils.FuncItems(ea):
  lines.append(hex(p)+' '+ida_bytes.get_bytes(p,idc.get_item_size(p)).hex()+' '+ida_lines.tag_remove(idc.generate_disasm_line(p,0) or ''))
 try:code=str(ida_hexrays.decompile(ea))
 except Exception as e:code='DECOMPILE_ERROR '+str(e)
 functions.append({'address':hex(ea),'name':name,'end':hex(f.end_ea),'code':code})
 (out/(name.lstrip('_')+'-asm.txt')).write_text('\n'.join(lines)+'\n')
(out/'functions.json').write_text(json.dumps(functions,indent=2)+'\n')
(out/'strings.json').write_text(json.dumps([{'address':hex(s.ea),'text':str(s)} for s in idautils.Strings()],indent=2)+'\n')
print('EXPORTED',len(functions))
idc.qexit(0)

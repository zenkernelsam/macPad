import ida_auto,ida_nalt,ida_hexrays,ida_funcs,ida_bytes,ida_lines,idautils,idc,pathlib,json,hashlib
role=idc.ARGV[1]
out=pathlib.Path('/Users/ciscohe/Desktop/macPad/docs/evidence/sequoia-share-update-20261018/ida')/role
out.mkdir(parents=True,exist_ok=True)
ida_auto.auto_wait()
ready=ida_hexrays.init_hexrays_plugin()
identity={'engine':'IDA Pro 9.3 batch + Hex-Rays','sha256':ida_nalt.retrieve_input_file_sha256().hex(),'imagebase':hex(ida_nalt.get_imagebase()),'hexrays_ready':ready,'input_basename':pathlib.Path(ida_nalt.get_input_file_path()).name}
functions=[]
for ea in idautils.Functions():
    name=idc.get_func_name(ea);f=ida_funcs.get_func(ea)
    selected=(role=='exec' and name in ['_main','_watch','_grant','_stopped','_identity_allowed']) or (role=='ws-hook' and name in ['_loadImageCallback','_hooked_return_1','_macws_coexist_observe_service_set','_macws_coexist_wait_for_completion_slot','_IOConnectCallMethod_new'])
    if not selected:continue
    try:code=str(ida_hexrays.decompile(ea))
    except Exception as e:code='DECOMPILE_ERROR '+str(e)
    lines=[hex(p)+' '+ida_bytes.get_bytes(p,idc.get_item_size(p)).hex()+' '+ida_lines.tag_remove(idc.generate_disasm_line(p,0) or '') for p in idautils.FuncItems(ea)]
    functions.append({'address':hex(ea),'end':hex(f.end_ea),'name':name,'code':code,'instructions':lines,'function_bytes_sha256':hashlib.sha256(ida_bytes.get_bytes(ea,f.end_ea-ea)).hexdigest()})
(out/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
(out/'functions.json').write_text(json.dumps(functions,indent=2)+'\n')
print('EXPORTED',role,len(functions));idc.qexit(0)

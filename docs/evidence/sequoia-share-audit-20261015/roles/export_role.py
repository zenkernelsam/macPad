import ida_auto,ida_nalt,ida_hexrays,ida_funcs,ida_bytes,ida_lines,idautils,idc,pathlib,json
role=idc.ARGV[1]
allowed={'WS-launcher':'launchdchrootexec-sequoia-main-20261005','Dock-launcher':'Dock-launcher-arm64','dock-actions':'dock-actions-arm64e','libdockfix':'libdockfix-arm64e','WindowServer-hook':'libmachook_arm64.dylib','Substrate':'CydiaSubstrate'}
assert role in allowed and pathlib.Path(ida_nalt.get_input_file_path()).name==allowed[role]
assert '--batch-export' in idc.ARGV, 'Batch only: pass --batch-export'
out=pathlib.Path(__file__).resolve().parent/role;out.mkdir(parents=True,exist_ok=True)
ida_auto.auto_wait();ready=ida_hexrays.init_hexrays_plugin()
(out/'identity.json').write_text(json.dumps({'engine':'IDA Pro 9.3 batch/Hex-Rays','input':ida_nalt.get_input_file_path(),'sha256':ida_nalt.retrieve_input_file_sha256().hex(),'imagebase':hex(ida_nalt.get_imagebase()),'hexrays_ready':ready,'cpu_subtype_in_loaded_header':hex(ida_bytes.get_dword(ida_nalt.get_imagebase()+8))},indent=2)+'\n')
functions=[];names=[]
for ea in idautils.Functions():
 name=idc.get_func_name(ea);f=ida_funcs.get_func(ea);names.append({'address':hex(ea),'name':name,'size':f.end_ea-ea})
 if role in ['Dock-launcher','WS-launcher']:selected=name in ['_main','_macws_insert_dylib_for_arch']
 elif role=='dock-actions':selected=f.end_ea-ea>60 and not name.startswith(('j_','_objc_','___stub'))
 elif role=='WindowServer-hook':selected=name in ['_loadImageCallback','_hooked_return_1']
 elif role=='Substrate':selected=name in ['_MSFindSymbol','_MSHookFunction','_MSHookMessageEx'] or any(k in name for k in ['12MSFindSymbol','17findPrivateSymbol','15sharedCachePath','10findSymbol','11messageHook'])
 else:selected=name in ['_loadImageCallback','_hooked_return_1','_InitStuff','_MacWSPostLegacySystemPointerEvent','_MacWSPostDockSystemInput','_MacWSPostDockModalPointerEvent','_MacWSInstallAppInputBridge']
 if not selected:continue
 try:code=str(ida_hexrays.decompile(ea))
 except Exception as e:code='DECOMPILE_ERROR '+str(e)
 lines=[hex(p)+' '+ida_bytes.get_bytes(p,idc.get_item_size(p)).hex()+' '+ida_lines.tag_remove(idc.generate_disasm_line(p,0) or '') for p in idautils.FuncItems(ea)]
 functions.append({'address':hex(ea),'name':name,'code':code,'instructions':lines})
(out/'functions.json').write_text(json.dumps(functions,indent=2)+'\n');(out/'function-index.json').write_text(json.dumps(names,indent=2)+'\n')
(out/'strings.json').write_text(json.dumps([{'address':hex(s.ea),'text':str(s)} for s in idautils.Strings()],indent=2)+'\n')
print('EXPORTED',role,len(functions));idc.qexit(0)

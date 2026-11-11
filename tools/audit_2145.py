"""Read-only binary contract audit. Requires pefile and capstone; never loads a DLL.
Usage: python tools/audit_2145.py ReSkate.dll --upstream TAG_SOURCE --game Skate.exe
"""
from pathlib import Path
import argparse,hashlib,json,re
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_IMM,X86_OP_MEM,X86_REG_RIP
p=argparse.ArgumentParser();p.add_argument('dll',type=Path);p.add_argument('--upstream',type=Path,required=True);p.add_argument('--game',type=Path,required=True);p.add_argument('--layout-report',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
data=a.dll.read_bytes();assert hashlib.sha256(data).hexdigest()=='0820fa71c1cb716f4222eb9027130933aa9e3db4ce20b46ef555760be997dbde'
pe=pefile.PE(data=data,fast_load=True);pe.parse_data_directories(directories=[0,3]);cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True
text=(root/'src/host_2145.h').read_text();values={k:int(v,16) for k,v in re.findall(r'uintptr_t (\w+)=(0x[0-9a-f]+)',text)}
rows=[]
assert values['image_size']==pe.OPTIONAL_HEADER.SizeOfImage
for key in ('state','gui_context','alloc','free','allocator_data'):
    section=pe.get_section_by_rva(values[key])
    assert section and section.Characteristics&0x80000000,key
for name,raw in re.findall(r'(\w+)_bytes\[\]\{([^}]+)',text):
    expected=bytes(int(x,0) for x in raw.split(','));assert pe.get_data(values[name],len(expected))==expected,name
    rows.append({'name':name,'rva':hex(values[name]),'prefix':expected.hex()})
exports={x.name.decode():x.address for x in pe.DIRECTORY_ENTRY_EXPORT.symbols if x.name}
assert exports['DingoSDKOverlayGetStatus']==values['status']
def code(name,count):return list(cs.disasm(pe.get_data(values[name],count),values[name]))
def calls(instructions):return [i.operands[0].imm for i in instructions if i.mnemonic=='call' and i.operands[0].type==X86_OP_IMM]
statuscalls=calls(code('status',180));assert statuscalls[-2:]==[values['mutex_lock'],values['mutex_unlock']],statuscalls
getter=statuscalls[0]
refs=lambda ins:[i.address+i.size+o.mem.disp for i in ins for o in i.operands if o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP]
assert values['state'] in refs(list(cs.disasm(pe.get_data(getter,100),getter)))
assert values['gui_context'] in refs(code('context_getter',7))
# ImGui allocator globals are only used by the synthetic embedded-menu path,
# but still require independent binary references rather than inherited RVAs.
section=next(s for s in pe.sections if s.Name.rstrip(b'\0')==b'.text')
allocator_refs={k:[] for k in ('alloc','free','allocator_data')}
for match in re.finditer(rb'[\x48\x4c][\x8b\x89][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....',section.get_data(),re.DOTALL):
    rva=section.VirtualAddress+match.start()
    target=rva+7+int.from_bytes(match.group()[3:7],'little',signed=True)
    for key in allocator_refs:
        if target==values[key]:allocator_refs[key].append(hex(rva))
for match in re.finditer(rb'\xff[\x15\x25]....',section.get_data(),re.DOTALL):
    rva=section.VirtualAddress+match.start()
    target=rva+6+int.from_bytes(match.group()[2:6],'little',signed=True)
    for key in allocator_refs:
        if target==values[key]:allocator_refs[key].append(hex(rva))
assert all(allocator_refs.values()),allocator_refs
render_entry=next(e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress==values['render'])
render=code('render',render_entry.EndAddress-render_entry.BeginAddress)
assert values['mutex_lock'] in calls(render) and values['mutex_unlock'] in calls(render)
assert getter in calls(render), 'render must acquire the same overlay singleton'
# All game contracts remain bound to the same executable, independently of ReSkate.
game=a.game.read_bytes();assert hashlib.sha256(game).hexdigest()=='fbce74d5e28ef525dbba2cb4adbebc13405bdbd88f31bc940bca45e4ae88b8f9'
gp=pefile.PE(data=game,fast_load=True);gamechecks=[]
for file in ('native_contracts.h','native_probe.cpp'):
    source=(root/'src'/file).read_text()
    for rva,raw in re.findall(r'\{(0x[0-9a-f]+),\s*\{([^}]+)\}',source):
        expected=bytes(int(x.strip(),0) for x in raw.split(',') if x.strip());addr=int(rva,16)
        assert gp.get_data(addr,len(expected))==expected,(file,rva)
        gamechecks.append({'file':file,'rva':rva,'bytes':len(expected)})
# Inventory every large game address in the active placement/camera reader against the tag.
official='\n'.join(p.read_text(errors='replace').lower() for p in (a.upstream/'Engine/Game/Build').rglob('*.h'))
source=(root/'src/native_attachment.cpp').read_text()
realm=re.search(r'realm_prefix\[\]\{([^}]+)',source)
assert realm
realm_bytes=bytes(int(x.strip(),0) for x in realm[1].split(',') if x.strip())
assert gp.get_data(0x18b6e40,len(realm_bytes))==realm_bytes
gamechecks.append({'file':'native_attachment.cpp','rva':'0x18b6e40','bytes':len(realm_bytes)})
addresses=sorted(set(int(v,16) for v in re.findall(r'0x[0-9a-fA-F]{6,}',source) if 0x400000<=int(v,16)<0x10000000))
for addr in addresses:assert re.search(r'0x0*'+format(addr,'x')+r'\b',official),hex(addr)
report={'passed':True,'runtime_sha256':hashlib.sha256(data).hexdigest(),'host_contracts':rows,'status_calls':[hex(v) for v in statuscalls],'state':hex(values['state']),'game_contracts':gamechecks,'game_addresses_checked_against_tag':[hex(x) for x in addresses]}
# Verify every active vendored header against the exact tagged source, including
# transitive game structure declarations used by placement and camera readers.
headers=[]
for header in (root/'vendor/reskate2144').rglob('*'):
    if header.is_file():
        relative=header.relative_to(root/'vendor/reskate2144')
        assert header.read_bytes()==(a.upstream/relative).read_bytes(),str(relative)
        headers.append(relative.as_posix())
report['tagged_headers_verified']=sorted(headers)
report['host_fields']={k:values[k] for k in ('visible','ready','failed','swapchain','queue','context','model','menu')}
report['tag_commit']='83200814db92135c90e35166397b4d79fb70d7e1'
report['allocator_global_references']=allocator_refs
report['render_function_size']=render_entry.EndAddress-render_entry.BeginAddress
layout_text=a.layout_report.read_text()
layout={key:int(value) for key,value in re.findall(r'(\w+)=(\d+)',layout_text)}
for key in report['host_fields']:assert layout[key]==values[key],key
assert layout['render_mutex']==0 and layout['Model']==26248 and layout['editor']==24192 and layout['debug']==128
report['compiled_layout']=layout
print(json.dumps(report,indent=2))

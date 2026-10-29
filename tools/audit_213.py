"""Read-only binary contract audit. Requires pefile and capstone; never loads a DLL.
Usage: python tools/audit_213.py ReSkate.dll --upstream TAG_SOURCE --game Skate.exe
"""
from pathlib import Path
import argparse,hashlib,json,re
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_IMM,X86_OP_MEM,X86_REG_RIP
p=argparse.ArgumentParser();p.add_argument('dll',type=Path);p.add_argument('--upstream',type=Path,required=True);p.add_argument('--game',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
data=a.dll.read_bytes();assert hashlib.sha256(data).hexdigest()=='139e932ad6328b898b2ff8c2aac3e0cd5ced6545c2f041aa8534239f97e59f9b'
pe=pefile.PE(data=data);cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True
text=(root/'src/host_213.h').read_text();values={k:int(v,16) for k,v in re.findall(r'uintptr_t (\w+)=(0x[0-9a-f]+)',text)}
rows=[]
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
render=code('render',10000)
assert values['mutex_lock'] in calls(render) and values['mutex_unlock'] in calls(render)
assert getter in calls(render), 'render must acquire the same overlay singleton'
# All game contracts remain bound to the same executable, independently of ReSkate.
game=a.game.read_bytes();assert hashlib.sha256(game).hexdigest()=='fbce74d5e28ef525dbba2cb4adbebc13405bdbd88f31bc940bca45e4ae88b8f9'
gp=pefile.PE(data=game);gamechecks=[]
for file in ('native_contracts.h','native_probe.cpp'):
    source=(root/'src'/file).read_text()
    for rva,raw in re.findall(r'\{(0x[0-9a-f]+),\s*\{([^}]+)\}',source):
        expected=bytes(int(x.strip(),0) for x in raw.split(',') if x.strip());addr=int(rva,16)
        assert gp.get_data(addr,len(expected))==expected,(file,rva)
        gamechecks.append({'file':file,'rva':rva,'bytes':len(expected)})
# Inventory every large game address in the active placement/camera reader against the tag.
official='\n'.join(p.read_text(errors='replace').lower() for p in (a.upstream/'Engine/Game/Build').rglob('*.h'))
source=(root/'src/native_attachment.cpp').read_text()
addresses=sorted(set(int(v,16) for v in re.findall(r'0x[0-9a-fA-F]{6,}',source) if 0x400000<=int(v,16)<0x10000000))
for addr in addresses:assert re.search(r'0x0*'+format(addr,'x')+r'\b',official),hex(addr)
report={'passed':True,'runtime_sha256':hashlib.sha256(data).hexdigest(),'host_contracts':rows,'status_calls':[hex(v) for v in statuscalls],'state':hex(values['state']),'game_contracts':gamechecks,'game_addresses_checked_against_tag':[hex(x) for x in addresses]}
print(json.dumps(report,indent=2))

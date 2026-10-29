"""Read-only section-mapped verification of the supplied depth integration ABI."""
from pathlib import Path
import hashlib,json,struct

def pe(path):
    data=path.read_bytes();header=struct.unpack_from('<I',data,0x3c)[0]
    assert data[:2]==b'MZ' and data[header:header+4]==b'PE\0\0'
    machine,count=struct.unpack_from('<HH',data,header+4);assert machine==0x8664
    optional=struct.unpack_from('<H',data,header+20)[0];sections=[]
    for i in range(count):
        off=header+24+optional+40*i
        size,rva,rawsize,raw=struct.unpack_from('<IIII',data,off+8)
        sections.append((rva,rawsize,raw))
    def at(rva,n):
        for begin,size,raw in sections:
            if begin<=rva and rva+n<=begin+size:return data[raw+rva-begin:raw+rva-begin+n]
        raise ValueError(hex(rva))
    return data,at

game,game_at=pe(Path('C:/Games/ReSkate/Skate.exe'))
sl,sl_at=pe(Path('C:/Games/ReSkate/sl.interposer.dll'))
checks={
    'game_sha256':hashlib.sha256(game).hexdigest()=='fbce74d5e28ef525dbba2cb4adbebc13405bdbd88f31bc940bca45e4ae88b8f9',
    'streamline_sha256':hashlib.sha256(sl).hexdigest()=='98ac2097902686441d6cbd1182fcf5cb15e9e7fa0744bfdf176ec728a4a49f1a',
    'slSetTagForFrame_prefix':sl_at(0x7680,16).hex()=='48895c240848896c2410488974241857',
    'slSetConstants_prefix':sl_at(0x77c0,16).hex()=='48895c24084889742410574883ec2049',
    # These are native import thunks, not hook targets. Call-site provenance only.
    'game_constants_import_thunk':game_at(0x547f7d1,6).hex()=='ff2511c6b600',
    'game_tags_import_thunk':game_at(0x547f7cb,6).hex()=='ff251fc6b600',
}
# Check the relative import-thunk targets from instruction bytes rather than
# assuming their displacement; the two thunks jump to the named import slots.
checks['game_constants_import_thunk']=game_at(0x547f7d1,2)==b'\xff\x25' and 0x547f7d1+6+struct.unpack('<i',game_at(0x547f7d3,4))[0]==0x5febde8
checks['game_tags_import_thunk']=game_at(0x547f7cb,2)==b'\xff\x25' and 0x547f7cb+6+struct.unpack('<i',game_at(0x547f7cd,4))[0]==0x5febdf0
report={'source':'official ReSkate v2.1.1 db99ed4d724dd77edd2abf62f3de0dbc5bb6717e',
        'streamline_sdk':'NVIDIA Streamline v2.11.1',
        'checks':checks,'passed':all(checks.values()),
        'native_provenance':{'constants_call_rva':'0x3f0fa04','depth_tag_builder_rva':'0x3f21c60',
                             'depth_tag_call_rva':'0x3f10a1c','depth_type':0,'declared_depth_state':'0xc0',
                             'lifecycle':'1 (until Present), or 0 (only now) with a command list'}}
print(json.dumps(report,indent=2))
raise SystemExit(0 if report['passed'] else 1)

"""Read-only PE section-mapped verification against pinned ReSkate 2.1.1 contracts."""
import argparse, hashlib, json, pathlib, re, struct
p=argparse.ArgumentParser();p.add_argument('image',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path);a=p.parse_args()
data=a.image.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
assert data[:2]==b'MZ' and data[pe:pe+4]==b'PE\0\0'
machine,nsections=struct.unpack_from('<HH',data,pe+4);assert machine==0x8664
optional=struct.unpack_from('<H',data,pe+20)[0];sections=[]
for i in range(nsections):
    off=pe+24+optional+40*i
    name=data[off:off+8].rstrip(b'\0').decode();vsize,rva,rawsize,raw=struct.unpack_from('<IIII',data,off+8)
    sections.append((name,rva,rawsize,raw))
def bytes_at(rva,count):
    for name,start,size,raw in sections:
        if start<=rva and rva+count<=start+size:return data[raw+rva-start:raw+rva-start+count]
    raise ValueError(hex(rva))
root=pathlib.Path(__file__).resolve().parents[1]
text=(root/'src/native_contracts.h').read_text()
contracts=[]
for m in re.finditer(r'\{(0x[0-9a-fA-F]+),\s*\{([^{}]+)\}',text):
    rva=int(m[1],16);expected=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',m[2]));contracts.append((rva,expected))
contracts.append((0x2e77190,bytes.fromhex('405357415541564883ec388b81c4000000')))
contracts.append((0x58ee560,bytes.fromhex('8b05d26a9201480301c3')))
results=[{'rva':hex(rva),'bytes':len(expected),'match':bytes_at(rva,len(expected))==expected} for rva,expected in contracts]
report={'source_tag':'v2.1.1','source_commit':'db99ed4d724dd77edd2abf62f3de0dbc5bb6717e','image':str(a.image),'sha256':hashlib.sha256(data).hexdigest(),'contracts':results,'passed':all(x['match'] for x in results)}
output=json.dumps(report,indent=2);print(output)
if a.output:a.output.write_text(output+'\n')
raise SystemExit(0 if report['passed'] else 1)

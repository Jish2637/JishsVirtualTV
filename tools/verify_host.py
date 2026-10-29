"""Pinned host verification, with PE section mapping rather than file offsets."""
import hashlib,json
from inspect_host import data,file_at
expected='d79ff144768ce8b0a8e992c9f134c670a3ff85bb3b437a0eb915876973014301'
contracts=[('Mods menu',0xe6b7c0,'48895c241889542410574883ec50'),('render',0xe39440,'488bc4488958104889701848897820'),('ImGui context getter',0xfcc5b0,'488b0581069300c3')]
rows=[dict(name=n,rva=hex(r),prefix=h,match=data[file_at(r):file_at(r)+len(bytes.fromhex(h))]==bytes.fromhex(h)) for n,r,h in contracts]
report=dict(source_tag='v2.1.1',source_commit='db99ed4d724dd77edd2abf62f3de0dbc5bb6717e',sha256=hashlib.sha256(data).hexdigest(),expected_sha256=expected,contracts=rows,state=dict(singleton='0x1864198',render_mutex='0x0',ready='0x328',failed='0x330',swapchain='0x4f0',queue='0x4f8',context='0x5a8',model='0x5e0',menu='0x6c10'),passed=hashlib.sha256(data).hexdigest()==expected and all(r['match'] for r in rows))
from pathlib import Path
Path('host-verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
raise SystemExit(0 if report['passed'] else 1)

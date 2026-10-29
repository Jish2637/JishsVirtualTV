"""Package the life2 visibility-logging build, preserving all earlier deliverables."""
from pathlib import Path
import zipfile,hashlib,json
root=Path(__file__).resolve().parents[1];out=root.parent/'deliverables'
base=zipfile.ZipFile(out/'JishsVirtualTV-source-v0.7.0-life2.zip')
changed=['src/main.cpp','src/dx12_renderer.cpp','src/host_bridge.cpp','src/native_attachment.cpp','src/tv_runtime.h']
added=['src/gate_trace.h']
for name in base.namelist():
 rel=name.removeprefix('JishsVirtualTV/')
 if name.endswith(('.cpp','.h','CMakeLists.txt')) and rel not in changed:
  assert (root/rel).read_bytes()==base.read(name),rel
binary=root/'build-baseline-070/Release/JishsVirtualTV.dll';assert binary.is_file()
report={'build':'v0.7.0-life2-log1','base':'v0.7.0-life2','dll_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'changed':changed,'added':added,'validation':'VS2026 Release, seven CTest tests and RTX 5070 Ti debug-layer GPU tests passed; visibility logging build not observed in game'}
notes=(root/'DIAGNOSTICS-life2-log1.md').read_bytes()
with zipfile.ZipFile(out/'JishsVirtualTV-v0.7.0-life2-log1.zip','w',zipfile.ZIP_DEFLATED) as z:
 z.write(binary,'JishsVirtualTV/JishsVirtualTV.dll');z.writestr('JishsVirtualTV/README.md',notes)
 for rel in ['NOTICE.md','LICENSE','vendor/minhook/LICENSE.txt','vendor/imgui/LICENSE.txt']:
  z.writestr('JishsVirtualTV/'+rel,base.read('JishsVirtualTV/'+rel))
 z.writestr('JishsVirtualTV/build-manifest.json',json.dumps(report,indent=2)+'\n')
with zipfile.ZipFile(out/'JishsVirtualTV-source-v0.7.0-life2-log1.zip','w',zipfile.ZIP_DEFLATED) as z:
 for name in base.namelist():
  rel=name.removeprefix('JishsVirtualTV/')
  z.writestr(name,(root/rel).read_bytes() if rel in changed else base.read(name))
 for rel in added+['DIAGNOSTICS-life2-log1.md','tools/package_life2_log1.py']:z.write(root/rel,'JishsVirtualTV/'+rel)
 z.writestr('JishsVirtualTV/build-manifest-life2-log1.json',json.dumps(report,indent=2)+'\n')
for suffix in ['','-source']:
 p=out/f'JishsVirtualTV{suffix}-v0.7.0-life2-log1.zip'
 with zipfile.ZipFile(p) as z:
  assert z.testzip() is None
  assert not any(n.endswith('.exe') for n in z.namelist())
  if not suffix:assert hashlib.sha256(z.read('JishsVirtualTV/JishsVirtualTV.dll')).hexdigest()==report['dll_sha256']
print(json.dumps(report,indent=2))

"""Package depth occlusion over the preserved, working url1 delivery."""
from pathlib import Path
import zipfile,hashlib,json
root=Path(__file__).resolve().parents[1];out=root.parent/'deliverables'
changed=['CMakeLists.txt','src/main.cpp','src/dx12_renderer.cpp']
added=['src/depth_capture.cpp','src/depth_capture.h','tools/verify_depth.py','tools/package_depth1.py','OCCLUSION-depth1.md']
added += [str(p.relative_to(root)).replace('\\','/') for p in sorted((root/'vendor/streamline211').glob('*.h'))]
binary=(root/'build-baseline-070/Release/JishsVirtualTV.dll').read_bytes()
notes=(root/'OCCLUSION-depth1.md').read_bytes()
manifest={'build':'v0.7.0-depth1','base':'v0.7.0-url1','dll_sha256':hashlib.sha256(binary).hexdigest(),
          'changed':changed,'added':added,'validation':'VS2026 Release; eight CTest tests; WARP and RTX 5070 Ti DX12 debug-layer occlusion tests; supplied Streamline/native DX12 hook installation test passed; in-game test pending'}
for source in (False,True):
    stem='JishsVirtualTV-source' if source else 'JishsVirtualTV';target=out/f'{stem}-v0.7.0-depth1.zip'
    assert not target.exists(),target
    with zipfile.ZipFile(out/f'{stem}-v0.7.0-url1.zip') as old,zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as new:
        for name in old.namelist():
            rel=name.removeprefix('JishsVirtualTV/');data=old.read(name)
            if source and rel in changed:data=(root/rel).read_bytes()
            elif source and name.endswith(('.cpp','.h','CMakeLists.txt')):assert (root/rel).read_bytes()==data,rel
            if not source and rel=='JishsVirtualTV.dll':data=binary
            if not source and rel=='README.md':data=notes
            if not source and rel=='build-manifest.json':data=json.dumps(manifest,indent=2).encode()
            new.writestr(name,data)
        if source:
            for rel in added:new.write(root/rel,'JishsVirtualTV/'+rel)
            new.writestr('JishsVirtualTV/build-manifest-depth1.json',json.dumps(manifest,indent=2))
        else:
            new.writestr('JishsVirtualTV/OCCLUSION-depth1.md',notes)
            # NVIDIA's SDK license notice is included in each vendored header.
            new.writestr('JishsVirtualTV/vendor/streamline211/LICENSE.txt',(root/'vendor/streamline211/sl_struct.h').read_text().split('*/',1)[0]+'*/\n')
    with zipfile.ZipFile(target) as check:
        assert check.testzip() is None
        assert not any(n.endswith('.exe') and not n.endswith(('/resolver/yt-dlp.exe','/resolver/deno.exe')) for n in check.namelist())
        if not source:assert check.read('JishsVirtualTV/JishsVirtualTV.dll')==binary
    print(target,flush=True)
print(json.dumps(manifest,indent=2))

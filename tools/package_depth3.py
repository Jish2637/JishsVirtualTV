from pathlib import Path
import zipfile,hashlib,json
root=Path(__file__).resolve().parents[1];out=root.parent/'deliverables'
changed=['src/main.cpp','src/depth_capture.cpp','src/dx12_renderer.cpp']
added=['OCCLUSION-depth3.md','tools/package_depth3.py']
binary=(root/'build-baseline-070/Release/JishsVirtualTV.dll').read_bytes();notes=(root/added[0]).read_bytes()
manifest={'build':'v0.7.0-depth3','base':'v0.7.0-depth2','dll_sha256':hashlib.sha256(binary).hexdigest(),'changed':changed,'added':added,
          'validation':'VS2026 Release; 8/8 CTest; Native-world/renderer-relative camera GPU readback, projection mismatch rejection, D32S8 and mipmapped depth; RTX 5070 Ti debug layer with real depth hooks; in-game test pending'}
for source in (False,True):
    stem='JishsVirtualTV-source' if source else 'JishsVirtualTV';target=out/f'{stem}-v0.7.0-depth3.zip';assert not target.exists()
    with zipfile.ZipFile(out/f'{stem}-v0.7.0-depth2.zip') as old,zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=1) as new:
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
            new.writestr('JishsVirtualTV/build-manifest-depth3.json',json.dumps(manifest,indent=2))
        else:new.writestr('JishsVirtualTV/OCCLUSION-depth3.md',notes)
    with zipfile.ZipFile(target) as check:
        assert check.testzip() is None
        assert not any(n.endswith('.exe') and not n.endswith(('/resolver/yt-dlp.exe','/resolver/deno.exe')) for n in check.namelist())
        if not source:assert check.read('JishsVirtualTV/JishsVirtualTV.dll')==binary
    print(target,flush=True)
print(json.dumps(manifest,indent=2))

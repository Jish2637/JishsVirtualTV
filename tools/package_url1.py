"""Package the URL resolver without overwriting earlier known-good builds."""
from pathlib import Path
import hashlib, json, zipfile

root=Path(__file__).resolve().parents[1]
out=root.parent/'deliverables'
version='v0.7.0-url1'
changed=['CMakeLists.txt','src/main.cpp','src/vlc_player.cpp','src/vlc_player.h']
added=['src/url_resolver.cpp','src/url_resolver.h','tests/resolver_tests.cpp',
       'tools/fetch_resolver.py','tools/package_url1.py','URL-RESOLVER-url1.md',
       'vendor/nlohmann/json.hpp','vendor/nlohmann/LICENSE.MIT']
dependencies=sorted(p for p in (root/'resolver').iterdir() if p.is_file())
binary=root/'build-baseline-070/Release/JishsVirtualTV.dll'
manifest={'build':version,'base':'v0.7.0-life2-camera2',
          'dll_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
          'changed':changed,'added':added,
          'dependencies':json.loads((root/'resolver/manifest.json').read_text()),
          'validation':'VS2026 Release; 8/8 CTest; hardware DX12; supplied YouTube resolved and decoded via real libVLC with advancing frames and audio track; in-game test pending'}
notes=(root/'URL-RESOLVER-url1.md').read_bytes()
for source in (False,True):
    stem='JishsVirtualTV-source' if source else 'JishsVirtualTV'
    target=out/f'{stem}-{version}.zip'
    if target.exists():raise FileExistsError(target)
    with zipfile.ZipFile(out/f'{stem}-v0.7.0-life2-camera2.zip') as old,zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as new:
        for name in old.namelist():
            rel=name.removeprefix('JishsVirtualTV/');data=old.read(name)
            if source and rel in changed:data=(root/rel).read_bytes()
            elif source and name.endswith(('.cpp','.h','CMakeLists.txt')):
                assert (root/rel).read_bytes()==data,rel
            if not source and rel=='JishsVirtualTV.dll':data=binary.read_bytes()
            if not source and rel=='README.md':data=notes
            if not source and rel=='build-manifest.json':data=(json.dumps(manifest,indent=2)+'\n').encode()
            new.writestr(name,data)
        if source:
            for rel in added:new.write(root/rel,'JishsVirtualTV/'+rel)
            new.writestr('JishsVirtualTV/build-manifest-url1.json',json.dumps(manifest,indent=2)+'\n')
        else:
            new.writestr('JishsVirtualTV/URL-RESOLVER-url1.md',notes)
            new.write(root/'vendor/nlohmann/LICENSE.MIT','JishsVirtualTV/vendor/nlohmann/LICENSE.MIT')
        for p in dependencies:new.write(p,'JishsVirtualTV/resolver/'+p.name)
    with zipfile.ZipFile(target) as check:
        assert check.testzip() is None
        assert not any(n.endswith('.exe') and not n.endswith(('/resolver/yt-dlp.exe','/resolver/deno.exe')) for n in check.namelist())
        if not source:assert hashlib.sha256(check.read('JishsVirtualTV/JishsVirtualTV.dll')).hexdigest()==manifest['dll_sha256']
    print(target)
print(json.dumps(manifest,indent=2))

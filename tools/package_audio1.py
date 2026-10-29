from pathlib import Path
import zipfile, hashlib, json

root = Path(__file__).resolve().parents[1]
out = root.parent / 'deliverables'
changed = ['CMakeLists.txt', 'src/main.cpp', 'src/vlc_player.h', 'src/vlc_player.cpp',
           'src/tv_runtime.h', 'src/tv_runtime.cpp', 'src/dx12_renderer.h',
           'src/dx12_renderer.cpp', 'tests/resolver_tests.cpp']
added = ['src/tv_audio.h', 'src/tv_audio.cpp', 'src/audio_position.h',
         'tests/audio_tests.cpp', 'AUDIO-audio1.md', 'tools/package_audio1.py']
binary = (root / 'build-baseline-070/Release/JishsVirtualTV.dll').read_bytes()
notes = (root / 'AUDIO-audio1.md').read_bytes()
report = json.loads((root / 'audio1-validation.json').read_text())
manifest = dict(build='v0.7.0-audio1', base='v0.7.0-depth3',
                dll_sha256=hashlib.sha256(binary).hexdigest(),
                changed=changed, added=added, validation=report)
for source in (False, True):
    stem = 'JishsVirtualTV-source' if source else 'JishsVirtualTV'
    target = out / f'{stem}-v0.7.0-audio1.zip'
    assert not target.exists(), target
    with zipfile.ZipFile(out / f'{stem}-v0.7.0-depth3.zip') as old, zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=1) as new:
        for name in old.namelist():
            rel = name.removeprefix('JishsVirtualTV/')
            data = old.read(name)
            if source and rel in changed:
                data = (root / rel).read_bytes()
            elif source and name.endswith(('.cpp', '.h', 'CMakeLists.txt')):
                assert (root / rel).read_bytes() == data, rel
            if not source and rel == 'JishsVirtualTV.dll':
                data = binary
            if not source and rel == 'README.md':
                data = notes
            if not source and rel == 'build-manifest.json':
                data = json.dumps(manifest, indent=2).encode()
            new.writestr(name, data)
        if source:
            for rel in added:
                new.write(root / rel, 'JishsVirtualTV/' + rel)
            new.writestr('JishsVirtualTV/audio1-validation.json', json.dumps(report, indent=2))
            new.writestr('JishsVirtualTV/build-manifest-audio1.json', json.dumps(manifest, indent=2))
        else:
            new.writestr('JishsVirtualTV/AUDIO-audio1.md', notes)
            new.writestr('JishsVirtualTV/audio1-validation.json', json.dumps(report, indent=2))
    with zipfile.ZipFile(target) as check:
        assert check.testzip() is None
        assert not any(n.endswith('.exe') and not n.endswith(('/resolver/yt-dlp.exe', '/resolver/deno.exe')) for n in check.namelist())
        if not source:
            assert check.read('JishsVirtualTV/JishsVirtualTV.dll') == binary
    print(target, flush=True)
print(json.dumps(manifest, indent=2))

"""Package the local build and matching source; never installs or publishes anything."""
import hashlib,json,pathlib,zipfile
root=pathlib.Path(__file__).resolve().parents[1]
out=root.parent/'deliverables';out.mkdir(exist_ok=True)
binary=root/'build/Release/JishsVirtualTV.dll'
assert binary.is_file()
with zipfile.ZipFile(out/'JishsVirtualTV-v0.7.0.zip','w',zipfile.ZIP_DEFLATED) as z:
    z.write(binary,'JishsVirtualTV/JishsVirtualTV.dll')
    for name in ['README.md','STATUS.md','VALIDATION-v0.7.0.md','NOTICE.md','LICENSE','native-verification.json','host-verification.json','vendor/minhook/LICENSE.txt','vendor/imgui/LICENSE.txt']:
        z.write(root/name,'JishsVirtualTV/'+name)
with zipfile.ZipFile(out/'JishsVirtualTV-source-v0.7.0.zip','w',zipfile.ZIP_DEFLATED) as z:
    for path in sorted(root.rglob('*')):
        relative=path.relative_to(root)
        if path.is_file() and not any(p.startswith('build') or p=='__pycache__' for p in relative.parts[:-1]) and path.suffix not in ['.log','.url','.screen','.exe','.dll','.pdb','.obj','.lib','.exp','.dmp']:
            z.write(path,'JishsVirtualTV/'+relative.as_posix())
report={'version':'0.7.0','dll_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
        'validation':'five CTest tests pass including synthetic host UI; WARP and RTX 5070 Ti debug-layer GPU tests pass; native/host verification passes; v0.7.0 not observed in game',
        'limits':['No scene-depth occlusion','Reselect attachment after a fresh game/world session']}
(out/'build-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
for p in sorted(out.iterdir()):print(p.name,p.stat().st_size)

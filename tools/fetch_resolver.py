"""Fetch pinned portable dependencies from their official upstream releases."""
from pathlib import Path
import hashlib, io, json, urllib.request, zipfile

root = Path(__file__).resolve().parents[1]
dest = root / 'resolver'
dest.mkdir(exist_ok=True)
def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()
def checked(url, sha):
    data = fetch(url)
    assert hashlib.sha256(data).hexdigest() == sha, url
    return data

ytdlp = '2026.10.21'
deno = 'v2.9.8'
(dest/'yt-dlp.exe').write_bytes(checked(
    f'https://github.com/yt-dlp/yt-dlp/releases/download/{ytdlp}/yt-dlp.exe',
    '33121d192d706e62f684a06f80918e3f3a8ba52ac3a65213b6951c39c7d9f555'))
archive = checked(f'https://github.com/denoland/deno/releases/download/{deno}/deno-x86_64-pc-windows-msvc.zip',
                  'cba84b26e0df5ae7f74fc66b86a95b866872a86292ebd84fa1e959927edd93bf')
with zipfile.ZipFile(io.BytesIO(archive)) as z:
    (dest/'deno.exe').write_bytes(z.read('deno.exe'))
for name,url in {
    'yt-dlp-LICENSE.txt':f'https://raw.githubusercontent.com/yt-dlp/yt-dlp/{ytdlp}/LICENSE',
    'yt-dlp-THIRD_PARTY_LICENSES.txt':f'https://raw.githubusercontent.com/yt-dlp/yt-dlp/{ytdlp}/THIRD_PARTY_LICENSES.txt',
    'deno-LICENSE.txt':f'https://raw.githubusercontent.com/denoland/deno/{deno}/LICENSE.md',
}.items():
    (dest/name).write_bytes(fetch(url))
vendor=root/'vendor/nlohmann';vendor.mkdir(exist_ok=True)
for local,remote in [('json.hpp','single_include/nlohmann/json.hpp'),('LICENSE.MIT','LICENSE.MIT')]:
    (vendor/local).write_bytes(fetch('https://raw.githubusercontent.com/nlohmann/json/v3.12.0/'+remote))
manifest={'yt-dlp':ytdlp,'deno':deno,'json':'v3.12.0',
          'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in dest.iterdir() if p.is_file() and p.name!='manifest.json'}}
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))

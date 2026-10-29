"""Offline acceptance check against the actual bundled yt-dlp, without network/video downloads."""
from pathlib import Path
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
(root / "build").mkdir(exist_ok=True)
template = "%(.{_type,url,vcodec,acodec,has_drm,http_headers,requested_formats})j"
with tempfile.TemporaryDirectory(prefix="compact-resolver-", dir=root / "build") as temp:
    info = {"id": "fixture", "title": "fixture", "extractor": "generic", "webpage_url": "https://media.invalid/watch", "description": "x" * 9000000,
            "url": "https://media.invalid/video.mp4", "ext": "mp4", "vcodec": "h264", "acodec": "aac", "http_headers": {"User-Agent": "VirtualTV fixture"}}
    fixture = Path(temp) / "fixture.json"
    for separate in (False, True):
        if separate:
            info.pop("url")
            info["formats"] = [
                {"format_id": "v", "url": "https://media.invalid/video.mp4", "ext": "mp4", "vcodec": "h264", "acodec": "none", "height": 720},
                {"format_id": "a", "url": "https://media.invalid/audio.m4a", "ext": "m4a", "vcodec": "none", "acodec": "aac"}]
        fixture.write_text(json.dumps(info), encoding="utf-8")
        args = [str(root / "resolver/yt-dlp.exe"), "--ignore-config", "--no-plugin-dirs", "--simulate", "--load-info-json", str(fixture), "--print", template]
        if separate:
            args += ["--format", "bv+ba"]
        result = subprocess.run(args, capture_output=True, timeout=30, check=True)
        data = json.loads(result.stdout)
        assert "description" not in data and "formats" not in data and len(result.stdout) < 8192
        if separate:
            assert len(data["requested_formats"]) == 2
        else:
            assert data["url"] == "https://media.invalid/video.mp4"
        print("PASS", "separate audio/video" if separate else "combined stream", "9 MB metadata ->", len(result.stdout), "bytes")

# ReSkate 20261116 compatibility audit

The installed DLL matches the official 20261116 release, built from tag commit
`8e4aefca153ce9cb622074ae4de05f81fa71d09c`.

| Host contract | RVA |
| --- | --- |
| State pointer | 0x1ea1248 |
| Status export | 0xffba10 |
| Render callback | 0x107f5f0 |
| Mutex lock / unlock | 0x15449e4 / 0x1544a10 |
| ImGui context getter / global | 0x132fd70 / 0x1f3ac18 |
| Allocator / free / user data | 0x1c417e0 / 0x1c417e8 / 0x1f3ac20 |

The offline audit checked the image size, all five entry fingerprints, the
shared singleton and lock/unlock calls in status and render, and the ImGui
global references. The runtime checks the exact release DLL hash.

MSVC compiled the layout probe against the official tagged source. State
offsets in bytes are render mutex 0, visible 644, ready 833, failed 840,
swapchain 1288, queue 1296, context 2752, model 2808, and menu 29128.
The mutex remains 80 bytes and Status remains 16 bytes. Model grew to
26312 bytes and the menu field moved by 64 bytes. The active vendored
headers match the new tag byte for byte.

The game executable hash and its picking, camera, placement, and client
realm contracts passed. See AUDIT-20261116.json for the recorded results.

Build tools/layout20261116 in Release with `-DUPSTREAM=TAG_SOURCE` and
save the probe output as layout.txt. Then run:

```
python tools/audit_20261116.py ReSkate.dll --upstream TAG_SOURCE --game Skate.exe --layout-report layout.txt
```

The Release build and all 31 CTest tests passed. The user confirmed the build works in game.

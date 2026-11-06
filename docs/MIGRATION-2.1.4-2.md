# ReSkate 2.1.4-2 compatibility audit

Official tag: `v2.1.4-2`, commit `88540f254cc7d2cca79dcde07d5639daf9f56ddf`.
Installed ReSkate.dll matches the official release SHA-256:
`f7b1562667892187356b8617a3964ceaefea04324bfa7850c05d340da01c09cd`.

| Host contract | 2.1.4-2 RVA |
| --- | --- |
| State pointer | 0x1da88f0 |
| Status export | 0xfc2e50 |
| Render callback | 0x102fab0 |
| Mods page | 0x1089a00 |
| ImGui context getter | 0x12c0820 |
| Mutex lock / unlock | 0x14d1554 / 0x14d1580 |
| ImGui context global | 0x1e41f68 |
| Allocator / free / user data | 0x1bb7fc0 / 0x1bb7fc8 / 0x1e41f70 |

MSVC Release compilation against the official tagged State declaration asserts
every field used by the bridge: render_mutex 0, visible 644, ready 833, failed 840,
swapchain 1288, queue 1296, context 2752, model 2808, menu 28968. The model grew
from 26128 to 26152 bytes; menu moved from 28944 to 28968. Editor remains 24192
and debug remains 128. The model-size assertion and active tagged headers were
updated together; addresses alone would not make the prior build compatible.

Offline disassembly verifies the status export and render callback use the same
singleton getter and mutex functions. Render and Mods functions match their
previous normalized instruction sequences. ImGui context and allocator globals
are checked through RIP-relative references. Runtime initialization requires the
exact DLL hash and six function-entry fingerprints, rejecting other builds.

The game executable retains SHA-256
`fbce74d5e28ef525dbba2cb4adbebc13405bdbd88f31bc940bca45e4ae88b8f9`.
All native picking/placement function fingerprints, the client-realm getter,
and camera/placement address inventory pass against this executable and the
new tagged build declarations. Game RVAs and record field offsets are unchanged.
TV/audio/input/depth/sync mutexes are owned locally. GPU methods use SDK vtable
contracts rather than ReSkate addresses. The companion plugin uses the public API;
2.1.4-2 intentionally suppresses plugin messages from ignored players.

To reproduce, configure `tools/layout2142` with `-DUPSTREAM=TAG_SOURCE`, build
Release and save its stdout as layout.txt. Then run:

```
python tools/audit_2142.py ReSkate.dll --upstream TAG_SOURCE --game Skate.exe --layout-report layout.txt
```

The audit validates all active vendored headers byte-for-byte against the tag.
See AUDIT-2.1.4-2.json for the machine-readable results. Automated tests do not
replace live checks of startup, F8/Insert, screens, speakers, placement, playlists,
session changes, multiplayer and exit. Existing DX12 camera drift remains.

Validation: Release build succeeded and all 31 CTest tests passed. Installed TV
DLL SHA-256: `745c0c8db5d784c0a78f5f03d711bc07529a480f3e0ec975eca623a31eedcf21`. The previous installed DLL was backed up.
The user tested this build in game and confirmed it working on 2026-11-06.
Two-client synchronization has not been separately confirmed.

# ReSkate 2.1.4-4 compatibility audit

Official tag `v2.1.4-4`: commit `be6f26237f621d1f49e05720b3a2234461444b35`.
The installed DLL matches the official release SHA-256:
`1bcd400b6c487f497ab0110263444864c0269a6309f7143d22b5bc04863d544e`.

| Host contract | 2.1.4-4 RVA |
| --- | --- |
| State pointer | 0x1e6bf80 |
| Status export | 0xfed180 |
| Render callback | 0x106bb10 |
| Mutex lock / unlock | 0x1517e44 / 0x1517e70 |
| ImGui context getter | 0x1305d20 |
| ImGui context global | 0x1f05888 |
| Allocator / free / user data | 0x1c0f240 / 0x1c0f248 / 0x1f05890 |

The model grew from 26152 to 26248 bytes. The menu offset moved from 28968
(`0x7128`) to 29064 (`0x7188`). All active model headers and their transitive
dependencies are imported from the official tag.

An MSVC Release probe compiled against the tagged State declaration asserts every
host field used by TV: render_mutex 0, visible 644, ready 833, failed 840, swapchain
1288, queue 1296, context 2752, model 2808, menu 29064. It also asserts model size
26248, mutex size 80 and Status size 16. Debug remains 128 and editor 24192;
production ImGui size assertions remain active.

Offline disassembly proves status and rendering acquire the same singleton and
call the verified host mutex functions. The render callback, ImGui allocation and
free functions match the previous normalized instruction sequences. The context
getter is independently located by its reference to the same ImGui global;
allocator/free/user-data globals have verified RIP-relative references.

The unused Mods-page binary address/fingerprint dependency is removed from the
active host contract and startup checks. Production TV uses its independent menu
and render callback, and does not install the legacy Mods hook. The synthetic
embedded-menu test still validates the updated model/menu/context layouts without
calling any Mods-page address in ReSkate. No guessed Mods-page RVA is retained in
the active 2.1.4-4 contract.

Runtime initialization requires the exact DLL hash, correct status export and five
verified function-entry prefixes. Other DLLs are rejected before installing hooks.
Historical contract headers and vendor directories remain for audit provenance;
the active bridge and CMake targets use host_2144.h and vendor/reskate2144 only.

The game executable SHA-256 remains
`fbce74d5e28ef525dbba2cb4adbebc13405bdbd88f31bc940bca45e4ae88b8f9`.
All native picking, placement and client-realm function fingerprints pass.
Camera/placement address inventory matches the tagged game build declarations.
Game addresses and record field offsets remain unchanged for this identical
executable. TV/audio/depth/input/sync mutexes are locally owned; GPU vtable methods
use SDK interfaces. The companion bridge uses the public plugin API.

Reproduce the layout probe by configuring tools/layout2144 with
`-DUPSTREAM=TAG_SOURCE`, building Release and saving the program's stdout as
layout.txt. Then run:

```
python tools/audit_2144.py ReSkate.dll --upstream TAG_SOURCE --game Skate.exe --layout-report layout.txt
```

The audit verifies every active vendored header against the tag byte-for-byte.
See AUDIT-2.1.4-4.json for complete results. Live gameplay and two-client checks
remain outstanding for this build. Existing DX12 camera-motion drift remains.

Validation: Release build succeeded and all 31 CTest tests passed. Installed TV
DLL SHA-256: `b1dcbf3963adddf3827f25a9ad0958dcd22529a1225cc076dcb722fb9a481aef`. The previous installed DLL was backed up.
Live gameplay and two-client testing remain outstanding.

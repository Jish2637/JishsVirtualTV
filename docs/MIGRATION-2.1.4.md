# ReSkate 2.1.4 compatibility audit

Based on SelectiveSync5. Official tag commit: `919b04e9a5aa708974b8ea7298c61333104d3905`.
The installed DLL matches the official release SHA-256:
`39195d824d775e5046480d66cb62dae0d709648f145414cb952ed2aa8afb99b3`.

| Host contract | 2.1.4 RVA |
| --- | --- |
| State pointer | 0x1d945a0 |
| Status export | 0xfb65f0 |
| Render callback | 0x1021c20 |
| Mods page | 0x107bb70 |
| ImGui context getter | 0x12af1a0 |
| Mutex lock / unlock | 0x14c0354 / 0x14c0380 |
| ImGui context global | 0x1e2dc18 |
| Allocator / free / user data | 0x1ba3f70 / 0x1ba3f78 / 0x1e2dc20 |

MSVC Release compilation against the official tagged `State` declaration asserts
every field used by the bridge: render_mutex 0, visible 644, ready 833, failed 840,
swapchain 1288, queue 1296, context 2752, model 2808, menu 28944. Model size remains
26128; editor 24192; debug 128. Most fields after visible moved by eight bytes.
The layout probe is `tools/layout214`; configure with `-DUPSTREAM=TAG_SOURCE`.

Offline disassembly checks that status and rendering acquire the same singleton
and call the verified host mutex functions. The render and Mods candidates match
the previous functions' normalized instruction sequences. Runtime initialization
requires the exact DLL hash, status export and six entry fingerprints.

`tools/audit_214.py` audits the installed DLL without loading it, validates every
active vendored header against the tag, verifies native game function fingerprints
and checks the placement/camera RVA inventory against tagged build declarations.
The game executable remains pinned to SHA-256
`fbce74d5e28ef525dbba2cb4adbebc13405bdbd88f31bc940bca45e4ae88b8f9`.
Game RVAs remain unchanged. GPU vtable slots use SDK interfaces. TV, audio, input,
depth and session mutexes are locally owned and require no host RVA migration.
The companion bridge continues to use the public plugin API.

The Release CTest suite covers synthetic host integration, native placement,
GPU rendering, playback and sync logic. These checks do not establish live
gameplay or two-client synchronization. Cold startup, F8/Insert, screens,
speakers, placement, session changes and exit still require an in-game check.
The existing DX12 camera-motion drift limitation remains.

Validation: Release build succeeded; all 31 CTest tests passed. File transport
tests use a short workspace TEMP/TMP directory because the sandbox temp path
causes atomic file replacement failures. Installed DLL SHA-256:
`6fc5edf96032fef9bf82a7a15a3eb7450c63e012dac735a66249391add7673f6`.

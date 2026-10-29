# ReSkate 2.1.3 compatibility validation

Based on SelectiveSync5. Runtime SHA-256: 139e932ad6328b898b2ff8c2aac3e0cd5ced6545c2f041aa8534239f97e59f9b. Official tagged source: v2.1.3, commit 9506a39.

Host state: 0x1cf3ca0; status: 0xf8be60; TV render callback: 0xff5870; Mods page: 0x104e6a0; ImGui getter: 0x1279dc0. Mutex lock/unlock: 0x148a6f4 / 0x148a720. Offline disassembly verifies the status export calls the shared singleton getter and these mutex functions, and the TV render callback uses the same getter and mutex functions. The Mods page matches the prior instruction sequence; the render candidate was independently matched and its call relationships checked.

An MSVC Release layout probe compiled directly against official tagged headers confirms State offsets: render_mutex 0, visible 644, ready 825, failed 832, swapchain 1280, queue 1288, context 2744, model 2800, menu 28936. Model size 26128; editor 24192; debug 128. The active vendored model headers and their transitive dependencies are copied from the tag. ImGui size assertions remain active. Legacy test-only allocation slots are explicitly separate from production rendering.

All native picking/placement fingerprints are checked against the pinned game executable. Large camera/placement RVAs in the active reader are inventoried against the official game-build declarations. Those game contracts did not move; ReSkate host addresses did. GPU command-list methods use SDK interfaces/vtable slots, not ReSkate RVAs. TV/audio/session mutexes are owned locally; the host render mutex remains at State offset zero. The plugin bridge uses the public API and retains its protocol.

Run tools/audit_213.py with pefile/capstone, the official DLL, tagged source and the supported game executable. AUDIT-2.1.3.json records results. Runtime initialization requires the exact official DLL hash and exact entry prefixes; unreviewed runtimes fail closed.

Windows Release build and the complete configured CTest suite are run for this package. This does not establish live gameplay, camera accuracy, native object restoration or two-client synchronization. Test cold startup, F8/Insert input, local/synced screens, speakers, placement/restore, playlists, resize, level/session changes and exit on AMD and NVIDIA. The existing DX12 camera drift limitation remains. Installed game files are untouched.

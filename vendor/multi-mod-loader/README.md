# Bundled multi-mod loader

Unmodified loader source from Jish2637/JishsFirstPersonMod tag `1.36.7-reskate-2.1.3`, commit `e159963be11b9127363c5301841f814c43290c6b`. Source dependencies are copied with it; this standalone CMake entry point is supplied for rebuilding the loader only.

Upstream source: https://github.com/Jish2637/JishsFirstPersonMod/releases/tag/1.36.7-reskate-2.1.3

The release uses the upstream built dinput8.dll, SHA-256 `6a10539aeba7ea970eab5b07d86f03ae482571b008d3ed48e0f01e3f7aa2a4cf`. It waits for ReSkate's exported overlay-ready status, discovers scripts/Custom DLLs and preserves dinput8_mods.txt. It validates the game executable, rather than pinning a ReSkate version. Individual mods still enforce their own runtime compatibility.

Build on Windows x64 with Visual Studio 2026 C++ tools:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

See LICENSE.txt. This publication did not rebuild the upstream loader.

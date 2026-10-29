# Source provenance

Native contracts and adapted picking/transform/camera behavior derive from Dingo-Shenanigans/ReSkate v2.1.1 under GNU GPL v3. The license is reproduced in LICENSE. VALIDATION.md identifies the pinned commit and reference paths.

MinHook v1.3.4, copyright Tsuda Kageyu and contributors, uses its BSD-style license, reproduced in vendor/minhook/LICENSE.txt and source headers.

Original JishsVirtualTV v0.4.0 was supplied by the user. This continuation preserves its standalone DLL, VLC decoding, F8 controls and geometry. Complete source accompanies the binary.

v0.6.0 vendors Dear ImGui and ReSkate's adapted DX12 backend from official ReSkate v2.1.1, commit db99ed4d724dd77edd2abf62f3de0dbc5bb6717e, External/imgui. Dear ImGui's MIT license is reproduced in vendor/imgui/LICENSE.txt. ReSkate backend adaptations retain attribution in their source and are covered by the enclosing project's GPL license. No installed client files are modified; this code is compiled into the TV DLL.

v0.7.0 includes exact official v2.1.1 model headers in vendor/reskate211 for the pinned private menu ABI. Embedded allocation/context ownership is adapted from Jish2637/JishsFirstPersonMod commit fe80fa7bf53d69ef0c2a6121802f76ae2c36f068. The original reference mods are unchanged.


## Bundled multi-mod loader

The dinput8.dll loader is redistributed unmodified from Jish2637/JishsFirstPersonMod 1.36.7-reskate-2.1.3. Corresponding loader source, upstream provenance, build instructions and license are in vendor/multi-mod-loader. The upstream version names the First Person release; this TV build remains pinned to ReSkate 2.1.2.

Resolver licenses are in resolver/ in source and Scripts/Custom/resolver/ in the install package. Streamline header notices are retained in vendor/streamline211.

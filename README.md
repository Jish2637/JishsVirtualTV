# Jish's Virtual TV

**VERY EARLY WORK IN PROGRESS — experimental alpha. Expect bugs, rough edges and compatibility limits. This is not a finished or broadly supported mod.**

Watch videos on object-attached screens in ReSkate, build a setup with directional speakers, and opt in to watching another player's shared setup.

This public preview uses the known working **SelectiveSync5, updated for ReSkate 2.1.3** build. Known working does not mean bug-free: DX12/ReShade screens can still shift against objects during camera or character movement, and performance varies between PCs.

## Download and requirements

Download **JishsVirtualTV-SelectiveSync5-ReSkate2.1.3-Install.zip** from [Releases](https://github.com/Jish2637/JishsVirtualTV/releases). The source ZIP is for developers.

- Windows x64 and the supported Skate game build with **ReSkate 2.1.3**. This TV build checks the ReSkate DLL identity; other versions are not supported by this release.
- The expected ReSkate.dll SHA-256 is `139e932ad6328b898b2ff8c2aac3e0cd5ced6545c2f041aa8534239f97e59f9b`.
- [64-bit VLC](https://www.videolan.org/vlc/) installed in its normal location, or `VLC_HOME` set to its installation folder. VLC is not included.
- Internet access for online video. The package includes the matching yt-dlp and Deno resolver tools. Online services can change and make links stop resolving.

## Install

1. Close the game and back up your existing TV files and root `dinput8.dll`.
2. Open the install ZIP and copy the contents of its `JishsVirtualTV-SelectiveSync5-ReSkate2.1.3-Install` folder beside `Skate.exe`, merging `Scripts` and `Mods`.
3. The included `dinput8.dll` is the same multi-mod loader supplied with [Jish's First Person Mod](https://github.com/Jish2637/JishsFirstPersonMod/releases/tag/1.36.7-reskate-2.1.3). Install it if you do not have a compatible multi-mod loader. If that loader is already installed, keep it. For an unknown or single-mod loader, back it up outside the game folder before replacing it; do not overwrite a custom proxy arrangement blindly.
4. Enable **JishsVirtualTVBridge** in ReSkate's Mod Manager and approve its plugin permissions when prompted. Keep only one copy of `JishsVirtualTV.dll` in `Scripts/Custom`.
5. Launch the game, enter a level and press **F8**, or open **Insert → Mods → Virtual TV**.

The loader discovers DLLs in `Scripts/Custom` and preserves its existing `dinput8_mods.txt` order and disabled entries. If TV is listed as `# JishsVirtualTV.dll`, remove that leading `#` to enable it. No mod list is bundled, so installation does not replace yours. The First Person Mod itself is not required or included.

Keep personal `.screen`, `.objects`, `.attachments` and `.presets` files when updating. The package does not include personal settings or saved URLs. Restart the game after installing; do not hot-unload the TV DLL.

## Play a video

Enter a supported video or direct-media URL and press **Play**. Choose a nearby placed object and attach a screen. Adjust its size, rotation and local offsets to fit the object, then save the alignment. Move the screen slightly outward from a surface if it intersects it. Close the menu to watch.

Use **Front preview** to check video playback and **Colour bars** to check drawing independently of video. Screens start hidden; select the appropriate display mode to show them.

YouTube playlists support a queue, track selection and repeat. The next track is prepared in the background where possible; decoding, buffering and sync preparation can still cause a pause between tracks.

## Screens and speakers

The mod supports up to 64 screens and 64 speakers with one shared playback stream. These are limits, not performance recommendations: start with a small setup.

Attach speakers to placed objects and enable directional audio to use spatial playback and distance controls. **New objects become speakers** temporarily assigns newly placed local objects as speakers; it starts off and resets when the session changes. When enabled it takes priority over screen auto-attachment for new placements.

## Watch together

Everyone needs this matching TV DLL and bridge. In **Multiplayer TV sync**, enable sync, **Allow** the player you want to watch, then select **Watch** for that player. Enabling sync alone does not accept anyone's setup.

The broadcaster chooses **Share screen / speaker setup**. Their video continues while viewers prepare and catch up. A deliberately paused host stays paused. The selected broadcaster controls playback, screen/speaker assignments, alignment and audio distances; your volume stays local. Missing or ambiguous object matches can leave outputs pending instead of placing them somewhere else.

Use **Retry sync** if joining fails. **Leave sync** restores your local assignments with playback stopped. Selection lasts only for the current session. Catch-up happens on joining and host timeline changes; there is no continuous drift correction, so independent stalls can leave viewers out of time.

## Occlusion and current limitations

- Streamline and ReShade-style DX12 scene-depth paths are available. Streamline falls back when usable depth is unavailable. If Streamline occlusion does not work, try enabling DLSS in the game settings; this is not guaranteed to help.
- The ReShade option uses the mod's native DX12 depth path; a separate ReShade installation is not part of this package.
- **DX12/ReShade camera-motion drift remains unresolved.** A screen can shift slightly, intersect an object or flicker at an edge. Occlusion accuracy depends on the available scene depth. AMD and NVIDIA can behave differently.
- Video playback and depth processing add overhead. Low frame rates, unsupported streams, unavailable links, sync failures and object-binding problems are possible.
- ReSkate and game updates may make this version incompatible. This preview is pinned to the exact 2.1.3 runtime above.

## Report a problem

Open an [issue](https://github.com/Jish2637/JishsVirtualTV/issues) with your GPU, game/ReSkate version, selected occlusion path, steps to reproduce and approximate time. Logs are `Scripts/Custom/JishsVirtualTV.log`, `logs/ReSkate.log` and, for loading problems, `Scripts/Custom/UniversalLoader/loader.log`. For multiplayer issues, include both players' details. Review logs for personal information before posting them publicly. Debug mode can help capture a reproduction.

## Source and building

Playback and camera behavior remain based on SelectiveSync5. This package rebuilds the TV DLL against audited ReSkate 2.1.3 contracts and tagged model headers. See [the compatibility audit](docs/MIGRATION-2.1.3.md). Automated checks do not replace live gameplay validation.

Use Visual Studio 2026 with Desktop development with C++, the Windows SDK and CMake. Run `./build.ps1` for the Release DLL and standard CTest suite. If packaging from a source checkout, run `python tools/fetch_resolver.py` to obtain the pinned resolver binaries first, then run `./package.ps1 -LoaderPath PATH_TO_DINPUT8_DLL` with the loader from the install download (or your loader build). The bundled loader's corresponding source and separate build instructions are in `vendor/multi-mod-loader`.

Licensed under [GNU GPL v3](LICENSE). See [NOTICE.md](NOTICE.md) and the bundled third-party licenses for attribution and dependency terms.

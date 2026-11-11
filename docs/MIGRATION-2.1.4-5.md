# ReSkate 2.1.4-5 compatibility audit

The installed ReSkate DLL matches the official v2.1.4-5 release SHA-256
`0820fa71c1cb716f4222eb9027130933aa9e3db4ce20b46ef555760be997dbde`.
The tag points to commit `83200814db92135c90e35166397b4d79fb70d7e1`.

| Host contract | 2.1.4-5 RVA |
| --- | --- |
| State pointer | 0x1e6bf80 |
| Status export | 0xfed180 |
| Render callback | 0x106bb10 |
| Mutex lock / unlock | 0x1517e44 / 0x1517e70 |
| ImGui context getter | 0x1305d20 |
| ImGui context global | 0x1f05888 |
| Allocator / free / user data | 0x1c0f240 / 0x1c0f248 / 0x1f05890 |

All active host RVAs and field offsets match 2.1.4-4. The binary audit verified
the five entry fingerprints, the image size, and that the render and status
functions share the same state getter and mutex lock and unlock functions.

The layout probe compiled against the new tag and checked the State fields,
including render mutex offset 0 and menu offset 29064. The mutex is 80 bytes,
Status is 16 bytes, and Model is 26248 bytes. The 26 active vendored headers
match the tag byte for byte, so the build reuses vendor/reskate2144.

The game executable and its picking, placement, camera, and client-realm
contracts passed the audit. See AUDIT-2.1.4-5.json for the results. To reproduce,
build tools/layout2145 with `-DUPSTREAM=TAG_SOURCE`, save the probe output as
layout.txt, and run:

```
python tools/audit_2145.py ReSkate.dll --upstream TAG_SOURCE --game Skate.exe --layout-report layout.txt
```

Validation: Release build succeeded and all 31 CTest tests passed. Installed TV
DLL SHA-256: `9646b28815a911a8b985029c55430b8ca0f26aad27fc364a938f73bb00cd53a3`. The previous installed DLL was backed up.
The user confirmed the build works in game. Two-client synchronization remains unverified.

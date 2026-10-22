# SystemMods

Native visual mod pack for **Minecraft Bedrock (Android)** on LeviLauncher / SystemClient.

Repo: https://github.com/hater12132-ai/SystemMods

Output: `SystemMods.levipack` (entry `libBedrockTools.so` — name kept for loader compatibility).

## Modules

- ArmorHUD
- Connected Glass
- Fog Color
- Fullbright
- Glint Color
- Hitbox
- NoFog
- Swing Modifier
- Third Person Nametag
- View Model
- Nick
- Time Changer / Weather Changer (Ambience)
- No Disconnect (always on, hidden)

PL module ids: `bedrocktools.<Name>` e.g. `bedrocktools.Fullbright`.

## Build

Needs xmake, Android NDK (arm64), Python 3.

```bash
xmake f -p android -a arm64-v8a -m release --ndk=/path/to/ndk -y
xmake -y
```

## License

GPL-3.0 (based on BedrockTools). Internal C++ namespace remains `bedrocktools`.

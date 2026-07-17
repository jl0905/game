# OpenWarband

A small Mount & Blade–style game in C++17 with [raylib](https://www.raylib.com/).
Cross-platform: Windows, Linux, macOS — one CMake build, raylib is fetched automatically.

## Gameplay

**Campaign map** (top-down):
- Move your warband with **WASD** or hold **LMB** toward the cursor.
- Red war parties roam the map and chase you if they think they can win.
- Walk into a **town** to recruit troops: press **1/2/3** (Recruit / Infantry / Veteran).
- Colliding with an enemy party starts a battle.

**Battle** (third-person 3D, you fight alongside your troops):
- **WASD** move, **mouse** look, **SPACE** jump
- **LMB** swing your sword (arc hit in front of you)
- **RMB** block (greatly reduces incoming damage, slows you down)
- Win by wiping out the enemy; if you fall, the battle is lost.
- Casualties and loot carry back to the campaign map.

## Building

Requires CMake ≥ 3.20 and a C++17 compiler. Internet access is needed on the
first configure (raylib is downloaded via FetchContent).

### Linux
```sh
sudo apt install build-essential cmake libx11-dev libxrandr-dev libxinerama-dev \
                 libxcursor-dev libxi-dev libgl1-mesa-dev   # Debian/Ubuntu
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/openwarband
```

### macOS
```sh
brew install cmake            # Xcode command line tools required
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/openwarband
```

### Windows
The `build.ps1` helper auto-detects your toolchain (Visual Studio **or** MinGW):
```powershell
./build.ps1
```
Or manually — with Visual Studio:
```bat
cmake -B build
cmake --build build --config Release
build\Release\openwarband.exe
```
With MSYS2/MinGW GCC (no Visual Studio):
```powershell
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/openwarband.exe
```

## Code layout

The game is **data-driven**: armour, weapons, troops, and factions are
definitions registered in `src/content.cpp` and referenced by handle. Adding
content means adding a registry entry — not editing game logic. See
[CLAUDE.md](./CLAUDE.md) for the full architecture and extension guide.

| File | What it does |
|---|---|
| `src/main.cpp` | Window setup, loads content, screen state machine |
| `src/types.h` | Core enums (slots, weapon classes, attack directions, behaviours) |
| `src/registry.h` | `Registry<T>` — id→handle lookup |
| `src/content.h/.cpp` | Content definitions + **the one place to add game content** |
| `src/world.h` | Runtime state: characters, parties, towns, `GameState` |
| `src/character.h/.cpp` | Humanoid renderer driven by a character's equipment loadout |
| `src/campaign.cpp` | Overworld map, faction party AI, towns/recruiting, economy |
| `src/battle.cpp` | 3D battle: 4-directional combat, soldier AI, casualties |

## Ideas to extend

- Wire `WeaponDef`/`ArmorDef` values into combat (currently flat placeholders)
- Directional *blocking* to match the 4-way attacks
- Horses, archers, formations
- Sieges, lords, fiefs; save/load; character skills

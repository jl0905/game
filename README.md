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

### Windows (Visual Studio)
```bat
cmake -B build
cmake --build build --config Release
build\Release\openwarband.exe
```
(Or open the folder directly in Visual Studio / CLion — CMake is auto-detected.)

## Code layout

| File | What it does |
|---|---|
| `src/main.cpp` | Window setup + screen state machine |
| `src/game.h` | Shared state: parties, troop types, towns |
| `src/campaign.cpp` | Overworld map, party AI, towns/recruiting, economy |
| `src/battle.cpp` | 3D battle: player combat, soldier AI, win/lose, casualties |

## Ideas to extend

- Directional attacks/blocks (up/left/right swings like M&B)
- Horses, archers, formations (hold F1 for shield wall…)
- Sieges, lords, factions, fiefs
- Save/load, character skills and equipment

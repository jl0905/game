# CLAUDE.md — OpenWarband

Project context for AI agents (and humans). This is the canonical context file;
`AGENTS.md` points here.

## What this is

A small Mount & Blade–style game in **C++17** using **[raylib](https://www.raylib.com/) 5.5**
(fetched automatically by CMake). Two connected modes:

- **Campaign** — a top-down overworld. Your party roams, other factions' parties
  roam by their own behaviour, towns let you recruit. Colliding with a party
  starts a battle.
- **Battle** — a real-time third-person 3D fight. You fight alongside your troops
  with a four-directional (Mount & Blade style) melee system. Casualties and the
  outcome carry back to the campaign.

## Design principles (read before changing anything)

1. **Data-driven, not hard-coded.** Armour, weapons, troops, and factions are
   *definitions* registered in one place (`src/content.cpp`) and referenced
   everywhere by an integer **handle**. Adding content = adding a registry entry;
   no logic changes elsewhere.
2. **Structure vs. balance are separate.** The *shape* of the data (what slots
   exist, what fields a weapon has) lives in `src/content.h`. The *numbers* are
   deliberately **flat placeholders** (`base::*` in `src/content.cpp`, all marked
   `TODO(balance)`). Do **not** bake in a tuned progression — balancing is a
   later, human, playtesting-driven pass. When in doubt, add the field, leave the
   value flat.
3. **Rendering is derived from data.** `src/character.cpp` draws any humanoid
   purely from its `Loadout`, so equipping different gear changes the model with
   no rendering code changes.
4. **Keep it clean.** Small focused files, `namespace {}` for file-local helpers,
   `const`-correct, no magic numbers without a named constant, no dead code.

## Code layout

| File | Responsibility |
|---|---|
| `src/main.cpp` | Window setup, loads content, screen state machine |
| `src/types.h` | Core enums: `Screen`, `EquipSlot`, `WeaponClass`, `AttackDir`, `PartyBehavior`, `Team` |
| `src/registry.h` | `Registry<T>` — append-only, id→handle lookup |
| `src/content.h` | Definition structs: `ArmorDef`, `WeaponDef`, `TroopDef`, `FactionDef`, `Loadout`, `Content` |
| `src/content.cpp` | `LoadDefaultContent()` — **the single place to add game content** |
| `src/world.h` | Runtime state: `Character`, `Party`, `Town`, `GameState` |
| `src/game.h` | Umbrella include + subsystem entry points |
| `src/character.h/.cpp` | Segmented humanoid renderer driven by a `Loadout` |
| `src/campaign.cpp` | Overworld map, faction party AI, towns/recruiting, economy |
| `src/battle.cpp` | Real-time 3D battle, 4-directional combat, soldier AI, casualties |

Data flow: `Content` (static catalogue) is loaded once; `GameState` (mutable
runtime) references it by handle. Subsystems both update and draw within a frame.

## How to extend (common tasks)

All of these are edits to `src/content.cpp` only, unless noted:

- **New armour piece** → `c.armor.add(Armor("id", "Name", EquipSlot::Head, tint))`.
  A new *slot* (e.g. `Legs`) also needs an `EquipSlot` enum value in `types.h`
  and a segment in `character.cpp`.
- **New weapon** → `c.weapons.add(Weapon("id", "Name", WeaponClass::Polearm, tint))`.
  A genuinely new *class* also needs a case in `character.cpp`'s weapon switch.
- **New troop** → build a `TroopDef`, set its `Loadout` slots, `c.troops.add(...)`,
  and add the handle to the factions that field it.
- **New faction / party type** → build a `FactionDef` with a `color`, a
  `PartyBehavior`, and a `roster`, then `c.factions.add(...)`. It will roam and
  fight automatically.
- **Armour/weapon stats (balance)** → replace the flat `base::*` constants (or set
  per-def values) in `content.cpp`. This is the intended balancing surface.

## Combat model

- Four attack directions (`AttackDir`). In battle the player's swing direction is
  chosen from recent mouse motion (`DirFromMotion`): flick up = overhead, down =
  thrust, left/right = side cuts. RMB blocks.
- Damage/reach/cooldown currently use placeholder constants in `battle.cpp`
  (`HIT_DAMAGE`, `ATTACK_COOLDOWN`) and the weapon's `reach`. These are the next
  things to route through `WeaponDef`/`ArmorDef` once balancing begins.

## Build & run

MSYS2/MinGW GCC or MSVC, CMake ≥ 3.20. First configure needs internet (raylib).

```powershell
./build.ps1                 # auto-detects MinGW vs MSVC, configures, builds, runs
./build/openwarband.exe     # run directly (MinGW single-config output path)
```

`build.ps1` always wipes `build/` first to avoid stale-cache issues (e.g. a
CMakeCache left over from WSL). The `build/` directory is a build artifact.

## Conventions

- C++17, raylib + raymath. Prefer raymath vector helpers over hand-rolled math.
- File-local helpers go in an anonymous `namespace {}`; nothing leaks that
  doesn't need to.
- Handles are `int`; `-1` means "none/empty". Check `Registry::valid()` before use.
- Placeholder/unbalanced numbers must be labelled `TODO(balance)`.
- Verify changes by actually building **and** launching the game, not just
  compiling.

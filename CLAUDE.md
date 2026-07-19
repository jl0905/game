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

## Code layout & module boundaries

The tree is split so independent agents can work in parallel without touching
each other's files:

**Shared core (`src/`)** — stable contracts; change sparingly and deliberately:

| File | Responsibility |
|---|---|
| `src/main.cpp` | Window setup, screen state machine — **the only place campaign and battle meet** |
| `src/types.h` | Core enums: `Screen`, `EquipSlot`, `WeaponClass`, `AttackDir`, `PartyBehavior`, `Team` |
| `src/registry.h` | `Registry<T>` — append-only, id→handle lookup |
| `src/content.h/.cpp` | Definition structs + `LoadDefaultContent()` — **the single place to add game content** |
| `src/world.h` | World-map runtime state: `Character`, `Party`, `Town`, `GameState` |
| `src/input.h` | `CampaignInput` / `BattleInput` — per-frame player intent, decoupled from devices |
| `src/harness.h/.cpp` | Headless script harness (`--script`) — see "Programmatic play" below |
| `src/ui.h/.cpp` | Text rendering. Draw all text via `ui::Text`/`ui::Title` (not raylib `DrawText`) so it uses the smooth TTF fonts. Fonts are mod-configurable in `assets/fonts.cfg`; falls back to the built-in font if a font fails to load. |

**Campaign module (`src/campaign/`)** — owns the overworld: map, party AI,
towns/recruiting, economy, and *applying* battle outcomes to the world.
Hostile factions' parties hunt each other (`AreFactionsHostile`), and two that
collide lock into a **skirmish** that auto-resolves over a few seconds — the
player can watch it play out or press `[1]`/`[2]` to join a side, turning it
into a full battle (the backed party fights as your ally). World time is
**passively paused**: it only advances while the player travels or holds SPACE
to wait, so nothing moves until the player acts.

**Town module (`src/town/`)** — the walkable 3D settlement scene (roadmap B2):
seeded buildings + plaza, villager NPCs, tavern recruiting. Movement reuses
`BattleInput`; menu intents come via `CampaignInput`. Includes the battle
module's `character.h` renderer (read-only reuse).

**Battle module (`src/battle/`)** — owns everything inside a fight: 3D combat,
4-directional melee, soldier AI, battle camera/HUD, the humanoid renderer
(`character.h/.cpp`), and terrain. Terrain lives inside `battle.cpp`: a simple
deterministic heightfield (hills/mountains, an optional carved river, scattered
trees) whose single `HeightAt()` is the source of truth for both rendering and
sitting units on the ground. It is generated from `BattleSetup::campaignPos`
(`TerrainConfigFromWorld`) so the battlefield reflects where you are on the map.

### The battle handoff (keep this narrow)

Battle is a special state entered when two parties engage. It is deliberately
isolated: `src/battle/` **never includes `world.h`** and knows nothing about
parties, towns, or gold. The full contract lives in `src/battle/battle.h`:

- `BattleSetup` — a snapshot taken from the world map at engagement time
  (troop counts for both sides, an optional allied party's troops when the
  player joins someone else's fight, the hero's loadout and max HP).
- `BattleOutcome` — what the battle reports back (won/lost, per-troop player
  losses, and per-troop allied losses when an ally fought).

Flow: campaign detects a collision and sets `Screen::Battle` → `main.cpp`
builds a `BattleSetup` from `GameState` and calls `BattleInit` → battle runs
frame-by-frame until `BattleUpdateDraw` returns `false` with an outcome →
`main.cpp` writes the outcome into `GameState` → campaign applies it
(casualties, loot, party removal).

If the battle needs new information (e.g. hero skills, terrain), extend
`BattleSetup`/`BattleOutcome` — do not reach into campaign state. Both sides'
data (party composition, player health, etc.) always originates from the
global world map, which remains the source of truth.

Data flow: `Content` (static catalogue) is loaded once; `GameState` (mutable
runtime) references it by handle.

### Gather / Update / Draw (keep this separation)

Every screen is split three ways, and **simulation code must never read the
devices or draw**:

- `Gather*Input()` — raylib devices → `CampaignInput` / `BattleInput` (windowed only)
- `*Update()` — pure simulation, driven only by the input struct
- `*Draw()` — rendering only (windowed only)

This is what makes the game playable headless. New player-facing actions get a
field in the input struct, a branch in Gather, and handling in Update — never a
raw `IsKeyPressed` inside Update.

## Programmatic play (use this to verify gameplay changes)

`openwarband.exe --script file.txt` runs the full simulation **without a
window**, at a fixed 60 Hz step, driven by commands and printing state on
demand. This is the primary way for agents to actually *play* the game:

```
seed 42            # deterministic run
walk -1 0.05 2     # travel on the map (direction dx dy, seconds) — time flows
hunt 60            # auto-steer to the nearest hostile party until battle starts
wait 30            # stand still and let world time pass
state              # dump: screen, gold, party, all parties, skirmishes, battle view
enter 0 / leave    # settlements; recruit SLOT N
join 1|2           # join the nearest skirmish on a side
bmove F R T        # battle: move (forward, strafe-right) for T seconds
look DX DY         # battle: turn camera (mouse-delta pixels)
attack up|down|left|right   # ready, aim, release one swing
block on|off  swap  menu  formation 1-4  ranks +|-
```

The exe is a GUI app in Release, so capture output via
`cmd /c "openwarband.exe --script s.txt > out.txt 2>&1"`.

`openwarband.exe --bench N` runs a windowed N-vs-N synthetic battle at
uncapped FPS and writes avg/p99 frame times to `bench.txt` — use it to verify
any rendering or battle-scale change. Colliding with a
hostile party mid-`walk`/`wait` enters the battle screen; battles run entirely
on soldier AI if you issue no battle commands (the idle hero will eventually be
killed — block or move). See `src/harness.cpp` for the command reference.

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
  fight automatically. Declare who it fights in the relations block at the end
  of `LoadDefaultContent()` (`war(a, b)` fills `Content::hostile`); factions
  not marked are at peace and ignore each other.
- **Armour/weapon stats (balance)** → replace the flat `base::*` constants (or set
  per-def values) in `content.cpp`. This is the intended balancing surface.

## Combat model

- **Directional melee (`AttackDir`).** The player **holds** LMB to ready a swing,
  aiming it by moving the mouse (`DirFromMotion`: up = overhead, down = thrust,
  left/right = cuts), then **releases** to strike. `Pose.windup`/`Pose.swing`
  drive the wind-up-then-follow-through animation; `character.cpp` also draws a
  motion trail and a crossguard. RMB blocks (and cancels a wind-up).
- **Multiple weapons.** A `Loadout` carries an arsenal (`weapons`) with the
  active one mirrored into the `Weapon` slot. The hero swaps with **Q**; troop
  AI auto-selects per range (`PickWeaponForRange` — spear at range, sword up
  close). Add weapons to a unit with `loadout.addWeapon(handle)` in `content.cpp`.
- **Formations (`~` strategy menu).** The player's own troops obey Charge / Line
  / Square / Spread with an adjustable rank count; `FormationTarget` computes the
  slot layout. Allies and enemies always charge. Add a shape by extending
  `FormationType` + `FormationTarget`.
- **Multithreaded AI.** Soldier AI is a parallel read phase (`ComputeAI` over a
  read-only snapshot via `ThreadPool` in `src/parallel.h`) followed by a serial
  apply phase — no locks in the hot path, no data races. The pool is a shared
  singleton (`ThreadPool::Global()`); CMake links `Threads::Threads`.
- **Ranged combat.** A `WeaponClass::Ranged` weapon with `missileRange`/
  `missileSpeed` set (see the Short Bow) makes its wielder an archer: the AI
  advances to ~90% of missile range, stands, and looses ballistic arrows
  (`Arrow` sim in `battle.cpp`; gravity-compensated aim, per-hit armour soak,
  no friendly fire, terrain stops shafts). Blocking soaks arrows at
  `BLOCK_MISSILE_FACTOR`. Archers carry a sidearm and switch automatically
  when cornered (`PickWeaponForRange` treats missile range as reach).
- **Combat stats are data-routed.** Damage, reach and cooldown come from the
  wielded `WeaponDef` (`WeaponDamage`/`WeaponReach`/`WeaponCooldown` in
  `battle.cpp`; bare-hand `FIST_*` fallbacks); worn armour soaks per hit via
  `LoadoutArmor` + `ApplyArmor`. Values are still flat placeholders
  (TODO(balance)) — tune them in `content.cpp`, not in battle code.

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
- Verify changes by actually building **and** playing the game — use the
  `--script` harness (see "Programmatic play") to drive real gameplay and
  assert on `state` output, not just compiling.
- Runtime assets live in `assets/` and are copied next to the executable by
  CMake post-build; the game locates them via `GetApplicationDirectory()`, so it
  runs from any working directory. Draw text through `ui::Text`/`ui::Title`.

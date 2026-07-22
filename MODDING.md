# Modding OpenWarband

One door for modders. Two tiers: **config files** (no rebuild — reshape the
world with a text editor) and **content registration** (one C++ file, then
rebuild). Numbers everywhere are flat placeholders awaiting a balance pass —
see `BALANCE.md` before tuning.

## Config files (`assets/`, no rebuild)

The game looks for these beside the executable first, then in the working
directory (`assets/…`), then `../assets/`. Headless (`--script`) runs use the
working directory, windowed runs the copy beside the exe.

### `assets/map.cfg` — the world

Documented in the file itself. One line per fact:

| Line | Shape | What it does |
|---|---|---|
| `size N` | world units, square | map bounds |
| `start X Y` | position | player start |
| `parties N` | count | roaming faction parties at world start |
| `town Name type X Y owner` | type: `village|town|castle` | a settlement; the first `town` line replaces the built-in list; owner is a faction id, `player`, or `none` |
| `lair faction X Y` | | a bandit den that breeds parties until stormed |
| `biome …` | 8 floats | noise frequencies, forest/mountain thresholds, march-speed factors — drives the map paint, travel speed AND battlefield generation |
| `road linkDist width` | | towns closer than `linkDist` are joined; within `width` of a link parties march at full pace |
| `storm radius driftX driftY [startX startY]` | | the drifting weather cell (V62/V68): reach, per-dawn drift, and optional spawn point — inside it travel runs ×0.75 and battles fight in the rain |

### assets/goods.cfg & assets/enterprises.cfg (V95)

Wares: `good <id> <Name_with_underscores> <basePrice> <raw 0|1>` — raw goods are village produce, the rest town craftwork; new wares join shelves, caravans, quests and events automatically. Works: `enterprise <id> <Name_with_underscores> <cost> <daily-income> <makes-good-id|none>` — joins the one-per-town rotation, and a works that makes a good sells it cheap and restocks it each dawn.

### assets/events.cfg (V90)

World events, one per line, joining the day rotation beside the built-in three: `event <id> <Name_with_underscores> <News_with_underscores_%s> <prosperityDelta> <stockDelta> <spawnParties> <poolDelta>`. `%s` in the news becomes the struck settlement's name.

### assets/armor.cfg (V99)

Armour pieces, one per line, loading before troops.cfg: `armor <id> <Name_with_underscores> <slot> <soak>` with slots `head body hands feet`. Soak follows the V15 identity tiers (cloth 1 … plate 6); pieces drop as loot, wear in the paper-doll, and soak through `ApplyArmor` like any built-in.

### assets/weapons.cfg (V98)

Arms, one per line, loading before troops.cfg so modded troops can wield modded steel: `weapon <id> <Name_with_underscores> <class> <damage> <reach> [missileRange missileSpeed]`. Classes: `onehanded twohanded polearm axe ranged` (ranged takes the two extra numbers); reach 0 keeps the class default.

### assets/troops.cfg (V85)

Mint soldier types and enlist them, no code. Three line kinds: `troop <id> <Name_with_underscores> <weapon-id> <armor-id> <wage> <cost> [mounted]` builds a type (standard cap/boots plus your body armour and weapon, sword sidearm); `recruit <faction-id> <troop-id>` appends it to a faction's levy so its towns recruit it and its lords field it; `upgrade <from-id> <to-id> [xp]` (V103) wires a promotion ladder — the party screen's paid promote applies unchanged. New types always append — existing handles and saves stay valid.

### assets/companions.cfg (V83)

One hero-for-hire per line: `companion <id> <Name_with_underscores> <perk> <temper> <cost>`. Perks reuse the built-in hook strings (`surgeon`, `scout`, `quartermaster`, `drillmaster`, `jailer`, or `none`); tempers are `honorable`/`grim`. Modded companions join the tavern rotation beside the built-in five with a standard kit (cap, tunic, boots, sword and spear).
| `lordnames A B C…` | one-token names | the pool for player-raised lords; pool size = the raised-lord cap |

### `assets/settings.cfg` — presentation

Window size, fullscreen, LOD draw distance, particles, master volume,
invert-Y. Self-documenting header; rewritten losslessly by the in-game
settings screen (O on the title or map). Simulation never reads it, so
headless runs are settings-independent.

### `assets/fonts.cfg` — typefaces

Points `ui::Text`/`ui::Title` at TTF files; falls back to raylib's built-in
font if a file fails to load.

## Content registration (`src/content.cpp`, rebuild)

Everything in the game is a *definition* registered in `LoadDefaultContent()`
and referenced by integer handle. Adding content is adding a registry entry —
no other code changes:

- **Armour**: `c.armor.add(Armor("id", "Name", EquipSlot::Head, tint))`. A new
  *slot* also needs an `EquipSlot` value (`types.h`) and a segment in
  `character.cpp`.
- **Weapon**: `c.weapons.add(Weapon("id", "Name", WeaponClass::Polearm, tint))`.
  Set `missileRange`/`missileSpeed` to make it ranged (see the Short Bow).
  A new *class* needs a case in `character.cpp`'s weapon switch.
- **Troop**: build a `TroopDef`, set its `Loadout` (armour slots +
  `addWeapon(...)` arsenal), `c.troops.add(...)`, add the handle to faction
  rosters. `upgradesTo` chains tiers; `companion = true` makes a tavern hero.
- **Faction**: `FactionDef` with `color`, `behavior`
  (`Aggressive|Patrol|Passive`), `roster`, optional `lords` + `lordPartySize`
  and `kingdom = true`. Declare wars in the relations block at the end
  (`war(a, b)`); unmarked pairs are at peace.
- **Goods / enterprises / quests / events**: one-line `c.goods.add`,
  `c.enterprises.add`, `c.quests.add`, `c.events.add` entries — markets,
  businesses, the job board, and the world-news rotation pick them up
  automatically. An `EventDef` carries a news line (`%s` = the settlement),
  prosperity/stock deltas, and how many outlaw bands it raises.

## Testing your mod

`openwarband.exe --script file.txt` plays the full simulation headless at
60 Hz — see the command reference in `src/harness.cpp` and the scenario
library in `tests/` (49 scripts, one per system). `state` dumps everything;
`openwarband.exe --bench N` measures an N-vs-N battle. A capture pattern:

```
seed 42
walk 1 0 5
state
```

Run every script in `tests/` after a content change — a mod that breaks a
scenario usually broke a system.

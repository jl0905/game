# ROADMAP

Feature backlog for the ongoing autonomous-improvement effort. Ordered by
dependency, not priority — pick the earliest unblocked task in a track.
Per project rules: build **structure**, keep numbers flat `TODO(balance)` —
no tuned progressions, costs, damage curves, or stat weights yet.

## Track A — Engine & rendering

- [x] **A1. GPU rendering / acceleration.** Terrain baked into a one-time GPU
  mesh (`Terrain::BakeModel`); soldiers beyond 45 u draw as a two-box LOD
  silhouette. `--bench N` mode measures it: 300 soldiers 35→68 FPS,
  600 18→41, 1000 11→41. Further headroom (true instancing) only if C3
  demands it.
- [x] **A2. Better troop models (pass 1).** Dome helmets with brims + nasal
  bars, team surcoat stripe on the chest, troop-accent plumes (rank identity),
  shields carried on the arm for sword-and-board troops and raised with a boss
  when blocking. ~15% frame cost at 600 soldiers (35 FPS, still 2× pre-A1).
- [x] **A3. Clearer swing readability (pass 1).** Wind-up telegraph: a ghosted
  preview of the full upcoming arc while readying (direction is readable
  before it lands); hit-flash flare on any soldier or the hero taking damage
  (melee and arrows). Existing per-direction arcs + motion trails retained.
  Remaining: blocked-clang feedback, torso lean during wind-up.

## Track B — Settlements

- [x] **B1. Faction ownership of settlements.** `Town::owner` faction handle;
  map rings/labels in owner colour; hostile settlements refuse entry
  ("bars its gates"); ownership saved/loaded. Start state: Sargoth yours,
  Praven+Jelkala patrol, Tulga held by deserters (siege bait for B3).
- [x] **B2. Walkable settlements (v1).** `src/town/`: entering a settlement is
  a 3D walk — seeded buildings ring a plaza (count/materials by settlement
  type), gold-roofed tavern where recruiting happens, wandering villager NPCs,
  building collision, third-person camera reusing BattleInput. Harness drives
  it with `bmove`/`recruit`; `state` shows hero/tavern positions.
  Remaining: interiors, castle keep layout, NPC dialogue.
- [x] **B3. Sieging system (v1).** Settlements keep a garrison drawn from the
  owner's roster (sized by settlement type, TODO(balance)); clicking a hostile
  settlement storms it — the garrison fights on home terrain, victory
  transfers ownership (villages "sacked", castles/towns "taken"), garrison
  casualties and captures persist in saves. Undefended settlements change
  hands without a fight.
  - [x] B3b. Siege battlefields (v1): town/castle assaults have a stone wall
    with one gate (village raids stay open) — movement blocked outside the
    gateway, AI funnels through the gate mouth, low arrows stop on the wall,
    crenellated rendering. Remaining: defenders stationed ON the walls,
    ladders/rams as alternate entries.
  - [ ] B3c. Garrison replenishment by the owner faction (ties into C3 lords).

## Track C — Campaign systems

- [x] **C1. Save / load.** `src/save.h/.cpp`: line-based text save keyed by
  content id strings (survives content additions); F5/F9 quicksave/quickload
  on the map; harness `save`/`load` commands. Battles are not saved.
- [x] **C2. Troop veterancy / upgrade paths.** `TroopDef::upgradesTo` +
  `upgradeXp` (flat); survivors of won battles earn per-type XP pools
  (`GameState::troopXp`); upgrades spend XP to promote units. Persisted in
  saves.
- [x] **C3. Lord parties.** Factions field named lords (`FactionDef::lords`,
  `lordPartySize` flat 120): they muster at owned settlements, march on hostile
  settlements they outmatch, invest them (`AISiege` auto-resolve → ownership
  flips, garrison installed from the attacker's host), fight field battles,
  respawn at home after falling, and persist in saves. Soak-verified: the
  settlement war churns on its own.
  - [x] C3b. Lord awareness: lords ignore parties "beneath their notice"
    (LORD_NOTICE_RATIO, TODO(balance)) so the early game survives them; a
    banner alert fires when YOUR settlement is invested; riding into a siege
    camp breaks the siege and starts the battle. Remaining: lords still
    dogpile the same targets (pacing → balance pass).

## Track D — Character & party UI

- [x] **D1. Tiled inventory (Diablo/PoE style).** 10×6 grid (`Screen::Inventory`,
  I key): items are content handles with identity tile footprints (greatsword
  2×4, boots 2×2, …), LMB pick/place, E/RMB equips with the old piece swapped
  back into the bag; battle victories drop loot; persists in saves; harness
  `inv`/`equip` commands. Remaining: hover tooltips with stats, drop-to-
  discard, item rotation.
- [x] **D2. Party management screen.** `Screen::Party` (P): roster rows with
  counts + XP, promote with number keys, dismiss with Shift+number, and a
  daily ledger preview (income vs wages, red when running a deficit).
  Harness `party`/`upgrade`/`dismiss`. Remaining polish: per-troop loadout
  preview via `DrawCharacter`, mouse support.
- [x] **D3. Skill system (structure).** `AttributeDef` registry (str/agi/int/cha
  with documented hook strings); hero levels from battle XP (flat thresholds)
  and spends points on the character sheet (`Screen::Character`, C key).
  Effects are deliberately no-op until the balance pass. Persisted in saves;
  harness `char`/`spend` commands.

## Done (this effort)

- [x] Economy: world days (`DAY_LENGTH` sim seconds), daily settlement income
  by type, per-troop wages (`TroopDef::wage`), and unpaid desertion — all flat
  TODO(balance) rates. Day shown in the HUD and saves.

- [x] Gather/Update/Draw split + headless `--script` play harness (`hunt`, `state`, …)
- [x] A/D strafe inversion fix; AI parties clamped to map bounds
- [x] Weapon/armour-routed combat stats (flat values, `TODO(balance)`)
- [x] Ranged combat: archers, ballistic arrows, keep-distance AI
- [x] Post-battle casualty report (both sides, by troop type)
- [x] Data-driven faction relations matrix (lawful vs outlaw factions)

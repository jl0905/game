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
- [ ] **A2. Better troop models.** Replace bare cube-segments with nicer
  proportioned humanoids (still procedural from `Loadout` — no art pipeline):
  tapered limbs, heads with helmets that read at distance, shields on the arm,
  faction-tinted surcoats. Keep `character.cpp` the single rendering surface.
- [ ] **A3. Clearer swing readability.** Make attack direction obvious at a
  glance: distinct wind-up silhouettes per `AttackDir`, stronger motion trail
  along the actual arc, brief hit-flash on the victim, blocked-clang feedback.
  Applies to both hero and AI soldiers.

## Track B — Settlements

- [x] **B1. Faction ownership of settlements.** `Town::owner` faction handle;
  map rings/labels in owner colour; hostile settlements refuse entry
  ("bars its gates"); ownership saved/loaded. Start state: Sargoth yours,
  Praven+Jelkala patrol, Tulga held by deserters (siege bait for B3).
- [ ] **B2. Walkable settlements.** Entering a settlement drops you into a 3D
  scene (reuse the battle renderer/terrain tech): buildings, streets, wandering
  NPC villagers (procedural characters), talk/recruit at the tavern instead of
  a flat menu. Settlement layout seeded from the town like battle terrain is.
- [x] **B3. Sieging system (v1).** Settlements keep a garrison drawn from the
  owner's roster (sized by settlement type, TODO(balance)); clicking a hostile
  settlement storms it — the garrison fights on home terrain, victory
  transfers ownership (villages "sacked", castles/towns "taken"), garrison
  casualties and captures persist in saves. Undefended settlements change
  hands without a fight.
  - [ ] B3b. Siege battlefields: walls + gate for castles/towns, defender
    formations on the walls, attacker spawns outside.
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
  - [ ] C3b. Lord pacing/awareness: lords currently dogpile the same targets
    and the map churns fast — revisit with balance pass; player-side
    notifications when YOUR settlements come under siege.

## Track D — Character & party UI

- [ ] **D1. Tiled inventory (Diablo/PoE style).** Grid inventory for the hero:
  items occupy w×h tiles, drag/rearrange, equip to `Loadout` slots. Items are
  the existing `ArmorDef`/`WeaponDef` handles plus loot drops after battles.
  Input goes through the intent structs so the harness can drive it.
- [ ] **D2. Party management screen.** v1 exists (`Screen::Party`, opened with
  P): roster rows with counts + XP, number keys promote units, harness
  `party`/`upgrade` commands. Remaining: dismiss troops, per-troop detail
  (loadout preview via `DrawCharacter`), mouse support.
- [ ] **D3. Skill system.** Hero attributes (strength, agility, intelligence,
  charisma …) as data-driven definitions in `content.cpp` — structure and
  hooks only (what each *could* modify), flat/no-op effects until balancing.
  Shown on a character sheet; groundwork for level-ups.

## Done (this effort)

- [x] Gather/Update/Draw split + headless `--script` play harness (`hunt`, `state`, …)
- [x] A/D strafe inversion fix; AI parties clamped to map bounds
- [x] Weapon/armour-routed combat stats (flat values, `TODO(balance)`)
- [x] Ranged combat: archers, ballistic arrows, keep-distance AI
- [x] Post-battle casualty report (both sides, by troop type)
- [x] Data-driven faction relations matrix (lawful vs outlaw factions)

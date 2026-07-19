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

- [ ] **B1. Faction ownership of settlements.** `Town` gains an owning faction
  (data-driven; ownership changes at runtime). Map icons/labels tint by owner;
  recruiting only in friendly settlements; hostile settlements refuse entry
  (until B3/sieges). This underpins sieges and lords.
- [ ] **B2. Walkable settlements.** Entering a settlement drops you into a 3D
  scene (reuse the battle renderer/terrain tech): buildings, streets, wandering
  NPC villagers (procedural characters), talk/recruit at the tavern instead of
  a flat menu. Settlement layout seeded from the town like battle terrain is.
- [ ] **B3. Sieging system.** Attack a hostile village/castle/town: a battle on
  a settlement-flavoured battlefield (walls/gate for castles & towns, open
  raid for villages). Winning transfers ownership (B1) or sacks the village.
  Defenders drawn from a garrison the owner faction maintains.

## Track C — Campaign systems

- [x] **C1. Save / load.** `src/save.h/.cpp`: line-based text save keyed by
  content id strings (survives content additions); F5/F9 quicksave/quickload
  on the map; harness `save`/`load` commands. Battles are not saved.
- [ ] **C2. Troop veterancy / upgrade paths.** `TroopDef::upgradesTo` chain
  (recruit → infantry → veteran); survivors accrue experience toward upgrades
  (structure only — thresholds flat). Feeds the D2 management screen.
- [ ] **C3. Lord parties.** Each faction fields a few named lords with large
  parties (order-of-hundreds troops — exact sizes TODO(balance)); they besiege
  and defend settlements (B1/B3), fight field battles, and respawn at owned
  towns. Requires A1 so battles at that scale hold frame rate; harness soak to
  verify the map economy doesn't collapse.

## Track D — Character & party UI

- [ ] **D1. Tiled inventory (Diablo/PoE style).** Grid inventory for the hero:
  items occupy w×h tiles, drag/rearrange, equip to `Loadout` slots. Items are
  the existing `ArmorDef`/`WeaponDef` handles plus loot drops after battles.
  Input goes through the intent structs so the harness can drive it.
- [ ] **D2. Party management screen.** Roster view: troop counts, types,
  upgrade buttons (consumes C2 experience), dismiss troops; opened from the
  campaign map. Same gather/update/draw split as every other screen.
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

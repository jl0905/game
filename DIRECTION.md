# DIRECTION.md — Creative Direction

This file is the **creative direction** for OpenWarband, maintained by the
directing agent. Any agent working under the autonomous-improvement goal must:

1. **Read this file first** and pick the highest-priority unblocked item below.
2. **Mark progress here** — check items off, add a one-line "shipped" note, and
   append newly discovered follow-ups to the relevant track. Commit this file
   with the feature.
3. **Keep the rules**: data-driven content, flat `TODO(balance)` numbers, verify
   by playing via the `--script` harness, add a `tests/` script per system,
   commit+push at every stopping point (author per memory: GitHub-noreply
   identity).
4. If an item proves wrong or a better idea appears, **amend this file** and say
   why in the commit message — direction is versioned, not sacred.

## Vision

Feature parity with **Mount & Blade: Warband** in spirit: a living sandbox
kingdom war where the player rises from a nobody with 5 recruits to a
kingmaker — through trade, tournaments, vassalage, marriage-politics, and
ultimately their own crown. Every system should feed another (economy feeds
armies, armies feed politics, politics feeds wars). Structure first, numbers
flat.

## Track E — Economy & trade (Warband's backbone)

- [x] **E1. Marketplace.** Shipped: `GoodDef` registry (6 wares), per-town
  `stock`/`priceOffset`, `Screen::Market` opened with M inside a settlement,
  sell pays 3/4 of buy (flat TODO(balance)), daily +1 restock, saved/loaded,
  harness `market`/`buy`/`sell` + `tests/market.txt`. Follow-ups: an on-screen
  [M] hint in the town HUD; a market stall building as the trigger.
- [x] **E2. Trade loop.** Shipped: goods carry a raw/craftwork identity;
  villages price their produce at 70% and craftwork at 130% (towns mirror,
  castles flat with thin stock), so buy-low/sell-high caravan runs pay.
  Saddlebag capacity caps hauls at 30 units (GOODS_CAP). All numbers flat
  TODO(balance); `tests/trade_loop.txt` proves a profitable run. Deviation
  from the original note: goods stayed stackable counts rather than moving
  into the D1 tile grid — stacks fit wares better than footprints.
- [x] **E3. Caravans & prosperity.** Shipped: any faction holding two
  settlements keeps a lightly-guarded convoy on the road between them;
  each arrival adds +5 prosperity (cap 150), prosperity scales settlement
  income, beaten caravans spill up to 6 wares into the saddlebags, and both
  survive saves. Flat numbers TODO(balance) — the +5/arrival fills the cap
  in days; a real rate wants the balance pass. Follow-up: relation penalty
  for raiding waits on F1 relations.
- [x] **E4. Landowning / enterprises.** Shipped: `EnterpriseDef` registry
  (mill, smithy, dyeworks), one per town, bought with B at the market;
  daily income scales with prosperity; seized (with news) when the town's
  owner is at war with you; persists in saves; harness `enterprise` command
  + `tests/enterprise.txt`. Flat numbers TODO(balance). Track E complete.

## Track F — Politics & kingdom play

- [x] **F1. Relations (v1).** Shipped: per-faction standing with the player
  (−100..100) moved by deeds — beating a party −5, raiding a caravan −10,
  taking a settlement −20, winning beside an ally +10. Shown as a STANDING
  block on the character sheet, saved, in the harness dump. Deltas flat
  TODO(balance). Follow-ups: per-lord scores; effects (recruit prices,
  vassalage gates) arrive with F2.
- [ ] **F2. Vassalage.** Swear to a king: get a settlement fief, owe wartime
  muster (join your liege's siege target); lords react by relation.
- [ ] **F3. Player kingdom.** Rebel or conquer unowned: your own faction
  banner/colour, grant fiefs to hired lords (mercenary captains → vassals),
  other crowns react (F1) — the Warband endgame.
- [ ] **F4. Quests (structure).** A `QuestDef` registry + simple givers
  (guild master in towns, lords in halls): deliver goods, hunt bandits,
  collect taxes. Rewards flat. This gives the early game purpose.

## Track G — Combat & battlefield depth

- [x] **G1. Battle AI efficiency.** Shipped: `SoldierGrid` (uniform XZ grid,
  rebuilt per tick) now backs target search, separation, line-break and
  trample checks. `--bench 1000` (2000 soldiers): 46.8→38.0 ms avg frame
  (21→26 FPS); the remainder is GPU render cost.
- [ ] **G2. Tournaments.** Arena in towns: bracketed melee rounds with borrowed
  gear, bet gold, renown reward. Reuses the battle module with a
  `BattleSetup` arena flag (no terrain gen, ring walls).
- [x] **G3. Morale & rout.** Shipped: a side whose fighting strength drops
  under 30% breaks all at once — its soldiers drop their targets and run for
  their own field edge (can still be cut down), escape alive after 10 s of
  flight (survivors, not casualties), wall garrisons hold to the death, and
  the win check counts only willing fighters, so battles end at the decisive
  moment. THEY BREAK banner + war-cry sting. Flat numbers TODO(balance).
  Follow-up: per-soldier morale (leader death, hero aura) instead of one
  side-wide threshold.
- [ ] **G4. Shield/armor matter (structure).** Directional block vs swing dir
  for AI; shields degrade; horses can be killed under riders. Flat numbers.

## Track H — Feel & content

- [ ] **H1. Recruit-a-hero companions.** Named companions in taverns (content
  defs, personalities in dialogue) who fight beside you, level, and can be
  fitted from inventory — Warband's companion party.
- [ ] **H2. More content tiers.** Fill each faction's upgrade trees 3–4 deep
  (recruit → veteran → elite) with distinct silhouettes; bandits get lairs
  (fixed map dens that respawn parties until raided).
- [ ] **H3. Mouse-driven UI pass.** Clickable buttons/rows on the map, party,
  inventory, and settlement screens (keyboard stays). Unblocks casual play.
- [ ] **H4. Dialogue screens.** Talking to lords/NPCs opens a portrait dialogue
  screen (persuade/threaten/quest hooks) instead of floating text.

## Track I — World scale & moddability (user directive 2026-07-20)

- [x] **I1. Data-driven map.** Shipped: `MapDef` on Content (size, player
  start, party count, settlement list with owners) with a built-in fallback,
  overlaid from moddable `assets/map.cfg` (documented format in the file).
  Follow-ups: biomes/roads/lord names could move into the cfg too.
- [x] **I2. Bigger map.** Shipped: default world grown 2000→3000 units, 6→9
  settlements (Halmar, Skjold, Emberfall), 7 starting parties. All harness
  scripts pass; campaign tick is small-n. Grow further in map.cfg at will.

## Track J — Tactical battles & player impact (user directive 2026-07-20)

- [x] **J1. Targeting AI.** Shipped: scored target search (`FindTarget`) —
  distance plus a crowding penalty per foe already aiming at the candidate
  (spreads the line), a bonus for whoever is attacking you, and an archer
  bonus for unshielded marks. Weights flat TODO(balance).
- [x] **J2. Slower, more tactical pacing.** Shipped: `PACE_COOLDOWN_SCALE`
  (1.5× soldier swing recovery) and `PACE_MOVE_SCALE` (0.85× soldier closing
  speed) — the hero keeps full speed, so player skill is the edge. Both flat
  TODO(balance). Follow-up: move the scales into a cfg once J4's settings
  file exists.
- [x] **J3. Player impact.** Shipped: hero rally aura — allies within 12 u of
  the hero recover swings 25% faster; a kill by the hero's own hand rings a
  4 s rally pulse that doubles the aura (war-cry sting, RALLIED banner), and
  the HUD counts your kills. Flat numbers TODO(balance). Follow-up: morale
  ties into G3 when it lands.
- [x] **J4. Settings (v1).** Shipped: `src/settings.h/.cpp` + moddable
  `assets/settings.cfg` — window size, fullscreen, LOD draw distance,
  particles on/off, master volume, invert-Y. Presentation/comfort only;
  simulation never reads it, so headless runs are setting-independent.
  Follow-ups: an in-game settings screen; graphical upgrades (shadows,
  nicer sky, water) when cheap on the frame budget.

## Sequencing guidance

User-directive tracks I and J lead: G1+J1 (spatial grid → targeting AI) and I1
(data-driven map) are the current top priorities, with J4 (settings) as a good
standalone session task. E1→E2 next (marketplace was explicitly requested and
unblocks E3/E4/F4-goods quests). F-track after
E-track exists to pay for it. Prefer finishing a track's structure over
starting a new one; prefer one shipped, tested, committed feature per session
over two half-features.

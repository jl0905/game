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
- [x] **F2. Vassalage (v1).** Shipped: press V at a settlement of a kingdom
  you're at peace with (standing ≥ 0) to swear — the crown grants one of
  its villages as a fief, and your wars align with the liege's (re-synced
  daily as diplomacy shifts). Persists in saves; harness `swear` + `liege=`
  dump; `tests/vassalage.txt`. Follow-ups: muster obligations (ride to the
  liege's sieges), renouncing the oath, per-lord reactions.
- [x] **F3. Player kingdom (v1).** Shipped: hold two settlements and press K
  to claim a crown — sworn oaths break as rebellion (liege −40 + war), every
  other crown answers with war and −20 standing. A crowned ruler presses L
  at an owned settlement to raise a vassal lord (300 gold, 60-man host,
  4 max, named Bram/Edric/Sable/Corwin) who marches, sieges and respawns by
  the ordinary lord AI. Saved; harness `crown`/`raiselord`;
  `tests/player_kingdom.txt` plays sack → crown → raise → reload. All flat
  TODO(balance). Follow-ups: granting specific fiefs to lords, faction
  colour/banner picker, crown victory ceremony.
- [x] **F4. Quests (v1).** Shipped: `QuestDef` registry (Bandit Hunt, Grain
  Delivery), one active at a time, offered with G in any settlement
  (rotating by settlement+day). Hunts complete on breaking an outlaw band;
  deliveries pay at the destination gate and consume the cargo. Rewards
  gold + relation with the giver's faction, all flat TODO(balance); saved;
  harness `quest` + dump; `tests/quests.txt` plays both to completion.
  Follow-ups: gate on a guild-master NPC, collect-taxes shape, lord quests.

## Track G — Combat & battlefield depth

- [x] **G1. Battle AI efficiency.** Shipped: `SoldierGrid` (uniform XZ grid,
  rebuilt per tick) now backs target search, separation, line-break and
  trample checks. `--bench 1000` (2000 soldiers): 46.8→38.0 ms avg frame
  (21→26 FPS); the remainder is GPU render cost.
- [x] **G2. Tournaments (v1).** Shipped: `BattleSetup::arena` — press T in a
  town for a bout on a flat sanded ring (no weather/mounts/battle lines):
  hero with a practice blade + 3 borrowed recruits vs 4 brigands; the purse
  is 150 gold + hero XP, and the real warband is untouched win or lose.
  Flat numbers TODO(balance). Follow-ups: bracketed rounds, betting, ring
  walls, a town-HUD hint for the T key.
- [x] **G3. Morale & rout.** Shipped: a side whose fighting strength drops
  under 30% breaks all at once — its soldiers drop their targets and run for
  their own field edge (can still be cut down), escape alive after 10 s of
  flight (survivors, not casualties), wall garrisons hold to the death, and
  the win check counts only willing fighters, so battles end at the decisive
  moment. THEY BREAK banner + war-cry sting. Flat numbers TODO(balance).
  Follow-up: per-soldier morale (leader death, hero aura) instead of one
  side-wide threshold.
- [x] **G4. Shields matter (structure).** Shipped: soldiers swing with a
  direction and shield-bearers hold a guard (deterministic habits for now);
  a swing into the guard is mostly wood (35% through), each block and each
  arrow taken on the shield wears it (40 HP), and the hero's aimed swing
  direction is checked against the guard too (sparks + clang on wood).
  Horses under riders were already mortal (cavalry work). Flat numbers
  TODO(balance). Follow-ups: smarter guard choice, broken-shield visual.

## Track H — Feel & content

- [x] **H1. Companions (v1).** Shipped: three unique heroes-for-hire
  (Rega the Grim, Malin Longeye, Torva Ironhand — `TroopDef::companion`),
  one hosted per settlement (rotating), hired with H for flat 100 gold at
  triple wage. As party members they fight, draw wages, and persist in
  saves with zero extra plumbing. Follow-ups: gate on the tavern room,
  dialogue personalities (with H4), fitting them from the inventory,
  knock-out instead of death.
- [x] **H2. Content tiers & bandit lairs.** Shipped: Marksman (archer's
  ladder) and Marauder (brigand's ladder, fielded by raiders) deepen every
  troop line to 3–4 tiers. Bandit dens live in `assets/map.cfg` (`lair`
  lines): they breed an outlaw party every other day until the player
  storms one — the den's defenders muster at double strength, victory
  burns it out for good, and the ash persists in saves. Flat numbers
  TODO(balance). `tests/lair.txt` burns the deserters' den.
- [x] **H3. Mouse-driven UI pass (v1).** Shipped: click rows everywhere the
  keyboard worked — title menu options, party roster (click promote /
  right-click dismiss), character attributes, market wares (click buy /
  right-click sell); the map (towns, dens) and inventory grid were already
  mouse-driven. All hit-boxes mirror the draw layouts in Gather, so
  simulation and the harness are untouched. Hint footers mention clicks.
  Follow-ups: hover highlights, shared layout constants between
  Gather/Draw instead of mirrored numbers.
- [x] **H4. Dialogue screens (v1).** Shipped: press E beside a villager or
  guardsman to open a conversation screen (painted bust, name, spoken
  lines): a greeting, [1] live war news composed from world state, [2] a
  work rumor pointing at the quest giver and a bandit den. Esc/E returns
  to the streets with the scene intact. Harness `talk`/`topic` commands +
  `tests/dialogue.txt`. Follow-ups: talk to lords in castle halls
  (persuade/threaten), companion personalities, real portraits.

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

## Track K — Second wave (deepening the shipped v1s)

The first board is swept: every core Warband system exists in v1. This wave
turns v1s into keepers. Ordered roughly by player-visible payoff.

- [x] **K1. In-game settings screen.** Shipped: press O on the title or the
  map — number keys or clicks cycle fullscreen, draw distance (30–90),
  particles, volume and invert-Y live; Esc writes the cfg back (with its
  documentation preserved) and returns whence it came. Presentation-only
  by design: `tests/settings.txt` proves the sim is untouched and the cfg
  round-trips.
- [x] **K2. Lords in the hall.** Shipped: a castle's keep door now opens the
  lord's court (H4 dialogue, named for the crown's first lord): ask for
  news, swear your sword (full F2 oath with spoken refusals — "your name
  is mud", "you are at war"), or take work (F4). The V and G hotkeys share
  the same extracted TrySwear/TryQuest logic, so both paths stay in step.
  Harness `court`; `tests/lord_court.txt`. Follow-up: per-lord audiences
  (any lord present, not just the crown's first name).
- [x] **K3. Tournament brackets & betting.** Shipped: three narrowing rounds
  (even 4v4 melee → 2v2 → a true final duel, hero against champion);
  Shift+T stakes 50 gold that pays 3× on the championship on top of the
  150 purse; elimination in any round ends the bracket and eats the stake.
  Fixed en route: the aftermath can now chain into a follow-on battle
  (CampaignUpdate no longer stomps the redirect). The blind harness carries
  rounds 1–2 but loses the duel — winning it takes real swordsmanship,
  which is the J3 point. Flat numbers TODO(balance). Follow-up: renown →
  relations on championship.
- [ ] **K4. Per-soldier morale.** Replace G3's side-wide threshold: each
  soldier weighs nearby deaths, leader down, hero aura; lines crumble from
  the flanks instead of all at once.
- [ ] **K5. Muster obligations.** A sworn vassal is summoned to the liege's
  sieges (banner alert + relation penalty for ignoring it); a crowned
  ruler can point raised lords at a target settlement.
- [ ] **K6. Fit companions from the bag.** Equip hired companions from the
  tiled inventory (their Loadout is already per-troop data).
- [ ] **K7. UI polish pass.** Hover highlights on clickable rows, shared
  Gather/Draw layout constants, a town-HUD line listing the local keys
  (T bout, M market, G work, H hire, V oath, E talk).
- [ ] **K8. Map biomes & roads into map.cfg.** The painted biome map, roads
  and lord name pools move from code into the map file (finishes I1's
  moddability).
- [ ] **K9. Balance-pass prep.** One document listing every TODO(balance)
  constant with its file, current flat value, and intended feel — the
  worksheet a human playtesting pass needs. (Numbers stay flat until then.)

## Sequencing guidance

User-directive tracks I and J lead: G1+J1 (spatial grid → targeting AI) and I1
(data-driven map) are the current top priorities, with J4 (settings) as a good
standalone session task. E1→E2 next (marketplace was explicitly requested and
unblocks E3/E4/F4-goods quests). F-track after
E-track exists to pay for it. Prefer finishing a track's structure over
starting a new one; prefer one shipped, tested, committed feature per session
over two half-features.

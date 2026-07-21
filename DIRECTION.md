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
- [x] **K4. Per-soldier morale.** Shipped: every soldier carries `nerve`
  (100) — an ally dying within earshot costs 15, a foe falling restores
  10, the hero going down shakes the whole line for 20, the hero's
  kill-cry stiffens friends and shakes foes, and courage regenerates
  2/s. Nerve at 0 breaks that one soldier; the side-wide banner rings
  when half a side has fled. Arena bouts and wall garrisons never rout.
  All flat TODO(balance).
- [x] **K5. Muster obligations.** Shipped: when the liege's lords invest a
  settlement, the sworn are summoned (banner alert, 3 days): riding to
  the siege pays +10 standing, ignoring it costs −15. A crowned ruler
  presses J to rally raised lords to the banner for 3 days (verified:
  Lord Bram marched 1500 units to within 5 of it). Duties persist in
  saves; harness `rally` + muster/rally dumps; `tests/muster.txt`. Flat
  numbers TODO(balance). Follow-up: point lords at a *target settlement*
  rather than the ruler's position.
- [x] **K6. Fit companions from the bag.** Shipped: Tab in the inventory
  cycles the equip target (you, then each hired companion); equipping
  swaps into a per-companion fitted Loadout, the displaced piece drops
  back in the bag, and companions fight (and render) in their fitted gear
  via a `BattleSetup::gearOverrides` contract extension. Persists in
  saves; harness `target` + `cgear:` dump; `tests/companion_gear.txt`
  dresses Rega in looted armour and reloads him.
- [x] **K7. UI polish pass.** Shipped: a `layout` namespace both Gather
  hit-boxes and draw rows quote (title/market/party/character/settings — no
  more mirrored literals), a gold hover band behind every clickable row
  (`DrawHoverRow`, draw-only so the sim never sees it), and the town HUD
  now lists the local keys (T bout, M market, G work, H hire, V oath,
  E talk).
- [x] **K8. Map biomes & roads into map.cfg.** Shipped: `MapDef::BiomeSpec`
  (noise frequencies, forest/mountain thresholds, march speeds), road
  link-distance/width, and the raised-lord name pool all load from
  `assets/map.cfg` (`biome`/`road`/`lordnames`, documented in the file; the
  pool size is also the raised-lord cap). One biome field now drives the
  map paint, terrain classification, travel speed AND the battlefield —
  the bridge passes hilliness/forest to `BattleSetup`, so battles follow a
  modded biome while the battle module stays world-blind. Verified: a
  hacked threshold turns the whole world to forest at the modded speed.
- [x] **K9. Balance-pass prep.** Shipped: `BALANCE.md` — every TODO(balance)
  constant (core `base::*` set, campaign pulse, economy, battle feel,
  structural stubs) with its site, current flat value and intended feel,
  plus a how-to-run-a-tuning-session footer. Numbers stay flat until a
  human plays through the sheet.

## Track L — Cohesion & integration (user directive 2026-07-21)

Connect the shipped systems into one living world: NPCs do what the player
does, the economy is a single flow, terrain matters on the march.

- [x] **L1. Terrain travel speed.** Shipped: the map-paint noise classifies
  any point (`WorldTerrainAt`) — forests 0.7×, mountains 0.55×, for player
  and AI parties alike; the drawn road network is now queryable (`OnRoad`)
  and negates the penalty. HUD + harness show the going. Flat TODO(balance).
- [x] **L2. Caravans carry real freight.** Shipped: wagons load up to 8 units
  of the origin's surplus stock, unload into the destination's market, and
  plunder spills the actual cargo. Persists (`ccargo`); dump shows
  `caravan(cargo=N)`.
- [x] **L3. Lords recruit like the player.** Shipped: a lord under half
  strength rides to his own banner's settlement and takes +5 volunteers/day
  until whole; respawn timers remain only for the fallen.
- [x] **L4. Living markets.** Shipped: towns produce their source goods
  (+1/day, cap 20) and consume imports (−1/day); prices quote
  base × offset × scarcity (4%/unit off a 10-unit par, clamped), exported
  as `MarketBuyPrice/SellPrice` so every consumer quotes the same number.
- [x] **L5. Cached map paint.** Shipped: biome ground + roads render once
  into a half-resolution RenderTexture (rebuilt if map size changes) and
  blit as one quad — ~900 rects and thousands of triangles off every
  campaign frame. Settlements/parties/ownership still draw live.
- [x] **L6. NPC parity.** Shipped: bandits smell freight (a laden caravan
  reads at half distance to Aggressive-behaviour parties, so outlaws prey
  on the trade flow like the player), and a full-strength lord at his
  settlement drills two men a day up their troop lines — the NPC
  counterpart of the player's promote button. Follow-up idea (cosmetic):
  villagers walking the roads between towns.

## Track M — Third wave: reputation, command, and pageantry

Everything Warband still does that we don't, ordered by how much world it
adds per line of code. Structure first, numbers flat, as ever.

- [x] **M1. Renown & honor.** Shipped: renown from victories (+1, +1 per 5
  slain up to +4), championships (+5) and quests (+2); honor +1 per quest,
  −1 per caravan raid. Renown gates vassalage (`RENOWN_TO_SWEAR` 5, with a
  spoken refusal) and sets the party cap (`PartyCap` = 20 + renown,
  enforced at the tavern with a message). Character sheet + harness hero
  line show both; `fame` save tag; harness `fame R H` command for
  scenarios; `tests/renown.txt`. All flat TODO(balance). Follow-up: honor
  feeding per-lord opinions (M3/M5).
- [x] **M2. Battlefield orders.** Shipped: F1 Hold / F2 Follow / F3 Charge,
  barked instantly mid-fight (no menu) — orders resolve orthogonally to
  formation *shape* at the AI call site, so ComputeAI stayed order-blind:
  Hold freezes the anchor where the order rang, Follow anchors on the
  hero, Charge frees everyone (a shapeless Charge order defaults the
  shape to Line). The war-cry banner shouts the order; the HUD shows
  Order + Shape; `BattleView` gains order/anchordist so scripts can see
  obedience; harness `order` command; `tests/orders.txt` holds a line at
  3.2 u then charges to victory.
- [x] **M3. Fief grants.** Shipped: court topic [4] (crowned rulers only)
  grants the seat you stand in to your first landless raised lord — he
  taxes it (its income skips both ledgers), respawns at it when he falls,
  and the grant earns +1 honor. `Town::fiefLord`, `fief` save tag,
  `fief=` in the harness town dump; `tests/fief.txt` plays sack → crown →
  raise → grant → reload. Follow-up: per-lord standing moved by grants
  and refusals.
- [x] **M4. Player caravans.** Shipped: [C] at any market outfits a convoy
  (200 gold + the cargo's live book value); it loads the origin's cheap
  produce (all caravans now carry true surplus, not just the fullest
  shelf), plies markets at peace with you, sells each unit at the live
  destination price and sends the proceeds home, then reloads on your
  purse. Bandits smell it like any laden caravan. `ccost` save tag,
  harness `sendcaravan`; `tests/player_caravan.txt` turns 56 gold of
  cargo into 64. Follow-up: choosing the route; a caravan-lost notice.
- [x] **M5. Feasts & marriage (v1).** Shipped: every 4th day a kingdom at
  peace with the player feasts at one of its towns for 2 days (news line,
  harness `feast:` dump); walking in pays +5 standing and +1 renown, once
  per feast. Court topic [5] at the feast weds you (renown 10 required,
  one spouse ever): +20 standing, and NudgeRelation (moved to world.h)
  floors the house's standing at 0 forever — family forgives. `feast`/
  `spouse` save tags; `tests/feast.txt` attends and weds Lady Mira.
  Follow-ups: spouse gameplay (intercession, heirs), lords gathering at
  the hall, feast scenes.
- [x] **M6. Town life (v1: the roads).** Shipped: a `travellers` faction —
  the small folk — keeps up to 3 bands walking town to town via the
  caravan machinery (they trade in a small way, and freight-scenting
  bandits hunt their packs; the blanket outlaw wars made them prey with
  zero extra code). `TradesAnywhere` generalises landless traders.
  Descoped to a follow-up (pure scene dressing, no system value): market
  stall props, minstrel sting — do these in a dedicated art/audio pass
  with real assets rather than more procedural boxes.

## Track N — Fourth wave: depth where players live

- [x] **N1. Siege engineering.** Shipped: walls now open a choice — storm
  now (gate + the two standing ladders), build ladders (1 day: four
  climbs), or a siege tower (2 days: a wide rolling ramp as well) —
  via `BattleSetup::siegePrep` and runtime climb points (drawn: extra
  ladders, a timber tower with its ramp on the rampart). The cost is
  real time: the warband camps at the walls while the garrison musters
  on, relief lords roam, and diplomacy can end the war under you (all
  observed in play). Camp persists (`scamp` tag); harness `siege` cmd +
  `climbs=` in the battle dump; `tests/siege_prep.txt`. Follow-ups:
  defenders dropping stones; a relief-army set-piece battle.
- [x] **N2. Character creation.** Shipped: `Screen::Background` after New
  Game — a noble's second son (+5 renown, +200 gold, helmet, patrol
  favour), a merchant's heir (+400 gold, 10 grain, +1 honor), or a
  deserter (3 brigands, −150 gold, patrol suspicion). `ApplyBackground`
  is shared with the harness `background` command; mouse + keys via the
  shared layout. All flat TODO(balance); `tests/background.txt`.
- [x] **N3. Save slots.** Shipped: F5/F6/F7 quicksave to three slots from
  the saddle; the title gains [L] Load Game → `Screen::LoadMenu` listing
  autosave + slots with a peeked "day N, gold G" line (`PeekSave`), empty
  rows greyed. `SaveSlotPath` shared with harness `saveslot`/`loadslot`;
  `tests/save_slots.txt` rewinds a march and survives an empty-slot load.
- [x] **N4. Per-lord opinion.** Shipped: named lords remember — a fief
  grant +20, a victory at their side +10, their host broken by you −15
  (stored); honor is added at every *reading* (`EffectiveLordOpinion`),
  so honest captains are trusted a little everywhere. A first lord at
  effective −10 speaks against your oath at court. LORDS block on the
  character sheet, `lop` save tag + harness dump; `tests/lord_opinion.txt`.
  Follow-up: opinions gating council votes/elections when crowns deepen.
- [x] **N5. Art/audio pass.** Shipped (all synthesized, matching the
  existing procedural-audio approach): market stalls ring town plazas
  (posts, goods-tinted canopies, wares crates — colours from the goods
  catalogue); a plucked-lute minstrel loop plays in taverns (faint at the
  door, full by the hearth); a low modal drone beds the campaign map
  under the wind; and melee thuds rotate through three sibling voices so
  a press of bodies doesn't drum one note. Presentation-only — the suite
  proves the sim untouched.
- [x] **N6. Battle scale stress.** Shipped: `--bench 1000` (2000 soldiers)
  re-measured at 33.3 ms avg / 39.5 p99 (was 38.0 post-grid) — the sim
  holds, render is the remaining cost. AI lord hosts raised 120→150
  (still flat TODO(balance)); typical battles stay far under the 2000
  ceiling. Remaining idea (garrison + relief army in one siege) moves to
  a future siege track alongside N1.

## Track O — Fifth wave: rule what you won

- [x] **O1. Kingdom ledger screen.** Shipped: B on the map opens THE LEDGER
  — rank line (free captain / sworn / crowned, renown, honor, spouse),
  fiefs with prosperity and per-day pay (granted seats shown as "held by
  Lord X" with their income kept by the lord), the income equation
  (settlements + enterprises − wages, and what lords keep), lords afield
  with strength/state/opinion, prisoners in your train, and current wars.
  Read-only by design; Esc/B closes. Harness `ledger` +
  `tests/kingdom_ledger.txt` proves opening it disturbs nothing.
- [x] **O2. Prisoner lords.** Shipped: breaking a lord's host takes him
  prisoner (battle-report line; no respawn while he sits in your train).
  At any settlement, U ransoms all captives to their crowns (200 gold a
  head, −10 opinion each) and Y frees them (+2 honor, +20 opinion,
  +5 crown standing each); either way a respawn is queued and he rides
  again. `plord` save tag, harness `capture`/`ransomlords`/`releaselords`;
  `tests/prisoner_lords.txt` plays both fates. Flat TODO(balance).
  Follow-up: captives escaping over time; lords ransoming each other.
- [ ] **O3. Night and dusk.** The day clock already names morning/evening:
  tint the campaign map and battlefield lighting by hour; night battles
  darker and closer. Presentation with tactical texture.
- [ ] **O4. Arms from the forge.** Towns with a smithy sell weapon/armour
  *pieces* at the market (stock from prosperity), closing the loop from
  iron → smithy → the inventory the player already fits companions from.
- [ ] **O5. Modding guide.** MODDING.md: map.cfg, settings.cfg, fonts.cfg,
  content registration, and the harness — one door for modders.
- [ ] **O6. Rebellion arcs.** A sworn vassal can scheme: gather lord
  opinions, declare for the crown you serve to be yours, and fight the
  civil war (the crowning move the sandbox is missing).

## Sequencing guidance

User-directive tracks I and J lead: G1+J1 (spatial grid → targeting AI) and I1
(data-driven map) are the current top priorities, with J4 (settings) as a good
standalone session task. E1→E2 next (marketplace was explicitly requested and
unblocks E3/E4/F4-goods quests). F-track after
E-track exists to pay for it. Prefer finishing a track's structure over
starting a new one; prefer one shipped, tested, committed feature per session
over two half-features.

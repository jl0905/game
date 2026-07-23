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
- [x] **O3. Night and dusk.** Shipped: the campaign map already had its
  veil and lit windows; the battlefield now joins the clock —
  `BattleSetup::timeOfDay` carries the hour, the sky lerps blue noon /
  amber dusk / deep night, and a screen veil presses close after dark
  (under the HUD). Presentation only; the suite proves the sim
  untouched. Track O complete.
- [x] **O4. Arms from the forge.** Shipped: every town market has an ARMS
  counter — two pieces (one armour, one weapon) rotating by town and day,
  keys 7/8, flat 50 gold each, placed straight into the tiled bag the
  player already fits themself and companions from. Towns only; villages
  sell produce, not steel. Harness `buyarm armor|weapon`;
  `tests/forge_arms.txt`. Follow-up: stock/price from prosperity and a
  built smithy enterprise.
- [x] **O5. Modding guide.** Shipped: `MODDING.md` — the two tiers (config
  files with no rebuild: map.cfg incl. biome/road/lordnames tables,
  settings.cfg, fonts.cfg; content registration in content.cpp with a
  recipe per def type), plus how to test a mod with the harness and the
  49-script scenario library.
- [x] **O6. Rebellion arcs.** Shipped: court topic [6] at a liege
  settlement — "The crown should be mine." Gated on renown 15 and a
  majority of the crown's lords at effective opinion ≥ 10 (court them, or
  let honor carry you), with spoken refusals. On success the willing
  lords defect with their hosts, the crown answers with war (−60
  standing), the oath breaks (−2 honor), and you are crowned into the
  civil war. `tests/rebellion.txt` turns both patrol lords and reloads
  the war. All gates flat TODO(balance). Follow-up: loyalist lords
  hunting the usurper specifically; a coronation moment.

## Track P — Sixth wave: the cost of war

War should scar the map, not just the rosters.

- [x] **P1. Village raiding.** Shipped: hostile villages join the assault
  prompt — [1] take it, [2] put it to the torch. A raid fights the same
  battle but the aftermath differs: 100–150 gold + up to 8 wares into
  the saddlebags, prosperity −50 (floor 30), the banner stays; −3 honor,
  −15 standing, +1 renown (infamy is fame). Warring lords bleed enemy
  villages the same way daily (−10 prosperity, −1 stock within 200 u,
  with a warning when it's yours). `tests/raid.txt` burns Tulga. All
  flat TODO(balance).
- [x] **P2. Relief battles.** Shipped: a defending crown's lord within
  250 u joins the garrison when you assault — one battle against both
  (`ReliefLordFor` in world.h, called identically at launch to merge his
  host into `BattleSetup` and at the aftermath to settle his fate: beaten
  he falls with the walls and into your train (O2 capture), victorious
  his host stands). Storming a lord-guarded seat is now storming an army.
  `tests/relief_battle.txt` breaks against 158 defenders at Curaw.
  Flat TODO(balance).
- [x] **P3. Companion voices.** Shipped: `TroopDef::temper` — Malin and
  Torva are honorable, Rega is grim. At a raid's fire the honorable
  object and the grim grin (battle-report lines); each dawn your honor
  sits at −3 or worse, one honorable companion walks out with parting
  words. Amended P2 en route: relief armies only reach *walled* sieges —
  villages fall or burn before help arrives (also restores the meaning of
  every storm-Tulga scenario, which had silently begun fighting Lord
  Vex's 150). `tests/companion_voices.txt` plays hire → burn → objection
  → departure. Track P complete.
- [x] **P4. The first hour.** Shipped: one-time hint toasts on a captain's
  firsts (victory → promote with P; captives → ransom with R; loot →
  inventory I/E/Tab), a contextual "what now?" line on the map HUD
  (thin band → recruit; unknown → win renown; sworn → serve; crowned →
  N banners remain), and the title naming the goal. Seen-bits persist
  (`hints` tag); `tests/first_hour.txt`.
- [x] **P5. Supply lines.** Shipped: the warband eats daily — 1 grain per
  10 mouths from the saddlebags first, then forages at 3 gold per missing
  unit; broke *and* breadless, men desert in the night (1 + men/10). The
  ledger line reports which. Grain is now a strategic good: markets sell
  it, raids loot it, caravans move it, armies starve without it.
  `tests/supply.txt` forages day 1 and eats from the bags day 2. All
  flat TODO(balance). Follow-up: AI hosts eating too (their side of the
  logistics war).

## Track Q — Seventh wave: the whole cloth

The systems all exist; this wave is about them holding together over a
full career, and the last Warband textures.

- [x] **Q1. The campaign arc.** Shipped: `tests/campaign_arc.txt` lives a
  career — noble birth, tavern recruiting, provisioning, a road victory,
  the oath (Emberfall in fief), rebellion (both patrol lords turn), and
  a crown that survives the save. It immediately surfaced real friction:
  tavern recruiting was unreachable from scripts (position-gated since
  the walkable town shipped) — fixed with a `tavern` harness shortcut in
  the `court` idiom. Keep this script green forever.
- [x] **Q2. AI armies eat.** Shipped: a lord's host more than 600 u from
  any settlement at peace with him sheds 1 + men/20 daily — long
  campaigns wither, and the recruit-at-home loop (L3) becomes the other
  half of a real war rhythm. Flat TODO(balance).
- [x] **Q3. Knocked out, not dead.** Shipped: a companion counted among the
  fallen is instead carried senseless from the field (battle-report line,
  loss zeroed before the books close), and `RemoveTroops` now skips
  companions everywhere — starvation, unpaid wages, and auto-resolve
  can't strip a hero either. Heroes leave by choice (P3) or not at all;
  death stays for the nameless. `tests/knockout.txt`.
- [x] **Q4. Sinks and decay (renown half).** Shipped: renown fades a point
  a week without new deeds ("the bards move on to newer songs") — the
  party cap and courts breathe with it. Amended: the wage-scaling half is
  *tuning*, not structure (TROOP_WAGE is already a BALANCE.md row), so it
  moves to the human balance pass rather than baking a progression here.
  `tests/attrition.txt` covers both Q2 and the decay.
- [x] **Q5. Autosave cadence.** Shipped: the game autosaves at every dawn
  (the day tick) alongside the existing on-quit save — a crash costs a
  day, not a career. `tests/autosave.txt` marches past a dawn and rewinds
  to it.

## Track R — Eighth wave: texture and consequence

- [x] **R1. Weather fights too.** Shipped: on a raining field archers
  engage at 60% range and nock 1.4× slower — wet strings throw short.
  `rain=` in the battle dump; `tests/rain.txt` raids Tulga under cloud.
  Flat TODO(balance). Check the sky before you rely on archers.
- [x] **R2. Deployment.** Shipped: windowed battles open in a planning
  pause — look over the field, set shape (1-4), ranks ([/]) and the
  opening order (F1-F3), then SPACE/LMB sounds the horn (banner + cry).
  Architecture-clean: the pause is armed at init and tripped by the first
  *windowed gather*, so headless sims never see it and every script is
  bit-identical (63/63 unchanged proves it). Arena bouts skip it — the
  ring waits for no plans.
- [x] **R3. Roster polish.** Shipped: the party screen shows Troops N/cap
  (renown's cap made visible), a per-line g/day wage column, and a
  veterancy bar per upgradable line (gold filling to lime at promotable).
  Draw-only; sim untouched (63/63). Track R complete.
- [x] **R4. World events.** Shipped: `EventDef` registry (news line with
  the settlement's name, prosperity/stock deltas, outlaw bands raised) —
  Good Harvest, Murrain, and A Bandit King Rises registered; one fires
  every third day at a rotating settlement. Modders add events like they
  add goods (MODDING.md updated). `tests/world_events.txt` watches the
  harvest lift Praven to 120. Flat TODO(balance).
- [x] **R5. Lords change banners.** Shipped: every fifth day, one lord of
  a beaten crown (≤1 settlement) rides to the ascendant one (≥3) — one
  turncoat a season, AI crowns only (players win lords by rebellion).
  Verified with a committed fixture save (`tests/defection.owb`): the
  live world crowned Vaeling ascendant and Lord Aldric abandoned the
  beaten Patrol, deterministically. Flat TODO(balance).
- [x] **R6. The standing bench.** Shipped: a "Standing bench" table in
  BALANCE.md, logged at each wave's end (post-grid 38.0 → N6 33.3 →
  Track R 33.8 ms avg at 2000 soldiers — this wave's features landed
  free). Investigate any avg regression past ~15%.

## Track S — Ninth wave: a ruler's hands

- [x] **S1. Sue for peace.** Shipped: the ledger's war rows are numbered —
  press the number, pay tribute (100 + 5×war score), and an ordinary
  truce begins (+5 standing with the relieved crown). Kingdoms only:
  outlaw wars read "(no quarter)", a rule the first test run enforced
  after happily buying silence from the Raiders. AI always accepts (v1;
  acceptance conditions are a follow-up). `tests/sue_peace.txt` rebels
  then buys the peace back. Flat TODO(balance).
- [x] **S2. Garrison your walls.** Shipped: F leaves a soldier (from your
  fullest line; companions never garrison), Shift+F recalls one (cap
  respected) at your own settlements. Garrisons already fought,
  mustered, and persisted — this was the missing hand. Town HUD hint,
  harness `garrison`/`ungarrison N`; `tests/garrison.txt` mans a stormed
  Tulga and reloads the wall.
- [x] **S3. The wounded cart.** Shipped: half of every line's fallen are
  wounded, not dead — they ride behind the warband (`wounded` pool,
  saved, in the dump) and one per line rejoins each dawn, cap willing.
  The battle report counts them; the ledger notes returns. Verified
  under fire: a defeat's arithmetic (fallen halved into the cart) checks
  exactly even mid-chaos. Flat TODO(balance).
- [x] **S4. Any lord's audience.** Shipped: press T on the map to hail the
  nearest non-hostile lord party within 30u — the full court dialogue on
  the road (news, oath to HIS crown via `AudienceFaction`, work from the
  nearest hall; seat-grants politely refused, hostile lords "talk with
  steel"). Castle courts now name the seat's own fief-holder, defectors
  included. Harness `parley`; `tests/parley.txt`.
- [x] **S5. A ruler's purse audit.** Shipped: one `ComputeLedger` quoted by
  the day tick, party screen, kingdom ledger and a new harness `purse:`
  line — income + enterprises in; troop wages, landless lords' 10/day
  retainers (landed lords tax their fiefs instead, closing M3's loop)
  and garrison pay (men/2) out. Manned walls finally cost what they're
  worth. `tests/purse.txt` balances the books to the gold. Rates flat
  TODO(balance).

## Track T — Playtest one (user feedback, 2026-07-21)

The first human playtest report. Balance changes here are sanctioned —
this is the feedback the flat numbers were waiting for.

- [x] **T1. Combat feel ("attacks do no damage").** Diagnosed by probe: no
  code glitch — damage always landed, but static shield guards soaked
  silently, 100 HP × 10 dmg meant ten unseen hits per man, and there was
  no hit reaction. Shipped: hit-stun (0.45 s reel on any solid blow —
  melee and arrows both; the victim can't move or riposte), reactive
  guards (a shield-bearer covers where he was last struck, so varying
  your swing direction gets through — spam is punished, craft rewarded),
  and the first playtest-tuned number: hero swings deal 2.5×. Verified:
  a lone hero with varied swings now cuts down three shielded recruits;
  one-note spam still fails.
- [x] **T2. Map zoom.** Shipped: mouse wheel zooms 0.35×–2.5×, device-side
  view state — click hit-testing shares the camera, so zoomed clicks
  stay true; headless sims never see it.
- [x] **T3. Settlement silhouettes.** Shipped: a village is a low huddle of
  thatched huts, a town a walled sprawl of red roofs around a gilt hall,
  a castle a tall grey keep between crenellated towers — readable at any
  zoom without the label.
- [x] **T4. Travel-pace indicator.** Shipped: a pace ring under your banner
  and the going named at your feet — green "road (full pace)", amber
  "forest −30%", slate "mountains −45%"; nothing shown on plain ground.
- [x] **T5. Mounted combat identity.** Shipped: the saddle adds 0.9 reach
  (hero and AI riders alike), and the hero's gallop adds weight — up to
  +50% damage at full stride (momentum tracked from real movement).
  Riding past at speed now hits like it should. Flat TODO(balance).
- [x] **T6. Horses as entities.** Shipped: `HORSE_HIT_SHARE` 0.4→0.15
  (playtest-driven: at 0.4 the horse died *with* its rider every time,
  which is why no mount ever survived) — killing the man now leaves his
  horse, which becomes a `LooseHorse` entity: wandering the field,
  shying from fighters, drawn riderless. All three death paths (melee
  sweep, hero swing, arrows) free the mount. Committed fixture
  `tests/horses.owb` + `tests/horses.txt` watch enemies 3→0 as horses
  0→3. Also fixed en route: stun could chain-lock (added post-stun
  immunity). Follow-up: catching a loose horse to remount.
- [x] **T7. Save/load discoverability.** Shipped: a persistent key footer
  on the campaign map — party, character, bag, ledger, options,
  F5-F7 quicksave, Esc-Esc save+quit, and where Load lives (title, L,
  clickable). The systems existed (N3/Q5/H3); the player couldn't find
  them, which is a UI bug by definition. Track T complete.

## Track U — Playtest two (user feedback, 2026-07-21)

- [x] **U1. Readable footer.** Shipped: size 20 white on a solid dark band
  with a gold rule.
- [x] **U2. Implicit health.** Shipped: the floating 3D bars are gone —
  a hurt man darkens and bloodies as the fight wears him. One 2D HUD bar
  (top-centre, named) for the enemy nearest your crosshair within 16 u.
- [x] **U3. Sieges favour walls.** Shipped: `GarrisonCap` 20/10/30 (was
  8/4/12) at init, capture, and muster (now 2/day); auto-resolve walls
  fight at 1.7×; a repelled assault costs the attacker half to all of
  his host. Playtest-sanctioned numbers.
- [x] **U5. Combat clarity (added mid-sprint).** Pushed back with
  evidence: WASD never touched attack direction (aim is mouse-only by
  construction). The real confusion was continuous re-aim while holding —
  now the swing locks at the click from the last mouse flick, Warband's
  rule exactly. And the mystery yellow dots are named: they're shield
  sparks, now captioned "SHIELD! vary your swing direction" when your
  blow meets wood.
- [x] **U4. Settlement menu first.** Shipped: entering a settlement opens
  the gate menu — name, owner, prosperity, garrison and your purse up
  top, then ten rows (market, tavern, tournament, work, hire, oath,
  hall, garrison±, and "[W] Visit the settlement" for the streets), all
  clickable with hover. Rows translate into the same intents the hotkeys
  raise, so every action has one implementation and all 68 harness
  scripts pass untouched. Esc walks back to the menu; Esc again rides
  on. The tavern row walks you in at the hearth.
- [x] **U6. Press of bodies & scale.** Shipped: a hard positional
  constraint — one grid-accelerated relaxation per frame, half-push per
  overlapping pair at a 0.9 u body gap — men cannot share ground.
  Research answer (the "hivemind" question): the engine already runs the
  architecture large-battle games use — per-troop scored targeting reads
  a shared immutable snapshot in parallel across cores, all proximity
  queries go through a uniform grid rebuilt per tick (O(1) per query),
  and mutations apply serially, so there are no locks anywhere in the
  hot path. Bench: 35.7 ms avg at 2000 soldiers (+1.9 ms for the crowd
  constraint, within the 15% tripwire). The next scale steps if ever
  needed: SoA layout for the AI snapshot and staggered target refresh
  (recompute every N frames per soldier) — noted, not needed at current
  scale.
  Addendum (user asked: stochastic/dumber targeting? GPU pipeline?):
  targeting is *already lazy* — a soldier keeps his target until it dies
  or invalidates, so searches are rare and further stupidity saves ~0.
  The measured bottleneck is rendering (immediate-mode cubes per body
  part; sim ≈ ⅓, render ≈ ⅔ of the frame on the test GPU). The GPU
  roadmap, in payoff order: (1) instanced body parts
  (`DrawMeshInstanced` — one draw call per part kind instead of tens of
  thousands), (2) distance LOD — far soldiers as 2-box impostors (the
  settings lodDistance already exists to drive it), (3) frustum-cull
  soldiers behind the camera. Queued as V-track work when battles above
  ~300-per-side become the norm.

- [x] **U8. Paper-doll equipment.** Shipped: five slot boxes beside the
  bag (Head/Body/Hands/Feet/Weapon) naming what's worn. Pick a piece up
  and its slot glows gold — drop it there to wear it (displaced gear
  returns to the bag); click a worn armour piece with an empty hand to
  lift it off. Obeys the Tab companion target; E-to-equip still works.
  One shared layout for hit-test and draw (K7 rule).
- [x] **U9. Market meets the bag.** Shipped: the market gains an
  inventory-style saddlebags panel — a tinted, counted cell per good
  beside the ware list — one visual language across screens. The state
  seams held: town stock, saddlebag counts, and the tiled bag remain
  three separate stores; this is presentation only (suite untouched).
  Track U — playtest two — is fully swept.
- [x] **U10. Living map lettering.** Shipped: settlement names hold
  constant screen size against the zoom (readable fully zoomed out), in
  the owner's colour with a banner chip; hovering a settlement opens a
  Warband-style tooltip — name, type, crown (bordered in its colour),
  garrison, prosperity, standing, war/peace or the holding lord.
- [x] **U11. Dismount.** Shipped: Z steps down — your horse waits where
  you left it (a loose horse marked with a gold pennant), Z beside any
  loose horse swings you up (a fallen knight's mount will carry you
  too). The battle HUD names the key. `tests/dismount.txt` round-trips
  the saddle.
- [x] **U12. The outer marches (map stress test).** Shipped: 9 new
  settlements (Veidar, Ashfield, Dunmore, Saltcliff, Farwatch, Milldale,
  Greymoor, Thornholt, Coldharbour) appended after the original nine so
  every index-based scenario stays valid; 2 new dens; starting parties
  7→16. Measured: 9 world-days (36k frames, 18 settlements, sieges,
  caravans, events) simulate in ~0.1 s wall — campaign cost is
  microseconds a frame and could scale 10× before mattering. 69/69
  scenarios green on the big map; the arc still crowns.
- [x] **U13. Bigger text, no collisions (post-sprint playtest).** Shipped:
  a global text scale through every ui draw *and measure* (default 1.2,
  `fonts.cfg scale=`, clamped 0.8–1.6) so layouts stay coherent; and the
  real top-left overlap (what-now y38 / news y42 / siege-camp y38 all on
  the same pixels) fixed — news rides its own banded line at y66.
  Standing rule adopted: when adding HUD lines, check the y-stack.
- [x] **U7. The played exe stays current (process).** Running: pull +
  rebuild after every push (fails harmlessly while the game is open;
  retried each cycle). After every push:
  pull + rebuild `C:\Users\hambu\projects\game\build\openwarband.exe` so
  the user always launches the latest. (Currently blocked when the game
  is running — Windows locks the exe; rebuild retries each push.)
  Audit note: all other user asks to date are shipped — zoom,
  silhouettes, pace ring, stun/arrow-stun, mouse-locked aiming, block
  captions, hero damage, riderless horses, mounted identity, footer,
  implicit health, walls that hold, save/load surfacing, main-only
  branch.

## Track V — The 5-minute loop (continuous iteration, 2026-07-21)

- [x] **V1. Iteration one.** Settlement buttons audited end-to-end (rows →
  intents → single implementations; sound). Lettering up again: global
  scale 1.35 via fonts.cfg, and the text-bearing bands darkened hard
  (footer 0.88, gate menu 0.92, news 0.7) for contrast. Extensibility
  proof: the Crossbow (further/flatter/slower than the bow — a new
  ranged identity in four lines) and the Arbalist (new troop from parts
  on the shelf), fielded by Patrol and Sarleon — appended handles, so
  nothing moved; the AI archery, arms counter, loot tables and paper-
  doll all picked them up with zero code changes, which is the
  interaction claim made concrete.

- [x] **V2. Iteration two — the land raises sons.** The cross-system
  interaction asked for, made real: every settlement carries a recruit
  pool of prosperity/25, refilling one a dawn — recruiting drains it,
  the tavern names it, and a raided village genuinely has no spears to
  give until it prospers again (war → economy → manpower, one loop).
  Plus click feedback: the gate menu shows the last action's result
  right in the panel. `rpool` save tag, `pool=` in the town dump,
  `tests/recruit_pool.txt` drains Sargoth dry and watches a son come of
  age. Flat TODO(balance).

- [x] **V3. Iteration three — a floor under the small print.** HUD text
  requested at 14+ never renders below 19 px (deliberate fine print and
  zoom-encoded map text pass through), party labels join the settlements
  at constant screen size, and two more registries prove out in one line
  each: Timber (a seventh ware the villages fell cheap and the towns pay
  dear) and the Sawmill enterprise. Markets, caravans, world events and
  the ledger picked both up with zero code changes.

- [x] **V4. Iteration four — sky, live buttons, events touch manpower.**
  Battles get a real sky (zenith deepens, horizon pales, follows the
  day clock — one gradient rectangle). Gate-menu rows that can't act
  grey out (swear when sworn/at home, garrison rows at foreign towns) —
  buttons that look alive ARE alive. And `EventDef::poolDelta` threads
  events into manpower: a harvest brings sons of age early (+2 pool), a
  murrain and a bandit king drain them — verified: Praven's pool 4→6 on
  the day-one harvest. Flat TODO(balance).

- [x] **V5. Iteration five — the moneylender.** The sprint's own example,
  made real: [D] at any market deposits 100 (Shift+D withdraws), the
  account earns 5% a week, and 200+ banked at a town is *investment* —
  +1 prosperity a day there, which the living market turns into prices,
  the pool into recruits, the ledger into income. Interest → prosperity
  → prices, concretely. `bank` save tag, harness `deposit`/`withdraw` +
  `bank:` dump, `tests/bank.txt` round-trips an account through a
  reload. Flat TODO(balance).

- [x] **V6. Iteration six — tooltips for the roads.** Hovering any party
  opens its card: the lord or faction (border in their colour), strength
  and current business, hostile-or-not at a glance, and a caravan's
  freight count — the freight bandits can smell, now the player can too.

- [x] **V7. Iteration seven — the field wears grass.** 150 deterministic
  ground props per battlefield (tufts and stones, seeded by the terrain
  so the same field always dresses the same), drawn only within the
  lodDistance setting of the hero, skipped in the arena. Bench: 33.8 ms
  at 2000 soldiers — free.

- [x] **V8. Iteration eight — industry reshapes the town.** Every
  enterprise now names the good it makes (mill→grain, smithy→tools,
  dyeworks→wool, sawmill→timber): owning one turns that ware into local
  produce — priced at source (70%), produced daily by the living
  market's makes-check, hauled by caravans, taxed by prosperity. Applied
  idempotently each dawn, so it survives loads for free. Verified:
  Sargoth's mill flipped grain 130%→70% and the shelf began to fill.

- [x] **V9. Iteration nine — deposits carry their wars.** Coin banked in a
  town that falls to a crown at war with you is seized with the vault
  (the enterprise rule, extended) — banking pays best in safe heartland
  and gambles in border towns, which is exactly the geography the whole
  economy already speaks.

- [x] **V10. Iteration ten — dead rows say why.** Greyed gate-menu rows
  carry their reason inline, right-aligned in quiet gold: "(already
  sworn)", "(your own seat)", "(not your walls)", "(the wall is bare)".
  A button that can't act now teaches instead of ignoring you.

- [x] **V11. Iteration eleven — engines are built of timber.** Ladders
  want 2 timber, a tower 5 — drawn from the saddlebags first, shortfall
  bought from camp sutlers at 8 gold a beam, refused clean if you lack
  both. V3's toy ware now has a war to fight in: fell it cheap in
  villages, mill it at your sawmill, and haul it to the walls you mean
  to take. Flat TODO(balance).

- [x] **V12. Iteration twelve — the field remembers.** Dark stains mark
  where every soldier fell, flat on the ground for the rest of the
  battle (capped at 240 so a rout doesn't paint the plain). A fight's
  story stays readable in the dirt — where the line held, where it
  broke.

- [x] **V13. Iteration thirteen — armies mend under a roof.** The wounded
  cart heals two per line at dawn (instead of one) when the warband is
  inside or within 60 u of a settlement at peace with you — recovery
  now has geography, and holding towns is how campaigns are sustained.
  Flat TODO(balance).

- [x] **V14. Iteration fourteen — the attributes awaken.** Every attribute
  gets exactly one reader: STR +5%/pt hero swing damage (through the
  BattleSetup snapshot), AGI +2%/pt battle footwork, INT sharpens the
  moneylender's weekly rate, CHA +1/pt party cap. The character sheet
  names the effects instead of apologising. Flat TODO(balance).

- [x] **V15. Iteration fifteen — armour awakens.** Every piece carries an
  identity-tier soak at last (cloth 1, iron helm 3, mail 4, plate 6...),
  flowing through the LoadoutArmor→ApplyArmor pipeline that waited since
  the combat model shipped. Knights are finally hard to kill, the
  paper-doll and forge arms matter defensively, and looted plate is a
  prize. Tiers are identity; exact numbers TODO(balance).

- [x] **V16. Iteration sixteen — a blade's weight is its character.** The
  mirror of V15: weapon damage tiers by identity now that armour soaks —
  greatsword 14 and dane axe 13 crack plate, war axe 11, sword 10, spear
  9 (reach is its virtue), crossbow 12 punches where the bow's 8
  harasses. Weapon choice vs armour class is now a real matchup, for
  the player's swaps and the AI's per-range picks alike. TODO(balance).

- [x] **V17. Iteration seventeen — colours and the crowd.** The hero's
  horse wears a blue caparison (being mounted is unmistakable — U11's
  visual note closed), and a tournament championship warms the host
  crown +5 standing (K3's follow-up closed; the host captured before
  the bracket clears the settlement, caught in review).

- [x] **V18. Iteration eighteen — quests carry any ware.** `QuestDef`
  gains `goodId`: delivery quests name their cargo from the goods
  registry (proof: "Timber for the Walls", one line). The quests
  scenario was re-anchored twice en route — the third quest shifted the
  giver rotation, and the V15/V16 combat physics turned seed 5's blind
  hunt into a defeat until the script steered at prey its own size:
  the suite's meaning re-verified, not just its pass-marks.

- [x] **V19. Iteration nineteen — loot turns to coin.** Pick a piece up in
  the bag and press S inside any settlement: sold for 25 gold (half the
  forge's price). The last unclosed circle — fight → loot → sell →
  recruit — now runs, and battlefield plate is income as well as
  armour. Flat TODO(balance).

- [x] **V20. Iteration twenty — the gate menu dashboards.** The header
  shows your vault balance when coin sits here, and the tavern row
  carries the live recruit pool inline — every number the door leads to,
  visible before you open it.

- [x] **V21. Iteration twenty-one — rain country on the map.** The
  positional weather formula battles already use (R1) now hatches the
  campaign map in faint blue — archery weather is plannable before you
  give battle. Baked into the cached map texture: zero runtime cost.

- [x] **V22. Iteration twenty-two — strays sold, soak softened.** Winning
  a field rounds up its masterless horses at 30 gold a head (through
  `BattleOutcome::horsesTaken` — the horse arc closes into the economy).
  Found and fixed en route: V15's plate plus the flat-1 damage floor
  made knights literally unkillable by low-tier troops (battles
  stalled); `ApplyArmor` now floors at 25% of the raw blow — knights
  fearsome, not immortal (verified ~90 s blind fight, then victory with
  the roundup paying 90 on three mounts). TODO(balance): the curve.

- [x] **V23. Iteration twenty-three — money abroad on the books.** The
  kingdom ledger gains a DEPOSITS section: every vault holding your
  coin, the total at interest, and a red AT RISK flag on any account
  sitting in a town your enemies now hold — the V9 seizure rule made
  visible before it bites.
- [x] **V24. Iteration twenty-four — name the foe, weight the blow.** The
  battle-opening banner now names who stands against you (LORD so-and-so,
  a crown's war-band, THE GARRISON OF a town) with the odds spelled out,
  and the hero's landed hits play a thud whose volume scales with the
  damage that actually got through armour — a glance taps, an overhead
  through plate lands like a hammer. `foe=` in the harness battle dump.
- [x] **V25. Iteration twenty-five — the road audience (S4 closed).** The
  last pre-V backlog item ships: T hails the nearest friendly lord party
  on the map into the full court dialogue — swear to the crown whose lord
  actually stands before you, take work from the nearest hall, and castle
  courts now name the seat's own fief-holder. The dialogue state machine
  grew a `parleyParty` route so every settlement-bound topic degrades
  gracefully on the road.
- [x] **V26. Iteration twenty-six — courting lords with gold.** TryRebel's
  "Court them first" finally has a verb: topic [7] in any lord audience
  gives a 100-gold gift for +10 opinion — the same number defection and
  rebellion already read, so gold now buys a following. Audiences show
  the lord's live opinion under his name, and a parley greeting warms or
  chills with it. `tests/court_gift.txt`.
- [x] **V27. Iteration twenty-seven — dialogue topics are buttons.** The
  audience screen's topics now click: DialogueDraw records each row it
  draws, a gather-side hit-test (`DialogueOptionAt`) turns clicks into
  the same menuChoice intents the keys send, and the row under the mouse
  glows gold. Keyboard still works; headless play untouched (the hit
  cache only fills when drawing).
- [x] **V28. Iteration twenty-eight — the Free Company.** Extensibility
  proof at faction scale: a new weapon identity (the pike, 5.2 reach —
  longest on any field, slow to reset), the Free Pikeman who carries it,
  and a whole sellsword faction (gold banners, Lord Ostrec, pike/foot/
  crossbow roster) — all from registry entries in `content.cpp`, zero
  code changes. The blanket outlaw wars make them bandit hunters
  automatically; verified roaming, holding a town, and besieging
  outlaws within one world-day.
- [x] **V29. Iteration twenty-nine — steel for sale.** The Free Company
  earns its name: a data-driven `mercenary` faction flag unlocks topic
  [8] in a road parley — 300 gold buys 3 days of contract. The hired
  party shadows your march, joins any battle within 90u as the allied
  line (the existing `battleAllyIndex` path), and the papers expire at
  dawn. Saved/loaded, `merc:` in the harness dump, `tests/mercenary.txt`
  proves 7 men become 157.
- [x] **V30. Iteration thirty — the political map, live.** Every
  settlement radiates a soft gradient of its owner's colour (towns
  widest, castles middle, villages a hamlet's halo) so empires read at
  a glance and the glow moves with every conquest; hostile owners get a
  hot inner ring. Plus an UNDER CONTRACT chip on the campaign HUD
  quoting the hired company, its men, and the days left.
- [x] **V31. Iteration thirty-one — war reaches the shelves.** The user's
  own example, made real: every town carries a `warMarkup` refreshed at
  dawn — +12% on every price per kingdom war its owner fights (capped
  at three fronts; outlaw raiding is weather, not a war economy). Sue
  for peace and the shelves settle by morning; conquest re-prices a
  town overnight. Red WAR PRICES banner in the market, `warmarkup=` in
  the harness dump, `tests/war_prices.txt`.
- [x] **V32. Iteration thirty-two — battle standards.** One bannerman a
  side carries a tall pole-and-pennant in team colour, drawn at any
  distance so the line reads from across the field. When he falls the
  whole side takes a K4 nerve shock, "THE BANNER FALLS!" rings, and the
  colours pass to the nearest living hand — kill the standard again and
  again to break an army's spirit. Arena bouts fight without colours.
  `banners=x/y` in the harness battle dump.
- [x] **V33. Iteration thirty-three — the kick.** Warband's answer to a
  turtled shield: E boots the man in front (2.2u, ~120° arc, on foot
  only) — 1 hp of insult, but a stagger that goes THROUGH shield and
  stun-immunity both, opening him for the real blow. 1.2s recovery so
  it's a tool, not a spam. Harness `kick` command prints boots landed;
  mechanics verified against charging knights.
- [x] **V34. Iteration thirty-four — host your own feast.** Gate-menu row
  [0] in a hall you own lays a 200-gold feast: 2 days on the M5 clock
  (so marriage suits open at YOUR table), +8 opinion for every raised
  lord (the V26 courting currency), +2 prosperity for the host town.
  Greyed with reasons elsewhere ("not your hall") or while any feast
  already holds. `tests/host_feast.txt`.
- [x] **V35. Iteration thirty-five — ink that holds on any ground.** Every
  glyph in the game now carries a drop shadow (dark offset copy, alpha
  tied to the text's own, heavier under titles) — one change in
  `ui::Text`/`ui::Title` covers map labels over snowfields, battle HUD
  over sky, and every menu. Global scale nudged 1.35 → 1.4 per the
  standing "even bigger" ask.
- [x] **V36. Iteration thirty-six — press the captives.** Captives can
  join instead of only ransoming: R on the party screen presses one
  into the line at spear-point — party +1 (cap respected), honor −1
  (the currency N4 opinions and TryRebel already read), refusals in
  character when the train is empty or the band is full. Party screen
  shows the captive count and the price. `tests/press_captives.txt`.
- [x] **V37. Iteration thirty-seven — the commissary.** An army marches on
  its stomach: one grain per 20 men eaten from the saddlebags at every
  dawn. Empty sacks → red HUD warning; from the second hungry dawn the
  greenest men walk home nightly, and a hungry line marches into battle
  at 65% nerve (K4). Ties markets/caravans/harvest events directly to
  army staying-power. Saved/loaded; `hungry=` in the dump;
  `tests/commissary.txt`.
- [x] **V38. Iteration thirty-eight — lords ride to the feast.** Feasts
  become gatherings: while one holds, every lord of the celebrating
  crown steers for the table; arrivals are announced by name, seated
  once per feast (`feastGuests`, saved as `guest` tags), and a lord at
  YOUR feast warms +4 opinion — so a well-attended feast is bulk
  courting and the marriage window has real witnesses. `guests=` in
  the feast dump; `tests/feast_guests.txt`.
- [x] **V39. Iteration thirty-nine — the field re-arms you.** G over a
  fallen man (2.5u) takes up his weapon in place of your active one —
  lose your blade to a duel of attrition and finish the fight with a
  dead knight's spear. Each corpse yields once (`looted`); a quiet
  "[G] take up the fallen weapon" prompt appears in reach and a gold
  TAKEN UP caption confirms. Harness `pickup`; verified spear-for-sword
  off a fallen knight.
- [x] **V40. Iteration forty — cull what the camera can't see.** Frustum
  culling for the battle draw: soldiers, corpses, blood stains and
  ground props meaningfully behind the camera (dot < −0.25 beyond a
  12u safety bubble) are skipped before any GPU work. The 2000-man
  bench drops 33.8 → 20.5 ms avg (p99 40.3 → 25.7) — a 40% frame-time
  win from the first entry on the GPU roadmap. Draw-only; standards
  stay visible at any angle.
- [x] **V41. Iteration forty-one — send them in without you.** N during
  deployment (or the horn's grace, headless) auto-resolves the battle
  on paper: expected strength from steel + armour, nobody's blade-work
  counted — so it's always bloodier than a fight you lead (0.55 loss
  factor, beaten side ruined; TODO(balance)). Losses land on real
  soldiers, so loot/captives/horses/banners all flow the normal path;
  a lost paper battle carries you off at 1 hp. Harness `autoresolve`;
  `tests/autoresolve.txt`.
- [x] **V42. Iteration forty-two — quarter.** A beaten side can yield:
  routed at a fifth of its strength, colours-in-the-mud at a third, or
  a bloodied remnant facing six-to-one on the spot — "THEY THROW DOWN
  THEIR ARMS!" rings and every man passes whole into your prisoner
  train ("Yielded: N", stacking with the slain-share captives and
  feeding ransom and V36 pressing). Small warbands (< 5) are exempt
  and fight to the end. `tests/surrender.txt`.
- [x] **V43. Iteration forty-three — the cost of a lost field.** Defeat
  has teeth beyond casualties: the victors strip a fifth of your purse
  (min 25), free every captive in your train, and plunder half of
  every ware in the saddlebags — so trading runs and prisoner trains
  carry real risk, the mirror of V42's quarter. All spelled out in the
  battle report. `tests/defeat_cost.txt` on a doomed fixture.
- [x] **V44. Iteration forty-four — night falls on the archery.** The
  campaign clock now changes how battles fight, not just how they
  look: at night archers engage at ×0.55 range and nock ×1.25 slower
  (stacking with rain's wet strings) — attack after dark to blunt a
  crossbow line. The hour is saved (`clock` tag), `night=` joins the
  battle dump, `tests/night_battle.txt` on a midnight fixture.
- [x] **V45. Iteration forty-five — a true night sky, and paid stripes.**
  Fixed a real sky bug: an unconditional day gradient painted noon over
  midnight — night now gets a dark gradient, 140 hashed stars, and a
  crescent moon; dusk/dawn get their own tints. And promotions cost the
  quartermaster: +20 gold per stripe on top of the XP, quoted on the
  party screen. Repaired `tests/veterancy.txt` (0-based slot; it had
  silently never promoted) — it now asserts the full arc.
- [x] **V46. Iteration forty-six — quality eats gold.** Wage identity
  tiers (the V15/V16 pattern applied to upkeep): recruits and brigands
  1/day, line foot 2, veterans and shooters 3, huscarls 4, knights 6 —
  a knight's horse and plate cost six recruits' bread, so elite armies
  drain the ledger the party screen and purse already display. With
  V45's paid stripes, the whole army-quality ladder now runs on gold.
- [x] **V47. Iteration forty-seven — sellswords in the taproom.** Gate
  menu row [J] (towns only): a 5-man pack for 150 gold up front —
  instant manpower past the recruit pool, rotating pikeman/arbalist/
  infantry by town+day. Cap respected, refusals in character, greyed
  outside towns ("they drink in towns"). With V46 wages the trade-off
  is honest: cheap to raise, costly to keep. `tests/sellswords.txt`.
- [x] **V48. Iteration forty-eight — the shield wall.** Formation [5]
  extends the documented FormationType hook: the player's own foot pack
  shoulder-tight (×0.7 spacing), soak a third of ALL damage (arrows and
  steel), march at ×0.6 and swing ×1.3 slower — braced, slow, hard.
  Mounted men and allies are exempt; strategy menu lists it; `form=`
  joins the battle dump. `tests/shieldwall.txt`.
- [x] **V49. Iteration forty-nine — expand the works.** The same B at a
  market where you already own the enterprise sinks the build cost
  again and doubles the daily take (lvl 2, capped; "the works run at
  full stretch already" past that). Level survives save/load (`elvl`
  tag), quoted in the market line, `lvl=` in the dump. Deepens the
  gold → passive income → army upkeep chain. `tests/enterprise_expand.txt`.
- [x] **V50. Iteration fifty — the chronicle.** The reign writes its own
  history: conquests, oaths, crowns, weddings, rebellions, hosted
  feasts, mercenary contracts and lost fields each add a day-stamped
  line to a capped chronicle — shown as the right-hand column of the
  kingdom ledger (last 10, newest first), saved as `chron` tags,
  dumped as `chron:`. Fifty iterations of systems, and now the game
  remembers them. `tests/chronicle.txt`.
- [x] **V51. Iteration fifty-one — fortify the walls.** Gate row [F] in
  your own settlement: 500 gold of wall-work, forever — +10 garrison
  cap (daily muster fills the new beds) and AI sieges face ×2.2 wall
  strength instead of ×1.7. The work stays with the settlement whoever
  takes it, so fortifying a border town is arming your enemy's future
  prize. FORT in the town dump, chronicled, saved (`fort` tag).
  `tests/fortify.txt`.
- [x] **V52. Iteration fifty-two — the stone bites you too.** V51's
  wall-work now reaches the player's own assaults through the battle
  bridge: the opening banner names THE GARRISON OF X (FORTIFIED), and
  wall archers loose 20% faster from the better platform (murder holes
  and good stone), on top of the bigger fortified garrison you must
  cut through. `tests/fort_siege.txt` on a fortified Praven fixture.
- [x] **V53. Iteration fifty-three — peace pays, war grinds.** Dawn
  drift on prosperity: no kingdom wars → +1/day (cap 150); at war →
  −1 every other day (floor 60). Since prosperity already prices
  shelves, fills recruit pools and pays incomes, peace is now a
  compounding investment and long wars hollow out both sides. Plus
  fortified stone gets its look: darker wall, timber hoarding band.
  `tests/peace_prosperity.txt`.
- [x] **V54. Iteration fifty-four — companion perks.** Companions grant
  data-driven party bonuses via a `perk` string on TroopDef: Rega the
  surgeon (3/4 of the fallen live, not 1/2), Malin the scout (+15%
  march), Torva the quartermaster (grain feeds 30, not 20). One
  `HasPerk` helper, three effect sites, `perks:` in the dump — a new
  perk is a string plus one hook. Makes each hire a strategic pick,
  not a stat stick. `tests/perks.txt`.
- [x] **V55. Iteration fifty-five — the tax lever.** T on the kingdom
  ledger cycles LIGHT/CUSTOMARY/HEAVY: heavy = +35% settlement income
  but your towns wither (−1 prosperity every other day, on top of the
  V53 drift); light = −25% income and the land blooms. Prosperity then
  feeds prices, recruit pools and income itself — a compounding policy
  choice, saved (`tax` tag), `tax=` in the dump. `tests/tax_lever.txt`
  pins 50 → 67 → 37 income.
- [x] **V56. Iteration fifty-six — the ledger clicks.** Kingdom-ledger
  war rows (sue for peace) and the tax lever are now mouse buttons via
  the V27 record-and-hit-test pattern (per-hit x, two columns) — and a
  V50 layout bug fixed: the chronicle overlapped LORDS AFIELD; it now
  sits beneath the wars with a screen-bottom clamp.
- [x] **V57. Iteration fifty-seven — Sereth the Drillmaster.** A fourth
  companion, and the V54 perk hook proven end-to-end: her presence
  drills every fielded troop type +5 XP at each dawn (companions
  excluded) — time trains the line, paid stripes cash it in. One
  registry block plus a three-line day-tick hook.
  `tests/drillmaster.txt` on a clock-59 fixture pins the exact +5.
- [x] **V58. Iteration fifty-eight — deployment ghosts.** During the
  planning pause (and whenever the ~ strategy menu is open mid-fight),
  a translucent blue disc marks every own soldier's formation slot —
  computed by the same anchor rules the AI obeys, so what you see is
  exactly where they'll stand. Shape, ranks and orders become visual
  dials instead of imagined ones. Draw-only; bench holds ~20.8 ms.
- [x] **V59. Iteration fifty-nine — the giver's clock.** Quests carry
  deadlines (`QuestDef.days`: hunt 6, deliveries 8; 0 = untimed): the
  clock ticks at dawn, rides in saves, shows as `days=` in the dump —
  and an expired task costs the reward plus −2 relation with the
  giver. Contracts are commitments now, not souvenirs.
  `tests/quest_deadline.txt` expires a 1-day hunt on a clock-59 fixture.
- [x] **V60. Iteration sixty — the task in sight.** A standing TASK chip
  on the campaign HUD quotes the active quest, its progress or cargo,
  and the days left (red under 48 hours) — and the destination town
  pulses a gold ring on the map. A deadline you can see is a deadline
  you can keep. Draw-only.
- [x] **V61. Iteration sixty-one — the field wears its volleys.** Every
  arrow that lands stays: a shaft standing in the dirt at its arrival
  angle with a pale fletching knob, capped at 240 and frustum-culled.
  After an archery duel the ground reads like a pincushion — the
  battle's history written on the terrain, like the blood stains
  before it. Bench holds 20.4 ms.
- [x] **V62. Iteration sixty-two — the storm.** A living weather cell
  drifts across the map at each dawn (bouncing off the edges, saved):
  inside its 250u reach travel runs at ×0.75 mud pace and every battle
  fights in forced rain via a `rainOverride` over the battle bridge —
  ride around it, or drag a crossbow-heavy enemy into it. Breathing
  grey cell on the map; `storm:` in the dump. `tests/storm.txt`.
- [x] **V63. Iteration sixty-three — the storm flattens the fields.**
  Weather reaches the economy: a settlement under the cell at dawn
  loses 2 prosperity and 2 grain stock, the scarcity rule prices the
  loss into its shelves by morning, and thinner prosperity means a
  thinner recruit pool tomorrow. Announced in the news.
  `tests/storm_crops.txt` parks the storm on Sargoth.
- [x] **V64. Iteration sixty-four — hours and weather you can stand in.**
  The walkable town scene's sky now follows the campaign clock (noon,
  amber dusk, rose dawn, deep night — with a warm lit window on every
  building after dark), and riding inside the storm rains on the map
  screen itself: streaks and a leaden dim. Draw-only.
- [x] **V65. Iteration sixty-five — the strategy menu clicks.** The last
  keyboard-only UI: the ~ battle menu's formation rows (1–5) and new
  order rows (Hold/Follow/Charge, with the current one marked) are
  mouse buttons with hover glow; opening the menu shows the cursor,
  closing it returns to mouse-look, and a menu click never spends a
  swing. V27 pattern, battle-side.
- [x] **V66. Iteration sixty-six — the war horn.** V in battle (30s wind,
  not in the ring): +40 nerve to every friendly heart on the field, and
  routed men whose nerve recovers turn back — "RALLY TO ME! THE LINE
  RE-FORMS!" The player's answer to the K4 morale spiral: banners fall,
  lines crack, and now a leader's voice can stitch them back once in a
  while. Harness `warcry`.
- [x] **V67. Iteration sixty-seven — every door on one battle line.** The
  T7 key-footer rule brought to battle: a dark band lists horn, kick,
  scavenge, swap, mount, orders and strategy — and the right edge
  carries live readiness chips for the two cooldown moves (HORN READY
  in gold, or the seconds left). Draw-only.
- [x] **V68. Iteration sixty-eight — the storm goes moddable.** map.cfg
  gains `storm RADIUS DRIFTX DRIFTY [STARTX STARTY]` — the weather
  cell's reach, per-dawn drift and spawn point are mod data now
  (MODDING.md documents it; `InStorm` and the map draw read the
  config). Two storms of house rules away from a monsoon world.
- [x] **V69. Iteration sixty-nine — see the wagons through.** A fourth
  QuestType, Escort: taking the task spawns a REAL travellers convoy
  (guards, caravan brain, actual freight rules) rolling for the
  destination — arrival completes the quest, its death fails it
  instantly at −2 relation. The quest rotation widens to %4; the V59
  clock and V60 HUD chip apply unchanged. Meaning-checked quests.txt
  through the shift. `tests/escort_quest.txt`.
- [x] **V70. Iteration seventy — the street knows.** Talking to villagers
  and guardsmen adds a rumor drawn from live state: a real kingdom war,
  the current feast, which town the storm sits over, or the local cheap
  ware — rotated by day. The small folk become an information system
  fed by the same state machine everything else runs on.
  `tests/rumors.txt`.
- [x] **V71. Iteration seventy-one — wood on your arm.** The hero carries
  a shield by the same rule as his men (one-handed blade = shield),
  with hit points of its own: blocking with wood halves what a bare
  guard lets through and eats arrows at ×0.25 — but every hit wears it
  and "YOUR SHIELD SPLINTERS!" mid-fight drops you to steel-only
  blocking. Q-swapping is now a real trade (greatsword damage vs the
  wall on your arm). Shield bar over the HP bar; HUD lifted clear of
  the V67 footer it overlapped.
- [x] **V72. Iteration seventy-two — lettering in the player's hands.**
  The settings screen gains row [6] Lettering: 100/120/140/160%,
  applied live via `ui::SetTextScale` and persisted as `textscale` in
  settings.cfg (overriding fonts.cfg at startup). The standing
  "bigger text" ask becomes a dial the player owns.
- [x] **V73. Iteration seventy-three — turn his coat.** A crowned ruler
  in a road parley offers a foreign kingdom's lord topic [9]: at +20
  effective opinion he defects with his whole host (−25 with the crown
  he leaves; chronicled). Gifts, feasts, honor — the opinion economy
  now buys armies wholesale, the peaceful mirror of rebellion.
  `tests/poach_lord.txt` on a crowned fixture.
- [x] **V74. Iteration seventy-four — the sword cuts both ways.** A lord
  of YOURS at −15 effective opinion or worse abandons your banner at
  dawn — host and all — preferring a crown at war with you (one
  betrayal a day, chronicled, news-lined). Pressed captives, broken
  oaths and forgotten gifts now have a price named in armies. The
  probe caught him defecting and immediately fighting his old
  comrades. `tests/scorned_lord.txt`.
- [x] **V75. Iteration seventy-five — battle size and the next wave.**
  Warband's battlefield cap: field battles spawn at most `battleSize`
  per contingent (settings row [7]: 100–500, default 200); the
  overflow waits as reserves and marches on in waves of ≤20 when a
  side thins below 60% ("YOUR NEXT WAVE ARRIVES!"). Unspawned
  reserves are credited back as survivors in the loss books; sieges
  and the arena spawn whole; the bench exempts itself.
  `reserves=` in the dump; `tests/battle_size.txt` fields 307 men.
- [x] **V76. Iteration seventy-six — the hundredth script.** A reserves
  chip on the battle HUD (yours/theirs, gold, only when waves wait) —
  and `tests/grand_tour.txt`, the suite's 100th script: one unbroken
  career sweeping commissary, taxes, feast, sellswords, war horn,
  auto-resolve and defeat costs in a single run, tripping over any
  state-machine snag between systems. Suite: 100/100.
- [x] **V77. Iteration seventy-seven — darker bands, slipping ropes.**
  Contrast sweep on every battle overlay (intro/rout/knockout bands
  0.55–0.6 → 0.65–0.75 black; strategy panel 0.62 → 0.85) so banner
  text holds against noon snow. And captives slip the rope: a train
  larger than half the warband loses one prisoner per dawn — ransom
  fast, press, or grow. `tests/prisoner_escape.txt`.
- [x] **V78. Iteration seventy-eight — Hodd the Jailer.** Fifth companion,
  the V77 counter, pure V54 pattern: perk=jailer means no captive ever
  slips the rope and the broker pays 15 a head instead of 10. One data
  block, two one-line hooks. `tests/jailer.txt` holds 6 captives
  through the dawn that leaked one without him.
- [x] **V79. Iteration seventy-nine — the tavern names its guest.** Gate
  row [5] now quotes who is actually drinking here, their perk and
  price ("Hire Malin Longeye (scout, 100 gold)"), and greys with
  "(already yours)" once hired — companion shopping becomes a map
  decision instead of a slot machine. Draw-only.
- [x] **V80. Iteration eighty — the world builds too.** AI kingdom towns
  at prosperity 130+ fortify themselves at dawn (one work-site a day,
  −20 prosperity, news-lined) — the same V51 walls the player buys, so
  crowns you leave in peace grow harder to storm and V53's peace
  growth now arms your future enemies. `tests/ai_fortify.txt`.
- [x] **V81. Iteration eighty-one — stone you can see from horseback.**
  Fortified settlements wear a ten-stud stone ring on the map and a
  gold FORTIFIED note in the hover tooltip — siege planning reads at a
  glance now that the world walls itself. Draw-only.
- [x] **V82. Iteration eighty-two — the destrier.** W at a town market:
  200 gold, once, for life — the hero's horse carries double HP in
  every battle after (one bool over the bridge; horse=120 in the
  dump), chronicled and saved. A permanent gold sink that makes the
  cavalry identity durable. `tests/destrier.txt`.
- [x] **V83. Iteration eighty-three — companions from a cfg.** The perk
  system joins the modding surface: `assets/companions.cfg` mints
  heroes-for-hire with no code (`companion id Name perk temper cost`),
  perks reusing the V54 hook strings. Ships with Wren the Swift
  (scout) as the live sample; MODDING.md documents the line.
  `tests/modded_companion.txt` hires her from town 5's tavern.
- [x] **V84. Iteration eighty-four — the loan.** The moneylender lends
  both ways now: L at a market borrows 300 against 350 due in ten
  days (L again repays). A missed date compounds the debt by a
  quarter, costs a point of honor, resets the term, and is
  chronicled; a red DEBT chip rides the HUD. Borrowing funds a
  sellsword pack TODAY at the price of tomorrow — the interest
  example, borrower's side. `tests/moneylender_loan.txt`,
  `tests/debt_compounds.txt`.
- [x] **V85. Iteration eighty-five — troops from a cfg.** The soldier
  catalogue joins the modding surface: `assets/troops.cfg` mints
  types (`troop id Name weapon armor wage cost [mounted]`) and
  enlists them (`recruit faction troop`). Ships live with the
  Halberdier in Patrol's levy; wage tiers, save ids and rosters all
  pick modded types up automatically. `tests/modded_troop.txt`.
- [x] **V86. Iteration eighty-six — the lender's men ride.** Debt past
  700 at a missed date spawns Lord Graves and ~25 collectors under
  the raider banner, hunting the player like any prize — another band
  per further miss. Financial risk becomes physical risk; beat them
  and the quarter/loot rules apply as ever (the debt remains).
  `tests/debt_collectors.txt`.
- [x] **V87. Iteration eighty-seven — pay the man.** Graves is the one
  hostile party you can hail: topic [0] in his parley settles the debt
  on the spot — full sum counted out in the road, the band turning
  for home, chronicled. Fight the collectors, outrun them, or pay
  them: the debt arc has all three exits. `tests/pay_graves.txt`.
- [x] **V88. Iteration eighty-eight — the feed.** A battle ticker,
  bottom-right, newest first, fading over six seconds: your kills by
  troop name, banners falling, waves arriving, shields splintering,
  quarter given — the fight's story readable without taking your eyes
  off the melee. Draw-only; bench 20.2 ms.
- [x] **V89. Iteration eighty-nine — the news keeps a short memory.**
  Dawns fire several events at once and each overwrote the last on the
  single news line (watched it happen all sprint: harvest news eating
  debt notices). Now every change to the line also stacks the previous
  one into a fading four-line feed below the status chips — nothing is
  lost to the next announcement. Draw-only, zero refactoring of the
  ~60 resultText writers.
- [x] **V90. Iteration ninety — events from a cfg.** World events join
  the modding surface: `assets/events.cfg` lines enter the day
  rotation beside the built-in three (name, news template, prosperity/
  stock/party/pool deltas). Ships live with Fair Day; verified firing
  on its rotation slot. `tests/modded_event.txt`.
- [x] **V91. Iteration ninety-one — your lords earn their fiefs.** When
  an AI siege resolves against a player town/castle, a raised lord of
  yours within 400u throws half his host onto the walls: relief
  strength joins the defense roll, his men bleed for it, his opinion
  rises +3 for serving well. Raising lords and granting fiefs now buys
  a standing defense, not just a banner count. `tests/own_relief.txt`.
- [x] **V92. Iteration ninety-two — time to ride home.** Sieges of the
  player's settlements burn ×2.5 slower (30s vs 12s, verified side by
  side in one dump), and every invested settlement pulses a red ring
  on the map — the alarm is a summons, not an obituary: ride into the
  besieger to break the camp, or trust the V91 relief and V51 stone.
- [x] **V93. Iteration ninety-three — a lord will not wait forever.**
  Every fourth day the longest-held captive lord escapes at dawn:
  respawn queued, −5 opinion (he remembers the rope), chronicled.
  Captured lords become a decision with a clock — ransom for 200,
  release for honor and friendship, or lose both options to the
  night. `tests/lord_escape.txt`.
- [x] **V94. Iteration ninety-four — the circlet.** A crowned ruler
  looks the part: a thin gold band rides above the hero's helm in
  battle (one presentation bool over the bridge) and a three-point
  crown tops the map banner. Draw-only.
- [x] **V95. Iteration ninety-five — the whole economy from cfgs.**
  Goods and enterprises join the modding surface (`goods.cfg`,
  `enterprises.cfg`) with a chained sample: Ale, and the Brewery
  that makes it — spreads, scarcity, caravans, war markups and V49
  expansion all apply automatically. Every content registry is now
  moddable. `tests/modded_good.txt`.
- [x] **V96. Iteration ninety-six — the legacy screen.** Victory closes
  like a saga: reign stats (renown, honor, gold, lords under the
  banner) and the chronicle's last eight entries centered under THE
  LAND IS YOURS — every one written by a real event of the campaign
  that just ended. Draw-only.
- [x] **V97. Iteration ninety-seven — the fall of the house.** The
  destroyed-warband moment gets the V96 treatment in a darker key: a
  full overlay with the reign's numbers and the chronicle as far as
  it goes, over "[R] Raise a new banner". Same restart flow
  underneath, draw-only.
- [x] **V98. Iteration ninety-eight — steel from a cfg.** Weapons join
  the modding surface (`weapons.cfg`: class, damage, reach, ranged
  params), loading before troops.cfg so the chain closes: the shipped
  Warhammer is swung by the shipped Hammerman in Vaeling's levy — a
  modded troop wielding a modded weapon, zero code.
  `tests/modded_weapon.txt`.
- [x] **V99. Iteration ninety-nine — plate from a cfg.** Armour joins,
  completing the kit: `armor.cfg` (slot + soak tier) loads before
  troops.cfg; the shipped Brigandine (body 5, between mail and plate)
  rides a fixture inventory by id. Every piece of content the combat
  model consumes is now mintable from assets/. `tests/modded_armor.txt`.
- [x] **V100. Iteration one hundred — the saga.** A campaign's ending
  writes its whole story to `saga.txt` beside the saves: the headline
  (victory or the fall), the reign's numbers, and the full chronicle —
  a keepsake authored entirely by the hundred systems that lived it.
  Written once per ending, on victory and on the warband's
  destruction alike. `tests/saga.txt` grinds the doomed fixture to a
  wipe and finds the artifact.
- [x] **V101. Iteration one-oh-one — the lord in person.** Fighting a
  lord party puts HIM on the grass: the toughest enemy becomes a
  ×2.5-health champion under a gold circlet who never yields with the
  mob. His fall shocks the host at twice a banner's weight, feeds the
  kill feed, and a won field pays +3 renown plus a chronicle line.
  Found and fixed a last-tick blind spot (a lord dying as the battle
  ends now still counts). `tests/lord_champion.txt`.
- [x] **V102. Iteration one-oh-two — single combat.** [D] at deployment
  when a lord waits: the armies freeze while his champion charges YOU.
  Kill him — lines crash, your side +25 nerve atop his host's
  lord-fall shock; fall — the crash comes with your side shaken.
  Fixed the champion politely holding his line during his own duel,
  and purged a stale `-DDMG_DEBUG` from the worktree build cache.
  `tests/duel.txt` fells Gorak in 25 blind overheads.
- [x] **V103. Iteration one-oh-three — careers from a cfg.** troops.cfg
  gains `upgrade <from> <to> [xp]`: promotion ladders are data now.
  Shipped: Halberdier → Hammerman (a cfg troop promoting into a cfg
  troop wielding a cfg weapon). The V45 paid-stripes flow applies
  unchanged. `tests/modded_upgrade.txt`.
- [x] **V104. Iteration one-oh-four — crowns from a cfg.** Factions join
  the modding surface: `factions.cfg` mints them (color, behavior,
  kingdom/mercenary flags), names their lords, and declares wars that
  merge into the base table. Ships live: the Nords, Lord Ragnar,
  at war with Sarleon, fielding cfg hammermen via troops.cfg —
  roamers, hosts and town rotation picked them up on sight. quests.txt
  meaning-checked through the world shift. `tests/modded_faction.txt`.
- [x] **V105. Iteration one-oh-five — the enemy in its colours.** Enemy
  soldiers, banners and silhouettes tint by their faction's colour
  (one Color over the bridge; garrisons wear their owner's) instead of
  generic red — Sarleon fights teal, the Nords red-brown, Vaelings
  green. Friendlies stay blue for instant read. Draw-only.
- [x] **V106. Iteration one-oh-six — allies in their colours.** The V105
  rule completes: an allied contingent fights in ITS faction's colour
  (the hired Free Company gleams gold beside your blue; a skirmish
  side you joined keeps its crown's tint) via a per-soldier tint
  helper. Three-banner fields finally read as three banners. Draw-only.
- [x] **V107. Iteration one-oh-seven — the third standard.** An allied
  contingent raises its own banner in its own colours: its fall shocks
  only the ally line ("Your ally's banner falls"), and the colours
  pass within their contingent. Three standards can now fly and fall
  independently — own, enemy, ally.
- [x] **V108. Iteration one-oh-eight — where the blow came from.** A red
  arc flashes at the screen edge in the attacker's direction (melee
  from the striker's position, arrows from the shaft's back-trail),
  relative to your facing — get hit from behind and the arc says so.
  Draw-only.
- [x] **V109. Iteration one-oh-nine — the books show where you look.**
  Three visibility stitches: the party screen carries the wounded cart
  and the hunger warning; the kingdom ledger's DEPOSITS line carries
  the red DEBT beside it; the gate-menu dashboard quotes FORTIFIED and
  war-price markup. No new state — existing state, surfaced where the
  decision is made. Draw-only.
- [x] **V110. Iteration one-ten — LOD tiers for the far and the fallen.**
  Beyond 2× the LOD line a soldier is one box in his side's colour;
  beyond 1× a corpse is one dark slab (pools and faces only up close).
  Scalability guards for 500-a-side settings and long corpse-strewn
  fights — the 2000-man bench holds 20.2 ms (fill-bound at that
  range), the headroom shows at scale. Draw-only.
- [x] **V111. Iteration one-eleven — THE CROWNS.** The kingdom ledger
  gains the missing diplomacy overview: every kingdom in one list,
  each in its banner colour — holdings, standing with you, AT WAR in
  red or at peace. Confirms in passing that player battles already
  feed war-weariness (no gap found). Draw-only; cfg factions listed
  automatically.
- [x] **V112. Iteration one-twelve — declare war.** THE CROWNS rows are
  buttons: click a peaceful kingdom to open hostilities — hostile
  matrix both ways, −30 relation, chronicled, and V31/V53 price the
  new war into your own shelves by morning. Free captains finally
  choose their enemies. `tests/declare_war.txt`.
- [x] **V113. Iteration one-thirteen — set spears stop horses.** A
  braced polearm (standing, unrouted, unstunned) reverses the trample:
  the RIDER takes 1.5× the weapon's point — AI cavalry reels at double
  cooldown, and the hero's own charge into a set spear bleeds him
  through his horse ("A set spear stops your charge"). Pikes and the
  Free Company finally mean what they look like; cavalry must flank
  or soften with arrows first.
- [x] **V114. Iteration one-fourteen — the war drums.** A procedural
  deep-skin pattern (boom … boom-boom …) loops under battles, silent
  through intro and deployment, swelling with how much of the field
  still fights, gone when it's decided. Synthesized like every other
  sound — no assets, headless no-op.
- [x] **V115. Iteration one-fifteen — outriders, and rain in your ears.**
  The cfg Outrider proves mounted+ranged composes from data (bow +
  mounted flag, riding the same weapon-keyed archery AI), enlisted
  with the Nords; and the campaign map now plays the rain patter when
  you ride inside the storm, matching V64's streaks.
  `tests/horse_archer.txt`.
- [x] **V116. Iteration one-sixteen — Parthian habits.** Mounted archers
  pressed to half their range open it again, loosing as they ride —
  the kiting that makes horse archers horse archers, fixing V115's
  honest note (arrows=0 in the scrum → arrows=4 in the same fixture).
  Foot archers still stand; the counter is corner them or spear the
  horse. `tests/parthian.txt`.
- [x] **V117. Iteration one-seventeen — the hero draws a bow.** A ranged
  weapon in the hero's hands turns hold-and-release into draw-and-loose:
  the shaft flies where the camera looks (full pitch aim), a short draw
  robs it of speed and damage, and it rides the same Arrow sim as every
  AI archer. Crosshair ring tightens with the draw; `shots=` in the
  battle dump. Also fixed a latent save bug: `carry` lines appended to
  the default arsenal on load, doubling the hero's weapons every
  save/load cycle — first carry now replaces wholesale (mirrors ccarry).
  `tests/herobow.txt`.
- [x] **V118. Iteration one-eighteen — the quiver runs dry.** Hero arrows
  are finite: 24 on the hip (TODO(balance)), one spent per loose, an
  empty quiver refuses the shot and points at the ground — [G] pulls
  landed shafts (yours or anyone's) back into the quiver, so an archer
  harvests his own volleys mid-fight. ARROWS count on the HUD, gold
  while stocked and red at zero; `quiver=` in the battle dump.
  `tests/quiver.txt`.
- [x] **V119. Iteration one-nineteen — every quiver runs dry.** AI archers
  carry 24 shafts too (TODO(balance)): each loose spends one, and a dry
  quiver silences the bow — PickWeaponForRange skips ranged weapons with
  no ammo, so archers draw their sidearm and close. Long battles now
  shift naturally from missile exchange to steel. Verified with a temp
  2-shaft quiver: outriders volleyed (arrows=4), ran dry (arrows=0
  thereafter), closed to melee and finished the fight.
- [x] **V120. Iteration one-twenty — you enter a gate by standing at it
  (U-track).** Clicking a distant town no longer teleports the party
  inside: within 48 units the gate opens as before; beyond it the click
  sets a travel course — the party self-steers there with time flowing
  (any manual input takes the reins back) and enters on arrival. Harness:
  `goto N` travels, `enter N` keeps the scripted teleport, `course=` in
  the state dump. `tests/travelcourse.txt`.
- [ ] **U-track (user directive 2026-07-22, priority):**
  - [x] proximity gate on map-click town entry (V120)
  - [x] battle camera: closer to the player and player-adjustable (V121 —
        default shoulder distance 6.0→4.5, mouse wheel zooms 2.2–9.0)
  - [x] remove the yellow swing-direction dots in melee (V121 — the orange
        wind-up arc telegraph is gone; the cocked blade pose reads alone)
  - [x] settlement menu buttons fully wired end to end (V122 — walking-mode
        service chips and tavern recruit/ransom rows are hover-lit click
        targets firing the same intents as their keys; gate menu rows were
        already clickable)
  - [ ] all menus centered / responsive to window size and GUI scale
        (V122: campaign key bar shrinks-to-fit; V123: market screen floats
        centred at any width — layout::MarketX0()/X1() shared by draw and
        hit-test — and global lettering bumped 1.4→1.5; party/character/
        settings/gate menus were already centred; kingdom ledger's fixed
        left column is the remaining offender)
  - [x] remove leftover text behind the bottom-left control panel (V122 —
        the stale travel hint that printed under the key bar is gone; its
        content merged into the bar)
  - [ ] shader / graphics pass (lighting or post effects within raylib)
  - [x] playtest pass on quest-completion UX (V124 — payoff banner with
        fanfare/knell, richer HUD tracker with have/need + distances, and
        a full quest journal screen [Q]: task, progress, reward, clock,
        plus the saved record of every quest taken/done/failed)
  - [x] battle render caps (V125 — stuck arrows 240→80; corpses sink into
        the field after 25s and stop drawing, so long battles no longer
        accumulate an unbounded render bill)
  - [x] AI blocks more, cheaply (V125 — guard discipline: toe-to-toe and
        between swings a soldier holds his block, front-arc hits soaked
        ×0.4 vs AI and hero alike, raised-guard pose on the model; one
        compare per soldier, bench unchanged at 20.30ms)
  - [~] perf hunt (V126 — GPU instancing shipped: every far/far-far tier
        box and far corpse slab batches into one DrawMeshInstanced per
        colour; bench 20.30→19.69ms, 49→51fps at 2000 men. The near-tier
        segmented humanoids (DrawCharacter) are now the dominant draw
        cost — instancing THEM needs per-part transforms and is the next
        big perf ticket)

## Sequencing guidance

User-directive tracks I and J lead: G1+J1 (spatial grid → targeting AI) and I1
(data-driven map) are the current top priorities, with J4 (settings) as a good
standalone session task. E1→E2 next (marketplace was explicitly requested and
unblocks E3/E4/F4-goods quests). F-track after
E-track exists to pay for it. Prefer finishing a track's structure over
starting a new one; prefer one shipped, tested, committed feature per session
over two half-features.

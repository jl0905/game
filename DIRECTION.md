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
- [ ] **E2. Inventory-driven trade loop.** Goods occupy inventory tiles
  (reuse D1 grid); party carry capacity from party size. Sell price differs
  by settlement → the classic buy-low/sell-high caravan loop exists.
- [ ] **E3. Caravans & prosperity.** Faction caravan parties travel between
  owned settlements; arriving raises settlement prosperity (drives income);
  raiding caravans yields goods + relation penalty. Prosperity in saves.
- [ ] **E4. Landowning / enterprises.** Buy a productive enterprise in a town
  (mill, smithy, dyeworks — content defs); daily income scaled by prosperity;
  lost if the town falls to a faction at war with you.

## Track F — Politics & kingdom play

- [ ] **F1. Relations.** Per-lord and per-faction relation scores (int,
  −100..100), moved by battles, ransoms, raids. Shown on a lord/faction
  report screen. Saved.
- [ ] **F2. Vassalage.** Swear to a king: get a settlement fief, owe wartime
  muster (join your liege's siege target); lords react by relation.
- [ ] **F3. Player kingdom.** Rebel or conquer unowned: your own faction
  banner/colour, grant fiefs to hired lords (mercenary captains → vassals),
  other crowns react (F1) — the Warband endgame.
- [ ] **F4. Quests (structure).** A `QuestDef` registry + simple givers
  (guild master in towns, lords in halls): deliver goods, hunt bandits,
  collect taxes. Rewards flat. This gives the early game purpose.

## Track G — Combat & battlefield depth

- [ ] **G1. Battle AI efficiency.** Replace O(n²) `FindNearest` with a uniform
  spatial grid rebuilt per tick (a prior session started this — check
  git/stash before redoing). Target: 1000v1000 playable. Bench before/after
  with `--bench`.
- [ ] **G2. Tournaments.** Arena in towns: bracketed melee rounds with borrowed
  gear, bet gold, renown reward. Reuses the battle module with a
  `BattleSetup` arena flag (no terrain gen, ring walls).
- [ ] **G3. Morale & rout.** Soldiers flee at low team morale (deaths, leader
  down); routed enemies scatter and can be run down. Ends battles without
  killing every last soldier — Warband's decisive-moment feel.
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

- [ ] **I1. Data-driven map.** Move the world map (bounds, biomes, settlement
  positions/types/owners, faction spawn points, roads) out of code into a
  moddable asset file (e.g. `assets/map.cfg`, same spirit as `fonts.cfg`),
  loaded at startup with the current map as the shipped default. Modders can
  then reshape the world with no rebuild.
- [ ] **I2. Bigger map, still fast.** Expand the world meaningfully (more
  settlements, more room between crowns) once I1 lands; profile campaign tick
  and draw at the larger scale (party AI is O(n²)-ish — grid it if needed,
  see G1 for the battle-side twin).

## Track J — Tactical battles & player impact (user directive 2026-07-20)

- [ ] **J1. Targeting AI.** Soldiers pick targets sensibly instead of nearest-
  only: prefer threats attacking them or allies, spread across enemies rather
  than dogpiling one, archers prefer unshielded/high-value targets. Builds on
  G1's spatial grid.
- [ ] **J2. Slower, more tactical pacing.** Structural knobs for pace (swing
  cooldowns, movement speeds, engagement distances) routed through content
  defs so pacing is tunable data (numbers flat, TODO(balance)) — battles
  should read as lines meeting, not instant blenders.
- [ ] **J3. Player impact.** The hero should visibly matter: nearby-troop
  rally/morale aura hooks, killing-blow feedback, order responsiveness —
  fighting well should swing a close battle.
- [ ] **J4. Settings & graphics options.** A settings screen (and
  `assets/settings.cfg`): resolution/fullscreen, draw distance/LOD radius,
  particles on/off, audio volumes, invert-Y, key hints. Graphical upgrades
  (shadows, nicer sky, water) welcome when cheap on the frame budget.

## Sequencing guidance

User-directive tracks I and J lead: G1+J1 (spatial grid → targeting AI) and I1
(data-driven map) are the current top priorities, with J4 (settings) as a good
standalone session task. E1→E2 next (marketplace was explicitly requested and
unblocks E3/E4/F4-goods quests). F-track after
E-track exists to pay for it. Prefer finishing a track's structure over
starting a new one; prefer one shipped, tested, committed feature per session
over two half-features.

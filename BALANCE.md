# BALANCE.md — the tuning worksheet (direction K9)

Every deliberately-flat `TODO(balance)` number in the game, with where it
lives, its current value, and the feel it is meant to produce. **Numbers stay
flat until a human playtesting pass works through this sheet** (project rule:
structure first, tuning later). When you tune one, change it at the listed
site, play it via the harness or by hand, and strike the row.

Line numbers drift; the constant names don't — search by name.

## Core placeholder set (`src/content.cpp`, `namespace base`)

Every def resolves to these unless it overrides them. Tuning here moves the
whole game at once.

| Constant | Value | Controls / intended feel |
|---|---|---|
| `base::TROOP_HP` | 100 | Every troop's max HP — fights should survive ~5-10 clean hits |
| `base::TROOP_SPEED` | 6.0 | Foot troop move speed (mounted ×2) |
| `base::TROOP_COST` | 0 | Recruit price — free recruits are a placeholder, not a design |
| `base::TROOP_WAGE` | 1 | Daily upkeep per soldier — the economic brake on army size |
| `base::WEAPON_DAMAGE` | 10.0 | Every weapon's hit damage — differentiate per class in tuning |
| `base::WEAPON_REACH` | 2.5 | Melee reach (spear 4.0, great/dane 3.2 by identity) |
| `base::WEAPON_SWING` | 0.7 | Seconds between swings |
| `base::ARMOR_VALUE` | 0 | Flat damage soak per worn piece — all zero today |
| `base::ARMOR_WEIGHT` | 0.0 | Encumbrance — no effect yet |
| `base::UPGRADE_XP` | 100 | XP to promote one troop a tier |

Per-def overrides: Short Bow `missileRange 40` / `missileSpeed 30` /
`swingTime 2.0`; companions `cost 100`, `wage 3`; goods all `basePrice 10`;
enterprises all `cost 300` / `dailyIncome 15`; quests hunt `1 band, 100g,
+5 rel`, grain `5 units, 80g, +5 rel`; `lordPartySize` 60 (player) / 120
(all five AI factions).

## Campaign pulse (`src/campaign/campaign.cpp`)

| Constant | Value | Controls / intended feel |
|---|---|---|
| `DAY_LENGTH` | 60 s | Real seconds per world day — the tempo of everything below |
| `SettlementIncome` | 20/50/30 | Daily gold: village/town/castle |
| Fatigue block | 1.0 / 45 / 6.0 / 30 | March fatigue per sec, break-off limit, rest rate, camp reach |
| `LORD_RESPAWN` | 90 s | Downtime after a lord's host is destroyed |
| `LORD_NOTICE_RATIO` | 4 | Lords ignore prey this many times smaller — contempt, not blindness |
| `LORD_RECRUIT_RATE` | 5/day | A camped lord's refill speed (mauled lords sit home until half-strength) |
| `LORD_TRAIN_RATE` | 2/day | A full host's tier-up drilling speed |
| Lord siege gate | garrison×2 < host | When a lord dares invest a settlement |
| `WAR_WEARINESS` | 40 | Casualties that push a war to truce |
| `TRUCE_DAYS` | 4 | How long peace holds before rekindling |
| Garrison sizes | 8 / 4 / 12 | Default / village / castle — both at init and on capture; +1 mustered per day |
| Den breeding | every 2 days, cap 14 | Bandit spawn pressure; dens defend at ×2 troops |
| `lordsRallyDays` / `musterDays` | 3 / 3 | Crown rally duration; days to answer a liege's summons |
| Crown requirement | 2 settlements | Plus −40 liege / −20 other crowns on claiming |
| Relation deltas | −5 win, −10 raid, −20 siege, +10 ally, −40 rebellion, −15 ignored summons, +10 answered | Scattered at `NudgeRelation` call sites |
| `XP_PER_SURVIVOR` / `HERO_XP_PER_WIN` | 25 / 50 | Post-battle experience; level-up at `level × 100` XP |
| Battle loot | 50–150 g | Field and siege gold; 50% chance a beaten foe drops gear |
| Captive share | ~30% | Slain enemies who become ransomable (10 g a head at taverns) |
| Arena purse / stake | 150 / 50 (pays ×3) | Championship payout; bracket 4v4 → 2v2 → duel |

## Economy (`campaign.cpp` + `world.h`)

| Constant | Value | Controls / intended feel |
|---|---|---|
| `PRICE_AT_SOURCE` / `AT_MARKET` | 70% / 130% | The buy-low/sell-high spread that makes trade runs pay |
| Scarcity curve | 4%/unit, par 10, clamp 70–160% | How hard deliveries move prices |
| Merchant's cut | sell = buy × 3/4 | Round-trip cost of trading in one town |
| Production / consumption | +1 / −1 per day, cap 20 | Market metabolism; castles refill to 5 |
| Initial stock | 10 (5 castle) | Shelf depth at world start |
| `CARAVAN_CARGO` | 8 | Freight per convoy (guards: 3) |
| Caravan prosperity | +5/arrival, cap 150 | Prosperity growth; prosperity scales income |
| Bandit freight-scent | ×0.5 distance | How strongly outlaws prefer laden caravans |
| `GOODS_CAP` | 30 | Saddlebag capacity — caps trade-run size |
| Terrain march speeds | forest 0.70, mountain 0.55 | In `assets/map.cfg` (`biome` line) — roads negate |

## Battle feel (`src/battle/battle.cpp`)

| Constant | Value | Controls / intended feel |
|---|---|---|
| `PACE_COOLDOWN_SCALE` / `PACE_MOVE_SCALE` | 1.5 / 0.85 | Soldier-only slowdown — the hero's full speed IS the player impact |
| `RALLY_RADIUS` / `RALLY_COOLDOWN_SCALE` / `RALLY_PULSE_TIME` | 12 / 0.75 / 4 s | Hero aura; a hero kill doubles it for the pulse |
| Nerve block | 100 max, −15 ally death, +10 foe death, radius 8, −20 hero down, ±15 rally, +2/s regen | Per-soldier courage and rout |
| Shields | 40 HP, 35% through, 8 wear/hit | Directional guard strength and lifetime |
| Targeting weights | crowd −2, attacker +4, unshielded +3 | Line spread, self-defence, archer preference |
| `FIST_*` | 10 dmg / 2.6 reach / 0.7 s | Unarmed fallback |
| Arrows | gravity 10, life 4 s, hit 0.9 | Flight feel (flagged feel-not-tuning) |
| Trample | 15 dmg / 1.5 s cd / couched ×2 | Cavalry impact |
| Mounts | 60 HP, 40% hit share | How mortal horses are |
| `ApplyArmor` | max(dmg − armor, 1) | Soak curve — placeholder-simple, min 1 always |
| Hero speeds | foot 7 (3 blocking), mounted 14 (6) | Player mobility vs the 6.0×0.85 soldier |
| Arena fields | 4v4 / 2v2 / 1v1 | Bracket round sizes (`bridge.h`) |

## Structural stubs (fields exist, effects don't)

- `ArmorDef::weight` — no encumbrance effect yet.
- Hero attributes (STR/AGI/INT/CHA) — spendable, zero effect (says so in-game).
- `Town::priceOffset` beyond source/market identity — per-town spreads possible.
- Vassalage oath requirements — only "at peace + standing ≥ 0" today.

## How to run a tuning session

1. Pick a row; change the value at its site (grep the constant name).
2. `./build.ps1` or `cmake --build build -j 8`.
3. Play it: windowed for feel, `--script` (see CLAUDE.md) for arithmetic —
   `tests/` has a scenario per system (market_pulse, lord_recruit,
   tournament_bracket, ...).
4. Strike the row here, note the new value and why, commit both files.

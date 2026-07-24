# OpenWarband — The Manual

How the game works, kept current as mechanics land. (Developers: DIRECTION.md
tracks *what changed when*; this file describes *how it plays now*.
Modding surfaces are in MODDING.md.)

## The campaign map

- **Time is yours.** The world only moves while you do: travel (WASD or hold
  LMB) or hold SPACE to wait. Stand still and everything freezes.
- **Click-to-travel.** Click a town from afar and your party sets a course,
  self-steering there with time flowing; any manual input takes the reins
  back. Within 48 units of a gate, a click walks you straight in.
- **Terrain & roads.** Plains, forest (slower), mountains (slowest). Trunk
  roads (a spanning network, not a tangle)
  (bright sand over dark earth) join nearby settlements — full pace on them
  whatever the ground. A **scout** companion marches the band 15% faster.
- **The storm.** One weather cell drifts the map: travel at 0.75× inside it,
  battles there are fought in rain, and crops suffer where it sits at dawn.
- **The world.** Two crowns - Sarleon and the Vaelings - contest 26
  settlements (towns, castles, villages); you start neutral with both.
  Wars end and begin on their own: exhausted realms make peace, fat ones
  covet. A crown conquered out of its last town fields no more hosts.
  Also present:
  bandit lairs breeding raiders until burned out, lord parties (150 strong),
  caravans, and roaming warbands. Factions war and besiege each other
  without you; two hostile parties that collide fight a **skirmish** you can
  watch or join ([1]/[2]).
- **Keys:** the bottom bar lists everything — [Q] journal, [P]arty,
  [C]haracter, [I] bag, [B] kingdom ledger, [E] estate, [T] hail a lord,
  [O]ptions, [F5–F7] quicksaves, wheel to zoom.

## Settlements

- **The gate menu** greets you: recruit, market, tavern, tournament, work,
  audience, feast, garrison, fortify, sellswords — every row clickable, dead
  rows say why. [W] walks the streets in 3D instead.
- **Walking mode:** the service strip ([T] tournament, [M] market, [G] work,
  [H] hire, [V] oath, [E] talk, [F] garrison) is clickable; the gold-roofed
  building is the tavern (recruits, ransoming captives, companions).
- **Prosperity** (30–150) is a town's lifeblood: it scales market stock,
  recruit pools, enterprise income, and estate field rent. War, taxes,
  storms, and events push it around; caravans and deposits feed it.
- **The road-danger economy:** every town tracks *danger* — hostile bands
  prowling its land. Caravans refuse dangerous destinations, frightened
  markets wither, and patrol factions converge on their endangered towns.
  Prosperity grows from caravans that actually *arrive* (tallied each
  dawn), so burning out a bandit lair genuinely revives a whole region:
  fewer raiders → danger falls → trade returns → the town blooms. Nothing
  pays prosperity directly; the wagons do.
- **Fortify** your own walls (gold) to make their sieges bloodier for the
  attacker. AI towns self-fortify when rich.
- **Garrisons muster from the recruit pool** — the same sons of the land
  you recruit from. A poor, raided, or bandit-strangled town cannot man
  its walls; starve the pool (blockade, raids, danger) and the defences
  thin themselves.

## Economy

- **Goods** trade at living prices: scarce stock costs more, village produce
  is cheap at the source, war adds a markup everywhere the owning crown
  fights. Buy low, haul, sell high; saddlebags cap what you carry.
- **The forge counter** sells two rotating pieces a day (towns only) — and
  every item's price is **derived from its stats** (damage, reach, missile
  range, armour). Loot resells at half that same worth.
- **Troop promotions cost the gear delta**: promoting a man charges exactly
  what the next tier's added equipment and training would cost you at the
  counter (plus 200 for his first horse). The price is quoted on the row.
- **Enterprises:** buy one per town (mill, smithy, brewery…); it pays daily
  by prosperity and makes its good local produce. Expand it for double take.
- **The bank:** deposit in a town (5%/week, big deposits feed prosperity),
  or **borrow** — debt compounds weekly, and at 700+ the lender sends
  Graves and his collectors after you in person.

## Your warband

- **Party cap** = 20 + renown + Charisma (+10 with an estate barracks).
- **Wages** daily, tiered by troop quality; **grain** feeds the band each
  dawn (a quartermaster stretches sacks; an estate granary never runs out).
  Hungry men fight worse and start walking home on the second empty dawn.
- **Experience:** survivors of won battles earn XP per troop type; spend it
  on the party screen ([P]) to promote — costs gold (see economy above).
- **Prisoners:** beaten enemies can yield; ransom them in taverns or press
  them into your line at spear-point (honor suffers).
- **Companions** (tavern hires) are unique heroes with perks — surgeon,
  scout, quartermaster, drillmaster, jailer — and tempers; dishonorable
  deeds can drive the principled ones away. Dress them from your bag.

## Quests & the journal

- [G] in a town (or a lord's court) offers work: bandit hunts, deliveries,
  escorts (a real convoy rolls out and must survive), each with the giver's
  clock ticking. The HUD tracks progress; [Q] opens the journal — the task
  in full plus the record of every quest taken, done, or failed.
- Completion pays gold, relation, renown — with a banner and fanfare.
  Failure (timeout, dead convoy) costs relation, with a knell.

## Kingdom & grand strategy

- **Renown** opens doors: with enough, swear to a crown ([V] in their town),
  fight their wars, answer muster calls, and earn fiefs. Or stay free.
- **The ledger** ([B]) is the ruler's view: fiefs and their lords, armies
  afield, wars (click to sue for peace or declare), the tax lever (higher
  taxes bite prosperity), and the day's full income/wage books.
- **Lords** have opinions of you — gifts, feasts, relief of their sieges,
  freed captives all count — and above all, **deeds in the field**:
  fight beside a lord's host in a joined battle and he remembers (+4 won
  together, +1 even in shared defeat; his crown warms too). Scorned
  lords of yours can defect at dawn.
  Captured lords escape if unguarded (a jailer companion helps).
- **Crowned?** Raise new lords in owned settlements, grant fiefs, host
  feasts, and mind the chronicle — victory and defeat both get a screen,
  and the saga exports to a text file beside the saves.

## Your estate (the personal seat)

- Press **[E]** near a town at peace with you and pay 1000 gold: your manor
  rises outside its gates, linked to that town's fortunes.
- Build from the registry (one work at a time; masons take days):
  - **Tilled Fields** — daily rent scaled by the linked town's prosperity.
  - **Barracks** — party cap +10, and the town's recruit pool grows daily.
  - **Estate Smithy** — promotions cost 15% less.
  - **Granary** — hunger never thins your ranks while it stands.
  - **Stone Walls** — field battles fought on estate land are fortified.
- Buildings are moddable (assets/buildings.cfg) and save with the game.

## Parley — words before steel

When a hostile band catches you (or you it), the encounter opens at the
parley screen, not the battlefield:

- **[1] Draw steel** — fight as normal (Esc does the same).
- **[2] Buy the road** — a bribe of 15 gold per man they field (lords
  charge double). They take the purse and ride wide.
- **[3] Demand surrender** — at 3:1 odds or better they ground arms and
  march as your captives, no blood spilled (+renown). Short of that,
  they laugh and charge.
- **[4] Slip away** — you give ground and escape, unless their horse
  outnumbers yours: riders run down men on foot.

## Sieges — more than the ladder

Clicking a hostile walled town offers, besides storming and engineering:

- **[4] Blockade** — throw a cordon around it and stay close: every dawn
  the garrison starves a little and prosperity bleeds. Thinned to a fifth
  of its walls' worth, the town opens its gates without a fight
  (+renown). Ride off and the cordon breaks.
- **[5] Poison the wells** — a third of the defenders sicken and die at
  once. A black deed: honor −3 and the owner remembers.

## Battle

- **Melee is deliberate:** hold LMB to ready a swing, flick the mouse to
  aim it (up = overhead, down = thrust, sides = cuts), release to strike.
  The blow carries what the wind-up put into it — a panicked flick lands
  at half weight, a full draw lands whole — and every weapon needs 1.6×
  the old time between swings. RMB blocks. **Shields are cover**: they
  soak blows from every direction while the wood lasts (it wears, and
  splinters); bare-weapon guards are directional and read from the stance.
  [E] kicks straight through a raised shield; [G] scavenges a fallen
  man's weapon (and pulls spent arrows).
- **Archery:** a bow draws and looses where the camera looks; a full draw
  hits harder. You carry 24 arrows — harvest landed shafts with [G].
  AI archers run dry too and close with steel.
- **Cavalry:** [Z] mounts/dismounts; the gallop adds reach and weight and
  tramples — unless the man you ride down has a **braced polearm**. Horse
  archers kite. Buy a warhorse ([W] at a market) to ride in person.
- **Command:** F1 hold, F2 follow, F3 charge; [~] opens the strategy menu —
  Line, Square, Spread, Shield Wall (slow, braced, tough), rank count.
  Deployment lets you shape up before the horn.
- **Morale:** every soldier carries nerve; deaths nearby drain it, kills
  restore it, your war horn ([V]) rallies, your own kills embolden. Broken
  men rout; routed armies yield quarter — prisoners for you.
- **Champions & duels:** enemy lords ride in person with a champion's
  strength; some fights resolve as single combat while both lines watch.
- **Sieges:** walls, a gate, ladders; attackers funnel and climb, defenders
  hold the wall. Fortified walls (yours or theirs) bite harder. Relief
  lords can ride to a defender's aid — yours included.
- **Weather & night:** rain shortens bowshots and slicks the field; night
  dims archery and the sky both. The camera wheel zooms 2.2–9.0.
- **[N]** auto-resolves any battle on paper if you'd rather not fight it.

## Saving & options

- F5–F7 quicksave slots, autosave, Esc-Esc save+quit; the load menu peeks
  each slot. Settings ([O]): fullscreen, draw distance, particles, volume,
  invert Y, **lettering scale**, battle size (bigger battles use reserve
  waves).

## For modders & scripters

- Nearly everything is data: weapons, armour, troops (with upgrade lines),
  factions and wars, goods, enterprises, events, buildings, companions, and
  the whole map (assets/*.cfg) — see MODDING.md.
- The headless harness (`--script`) drives real gameplay for testing; see
  tests/README.md and src/harness.cpp.

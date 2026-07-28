# The Campaign — every road, every button

This is the campaign half of the manual: what every option does, and which
system it feeds. Nothing here is decoration — every line below was traced to
real state changes in the world (and the few that weren't have been given
teeth, marked **[NEW]**).

## The map

- **Click the ground / hold WASD** — your warband marches. Time only flows
  while you move or wait: the world is passively paused, so nothing sneaks up
  on a thinking player.
- **SPACE (hold)** — wait in place. Time flows: caravans trade, lords hunt,
  wounds heal, construction advances, dawn ticks the economy.
- **Click a settlement** — sets course for it; you enter by *arriving at* the
  gate (clicking from across the map does not teleport you in).
- **Click a bandit lair** — sets course to burn it out; arriving starts the
  assault.
- **Mouse wheel** — zoom the map.
- **[1] / [2] near a skirmish** — two AI parties fighting each other lock
  into a skirmish that resolves on its own; these keys join a side, turning
  it into a full battle with the backed party as your ally. Their casualties
  carry back to their campaign party, and helping a faction warms relations.
- **[T] Hail a lord** — opens a road-side audience with the nearest
  non-hostile lord's party within hailing distance. This is the same
  conversation you'd have in his hall: swear service, ask for work, court
  him with gifts, propose marriage, recruit his company, pay off Graves the
  collector, lend him gold, or talk him into turning his coat. A hostile
  lord will not talk ("he talks with steel") — except Graves, who always
  hears gold if you owe him.
- **[K] Claim a crown** — with two settlements held, you crown yourself: a
  faction of your own, wars of your own. Crowning while sworn to a liege is
  rebellion, and he will treat it as such.
- **[J] Rally the banners** — as a crowned ruler, calls your lords to your
  position for three days; they fight beside you when the war comes to you.
- **[R] after a defeat (Ironman only)** — begin again. With Ironman off
  (the default) the lone hero always survives defeat; this key only matters
  when you chose permadeath.
- **[F5/F6/F7] save slots, [F9] quick-load** — three slots plus the quick
  pair; the Load menu on the title screen reads the same slots.

### The bottom bar
Every button is also a hotkey; hovering shows it.
**Journal [Q]** · **Party [P]** · **Character [C]** · **Bag [I]** ·
**Ledger [B]** · **Estate [E]** · **Hail a lord [T]** · **Options [O]**.

### Sieges (walk into a hostile settlement)
A prompt with five real choices:
1. **Storm the gate** — immediate assault; walls mean ladders at the gate.
2. **Torch it** (villages) / **Build ladders** (walls) — ladders take a day
   and 2 timber (bought from sutlers at 8 gold a beam if your bags are
   short) and open extra climbing points.
3. **Build a siege tower** — two days, 5 timber; a rolling tower with its
   ramp on the rampart.
4. **Blockade** — throw a cordon and stay close: the garrison starves a
   little every dawn. Riding away (or peace) lifts it.
5. **Poison the wells** — a third of the garrison sickens and dies, your
   honor pays for it, and the deed is remembered in relations and chronicle.

## Settlements — the gate menu
An eagle-eye view with a side menu; you only walk the streets if you choose
to. Rows (click or number): enter and walk **[Enter]**, tournament **[3]**
(Shift stakes gold), market, tavern, recruits, quests, feast **[0]** (spends
food and gold, buys the town's regard), sellswords **[J]**, fortify **[F]**
(town works that raise the walls), and the same party/options/ledger side
buttons as the map bar.

### Walking the streets
- **[E] Talk** to villagers and the tavern keeper; lords hold court in the
  keep. Every dialogue topic changes something (see Dialogue below).
- **[1..9] Recruit** from the roster chips; **[H] hire** the tavern company;
  **[T] tournament**; **[M] market**; **[G] ask for work** (quests);
  **[V] swear** to the local crown; **[F] garrison** a soldier (Shift+F
  takes one back); **[R] ransom** your captives for gold; **[U]/[Y]**
  ransom or release captive *lords* (releasing buys goodwill, ransoming
  buys gold and resentment); **[L] raise a lord** from your veterans once
  you are crowned.

## The tournament (data-driven since V185)
Enter from the gate menu row 3 or [T] in the streets; **Shift stakes 50
gold** that pays ×3 if you take the crown. The circuit itself — rounds,
purse, per-round winnings, stake odds, renown — is a `TournamentDef` in the
content registry, so mods can field their own circuits.

- Three rounds of real fighting with borrowed gear beside borrowed blades —
  autoresolve is barred from the ring, and nothing that happens in the
  lists touches the world outside it (no casualties, no war score).
- **[NEW] Between rounds** you stand in the tent: bracket progress, your
  winnings so far, and two honest buttons — **Fight on** or **Withdraw with
  your winnings** (each round taken pays out, and your stake comes home).
  This screen also fixes the old wedge where a won round could stick the
  game on the victory banner forever.
- The champion takes the purse, the stake at odds, renown, hero XP, and the
  host crown's regard.

## The market
A paged tile grid; hover shows prices. **Left-click/[1-9] buys, right-click
or Shift sells.** Prices are live: they follow the town's stock and
prosperity, which follow caravans, war, blockades, your banked capital and
your enterprises — the whole economy is one connected loop.
- **Caravan [C]** (200 gold + cargo) — loads this market's surplus and
  plies the roads; every sale rides home to your ledger. Bandits can smell
  it, and road danger reroutes trade — which is why prosperous roads matter.
- **Deposit / Withdraw [D / Shift+D]** — the moneylender pays 5% a week,
  and banked capital feeds the town's prosperity daily (interest touching
  prices, made real).
- **Destrier [W]** — one horse, once, for life; twice the mount under you
  in every battle.
- **Loan / repay [L]** — 300 now against 350 in ten days; default and
  Graves the collector comes for you on the road.
- **Enterprise [B]** — buy the town's works (one per town), buy again to
  expand it for twice the take; it also nudges the town's prosperity.
- **Land deed [T]** — up to three parcels per town; rent rides prosperity,
  and a hostile owner confiscates the take until peace.
- **Armour / weapon tiles** — the forge's two pieces of the day, priced by
  the same stat formulas troop promotions pay.

## Dialogue — what every topic actually does
- **News of the war** — a live report assembled from the world state.
- **Swear my sword** — vassalage under the local crown: their wars become
  yours, fiefs may follow.
- **Work for me?** — a real quest with a clock (deliveries, bandit hunts,
  escorts). The journal tracks it.
- **Grant this seat** — as a liege: hand a settlement to a lord (his regard
  rises with the gift).
- **Marriage suit** — renown and regard gate it; a match binds his house to
  yours in war.
- **Rebellion** — talk a disaffected lord into rising with you.
- **A gift** — gold for regard, the blunt instrument of diplomacy.
- **Hire the company** — his armed retinue shadows your banner for a fee.
- **Turn his coat** — the expensive way to steal a lord, priced by his
  regard for his own liege.
- **Pay Graves** — clears your debt when the collector finds you.
- **Lend to him** — lords borrow too; the ledger tracks their notes, and
  dawn services the interest. A lord in your debt fights a little harder
  to stay your friend.

## The journal [Q]
The task at hand with live progress and its clock, plus the permanent
record of everything done. **[NEW] [X] abandons the task** — laying a quest
down costs a little regard with the giver's crown and is written into the
record; before this the only way out of an impossible errand was to let
the clock run out.

## Party [P], Character [C], Bag [I]
- **Party**: click a row to **promote** (the cost is literally the gear
  delta between tiers), right-click/Shift to **dismiss**; **[R]** presses a
  captive into your ranks (they may desert on the road).
- **Character**: spend level-up points on attributes; they feed damage,
  health and speed through the same data-driven formulas troops use.
- **Bag**: a tiled grid; pick up with click, equip with [E]/right-click
  (the paper-doll shows slots), sell with [S], and [Tab] cycles which
  companion you are outfitting. What a fighter wears is what the battle
  renderer draws.

## The ledger [B]
Income (tariffs, enterprises, rents, garrison wages) against costs, war
scores per front, and the levers: **[T] cycles taxes** (light/customary/
heavy — the land blooms or withers), **click a war row** to sue for peace
with tribute priced by the war score, **click a rival crown** to declare
war. Truces hold for a few days; breaking the peace early is not on offer.

## The estate [E]
Your manor by its town: each row is a building from the data registry —
cost, days of construction (masons work while time flows), and a daily or
military effect once it stands. One crew at a time; the hall remembers
what is built.

## Options [O]
Twelve rows, all clickable: fullscreen, draw distance, particles, volume,
invert-Y, lettering scale, battle size, Ironman (permadeath is opt-in),
Post FX, shadows, **renderer (raylib GL / Vulkan)**, and body style
(pill / blocky / boxy). Everything persists to `assets/settings.cfg`.

## How it all loops together
Caravans and peace raise **prosperity** → prosperity moves **prices**,
**recruit pools** and **tax income** → gold builds **enterprises, land,
banks and estates** → those deepen prosperity and fund **wars** → wars
create **danger** that reroutes trade and starves towns → victories buy
**renown**, renown buys **lords, marriages and crowns** → and the crown
turns the whole machine over to you.

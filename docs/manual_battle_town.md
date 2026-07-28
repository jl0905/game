# OpenWarband Manual — Battle & Settlements

Every control and choice on the battle and settlement screens: what it
does, and which system it feeds. (Campaign-map, market, and kingdom
screens are covered in the companion manual sections.)

---

## Battle: your own hands

| Control | What it does | System it feeds |
|---|---|---|
| **W A S D** | Move, camera-relative. | Hero physics; sprinting into terrain obeys the same heightfield the AI uses. |
| **Mouse** | Look. The **last direction you flicked** before releasing an attack picks your swing (dominant axis: up = overhead, down = thrust, left/right = cuts). | The V143 aim rule — the HUD shows the current `aim` direction. |
| **LMB hold → release** | Hold to wind up (the cock is deliberately huge so enemies can read it — so can you), release to strike. Damage, reach and cooldown come from the wielded weapon's data. | Directional melee; hero melee recovery is near-zero so you can feint: cock, cancel with RMB, re-cock. |
| **RMB** | Block (hold). With a **shield**: blocks all four directions and soaks arrows. With a **weapon only**: a *directional* parry — it bars the direction of your last mouse flick (shown as the guard pose); a matched parry costs the attacker most of the hit. Blocking also cancels your own wind-up (the feint). | Guard/parry system; `BLOCK_MISSILE_FACTOR` for arrows. |
| **Space** | Jump (on foot). | Hero physics; lets you hop low rubble and ladder lips. |
| **Mouse wheel** | Camera closer / farther. | Adjustable third-person camera. |
| **Q** | Swap to your next carried weapon (your arsenal cycles; the AI auto-picks per range instead). | Loadout arsenal (spear at range, sword in the press). |
| **E — the boot** | A short kick, ~1.2s recovery, **on foot only** (you cannot kick from the saddle — dismount first). Staggers the man in front and opens a turtled shield for a beat. The HUD shows `BOOT READY` / its cooldown. | Anti-turtle tool; kicks landed are tallied in the after-battle report. |
| **G** | Take up a fallen man's weapon if one lies at your feet (replaces your active weapon). | Battlefield scavenging — grab a spear when your blade is short. |
| **Z** | Dismount / remount your horse (if you own one and it lives). A killed mount drops you on foot for the rest of the fight. | Mount system; loose horses wander and yours is marked gold. |
| **V — the war horn** | A cry that turns nearby **routed** friendlies back into the fight. Costs nothing; timing it as your line wavers is the skill. | Morale/rout system. |
| **N** | Auto-resolve: the battle is fought on paper immediately (deployment or early in the fight). | The V41 resolver — same odds math the campaign skirmishes use. |
| **Esc** | Retreat/leave per the pre-battle terms. | Battle outcome carries to the campaign. |

## Battle: commanding your men

| Control | What it does |
|---|---|
| **F1 / F2 / F3** | Barked orders, no menu: **Hold** (form on this spot and keep it), **Follow** (form on you as you move), **Charge** (loose them). |
| **1–5** | Pick a formation **any time** — menu open or not *(fixed in the V185 audit; previously the keys silently did nothing unless the ~ menu was up)*: **1 Charge** (also orders the charge), **2 Line**, **3 Square**, **4 Spread**, **5 Shield Wall**. Picking a held shape (2–5) while charging automatically switches your men to Follow so the shape can form. |
| **[ / ]** | Fewer / more ranks (1–8): a wide thin line versus a deep short one. Works alongside any formation, menu or not. |
| **~** | The strategy menu: the readable version of all of the above — click rows instead of remembering keys. Opens the cursor. |
| **R (deployment)** | Sound the horn — end the deployment pause and begin the battle. Until then the field holds its breath: no movement, no arrows. |
| **D (deployment)** | **Single combat**: if the enemy fields a champion (a lord in person, circlet on his head), challenge him. Both armies hold while you duel; the winner's side takes the field's morale. |

Formations are **yours only** — allies and enemies always charge. Your
formation, order and rank count persist through the battle until you
change them.

## Battle: before the steel (the parley screen)

When you engage certain parties you get words before blades (see the
campaign manual for the full option list — *hail/parley is the campaign-map
`T` key*: it opens talk with the nearest lord's party, where terms,
tribute, or the fight itself are chosen). Against **walls** two more
options are real, not flavour: **blockade & starve** marks the town — its
garrison starves a little every dawn you hold the cordon (walk away and it
lifts); **poison the wells** kills defenders immediately but is a black
deed your honour pays for.

## Sieges

Attacking walls adds climbing points: **ladders** lean on the outer face
(climb by walking into them — the bump carries you up), a built **siege
tower** rolls its ramp onto the rampart, and the **gate** is the choke.
Fortified stone (bought at the settlement menu) reads darker, carries a
timber hoarding, and holds +10 garrison beds.

## The settlement (gate view and streets)

Entering a settlement opens the **gate view**: an eagle-eye look at the
town with the menu at the side. Nothing moves until you choose. Every row
is a button (hotkey appears under the mouse); greyed rows say why they're
dead.

| Row | What it does | System |
|---|---|---|
| **The market** | Buy/sell goods and arms on the tile GUI, the moneylender (deposit/withdraw/loan), caravans, enterprises, land deeds, the destrier. | Economy: prices move with prosperity, road danger and your own trading. |
| **The tavern** | Walk in at the hearth. Inside: **number keys recruit** from your faction's roster (drains the town's recruit pool — let towns prosper to refill it), **R** ransoms captives, the companion waits by the fire. **E** steps back out. | Recruiting, captives, companions. |
| **The tournament** | Enter the arena bracket (towns only). Shift-click stakes 50 gold on yourself. Win rounds for renown and the purse. | Arena/renown; party cap grows with renown. |
| **Seek work** | The local quest giver: fetch/escort/hunt work with real rewards; tracked in the quest journal (Q on the map). | Quest system. |
| **Hire the companion** | Each town's named companion joins once, forever (gear them via the inventory, Tab to target them). | Companions: heroes that never drift or starve. |
| **Swear to this crown** | Vassalage: your banner joins this faction — its wars become yours, lords' opinions start mattering, fiefs may follow. Free captains only. | Diplomacy/fealty. |
| **The hall** | At a castle: the lord's court (news, politics, **lend to the lord** — loans come back with interest and gratitude). Elsewhere: the local talk. Dialogue options are numbered; each names its topic. | Dialogue/diplomacy; lord opinions. |
| **Garrison / recall a soldier** | Your walls only: move one soldier between party and garrison (F / Shift+F work anywhere in town too). Garrisons defend when you're elsewhere and eat from the town's purse. | Settlement defence. |
| **Walk the streets in person** | Third-person walk mode (battle controls). **E** near a passer-by stops them for a word; **E** at the tavern door enters; **Esc** returns to the gate view. | The walkable scene: villagers live here, buildings are solid. |
| **Host a feast** | Your own hall, 200 gold: every lord of your realm warms to you (+opinion), the town eats well (+prosperity), matches may be made while it holds. One feast in the land at a time. | Feast/marriage/opinion systems. |
| **Sellswords for hire** | Towns only, 150 gold: a rotating 5-man pack (pikemen / arbalists / infantry by town and day) joins instantly if you have room. | Instant muscle, no pool drain — the mercenary economy. |
| **Fortify the walls** | Your walls, 500 gold, once: +10 garrison beds, a hoarding, and a harder storm for anyone who tries. | Siege defence. |
| **Ride on (Esc)** | Leave. From the streets, Esc first returns to the gate view. | — |

## How it hangs together

Recruits come from town **pools** that refill with prosperity; prosperity
follows **caravans** arriving safely, which follows **road danger**, which
follows who you've been killing (or failing to). Garrisons and feasts
spend the same gold your enterprises, land rents and loot bring in. Renown
from tournaments and battles raises your **party cap**; honour from how
you fight (poisoned wells remember) shapes how lords and quests treat you.
The battle is where all of it is spent — and the outcome writes straight
back into every one of those ledgers.

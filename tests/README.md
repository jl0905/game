# Harness regression scripts

Scripted playthroughs for the headless harness (see CLAUDE.md "Programmatic
play"). Run one with:

```
cmd /c "build\openwarband.exe --script tests\<name>.txt > out.txt 2>&1"
```

Then read `out.txt` — each script prints `state` dumps at its checkpoints.
These are *plays*, not asserting tests: verify by reading the output against
the comment at the top of each script.

| Script | Exercises |
|---|---|
| `soak.txt` | ~12 sim-minutes of everything at once: battles, lords, economy, saves, all screens; look for crashes/negative counts/wedged screens |
| `strafe.txt` | battle movement conventions (A/D vs camera), blocking, AI battle resolution |
| `siege_walls.txt` | walled assault: wall defenders (`wall=N`), gate funnel, capture |
| `economy_desertion.txt` | daily ledger + unpaid desertion |
| `veterancy.txt` | troop XP accrual + promotion in the party screen |
| `town_tavern.txt` | walkable settlement: navigate to the tavern, recruit |
| `lord_notice.txt` | lords ignore trivial parties; your-settlement siege alert |
| `focus_fire.txt` | value-based targeting: the line finishes wounded foes, a contested clash resolves decisively (V119) |

Scripts use fixed seeds but the world is chaotic — read outputs for the
*mechanism* under test, not exact numbers.

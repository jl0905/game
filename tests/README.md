# Harness regression scripts

Scripted playthroughs for the headless harness (see CLAUDE.md "Programmatic
play"), plus the renderer smoke. **The canonical runners (V179):**

```
powershell -File tests\run_suite.ps1                # all scripts vs build\openwarband.exe
powershell -File tests\run_suite.ps1 -Filter vet*   # a subset
powershell -File tests\run_suite.ps1 -Exe build-x\openwarband.exe
powershell -File tests\run_render.ps1               # bench BOTH backends (GL + Vulkan)
```

`run_suite.ps1` passes a script when it prints `harness: done` and exits 0;
per-test output lands in `<builddir>\testout\<name>.txt`. Exit code = number
of failures. It reports tests slower than 20s.

`run_render.ps1` replaces the hand-edited-settings.cfg ritual from the
V161+ renderer migration: it benches `renderer raylib` then `renderer
vulkan` from inside the build dir (bench.txt and the settings edit never
touch the repo root), asserts the Vulkan executor actually came up
(`VULKAN DEVICE LIVE` / `FRAME EXECUTOR LIVE` / `SHADOW PASS LIVE` in the
log), enforces a parity guard (default: Vulkan avg <= 1.6x GL), and always
restores settings.cfg. Run it after touching src/rdr.*, src/vkexec.cpp,
assets/spv, or anything in the battle/town draw paths.

To run ONE script by hand:

```
cmd /c "build\openwarband.exe --script tests\<name>.txt > out.txt 2>&1"
```

Then read `out.txt` — each script prints `state` dumps at its checkpoints.
These are *plays*, not asserting tests: the runner proves "didn't wedge or
crash"; verify the *mechanism* by reading the output against the comment at
the top of each script (the "meaning-check").

Agent gotchas (learned the hard way):
- Check output with `(Get-Content out.txt | Out-String) -match "harness: done"`
  — Get-Content alone returns an array and -match behaves differently.
- The exe is a GUI app in Release: capture output via `cmd /c "... > f 2>&1"`,
  never rely on direct PowerShell redirection.
- `cmd` may not search the CWD for bare exe names
  (NoDefaultCurrentDirectoryInExePath) — use a path, e.g. `build\openwarband.exe`.
- Save fixtures in `tests/fixtures/` are gitignored by pattern — `git add -f`.
- Scripts use fixed seeds but the world is chaotic — assert mechanisms, not
  exact numbers.

A sampler of the suite (137 scripts total — this table is illustrative, not
exhaustive; each script's header comment is its documentation):

| Script | Exercises |
|---|---|
| `soak.txt` | ~12 sim-minutes of everything at once: battles, lords, economy, saves, all screens; look for crashes/negative counts/wedged screens |
| `strafe.txt` | battle movement conventions (A/D vs camera), blocking, AI battle resolution |
| `siege_walls.txt` | walled assault: wall defenders (`wall=N`), gate funnel, capture |
| `economy_desertion.txt` | daily ledger + unpaid desertion |
| `veterancy.txt` | troop XP accrual + promotion in the party screen |
| `town_tavern.txt` | walkable settlement: navigate to the tavern, recruit |
| `lord_notice.txt` | lords ignore trivial parties; your-settlement siege alert |

Scripts use fixed seeds but the world is chaotic — read outputs for the
*mechanism* under test, not exact numbers.

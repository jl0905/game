# AGENTS.md

Global agent context for this repository.

The full, canonical project context — architecture, design principles, code
layout, and how to extend the game — lives in **[CLAUDE.md](./CLAUDE.md)**.
Read it before making changes.

## Quick reference

- **Project:** OpenWarband — a C++17 / raylib Mount & Blade–style game.
- **Golden rule:** the game is **data-driven**. Add armour, weapons, troops, and
  factions by registering definitions in [`src/content.cpp`](./src/content.cpp) —
  not by editing game logic.
- **No balancing.** Numeric values are intentionally flat placeholders marked
  `TODO(balance)`; do not bake in tuned progressions. See CLAUDE.md → "Design
  principles".
- **Build & run:** `./build.ps1` (auto-detects MinGW/MSVC). Verify changes by
  launching the game, not just compiling.

For everything else, see **[CLAUDE.md](./CLAUDE.md)**.

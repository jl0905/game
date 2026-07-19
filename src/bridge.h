#pragma once
#include "world.h"
#include "battle/battle.h"

// ---------------------------------------------------------------------------
// The one bridge between world state and a battle, used by both the windowed
// loop (main.cpp) and the headless harness. Campaign and battle modules still
// never include each other — only orchestrators include this.
// ---------------------------------------------------------------------------

// Snapshot the world into a battle setup. Handles both field battles
// (battlePartyIndex) and sieges (siegeTownIndex — enemy is the garrison and
// the fight happens at the settlement).
inline BattleSetup MakeBattleSetup(const GameState& gs) {
    BattleSetup s;
    s.playerTroops = gs.player.troopCounts;
    if (gs.siegeTownIndex >= 0 && gs.siegeTownIndex < (int)gs.towns.size()) {
        const Town& t = gs.towns[gs.siegeTownIndex];
        s.enemyTroops = t.garrison;
        s.campaignPos = t.pos;            // battlefield looks like the town's land
        s.siege       = true;
        s.siegeType   = t.type;
    } else {
        s.enemyTroops = gs.parties[gs.battlePartyIndex].troopCounts;
        s.campaignPos = gs.player.pos;
    }
    if (gs.battleAllyIndex >= 0 && gs.battleAllyIndex < (int)gs.parties.size())
        s.allyTroops = gs.parties[gs.battleAllyIndex].troopCounts;
    s.heroLoadout = gs.playerHero.loadout;
    s.heroMaxHp   = gs.playerHero.maxHp;
    return s;
}

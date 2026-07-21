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

    // Tournament bout (G2): borrowed fighters and practice gear on both
    // sides — the party and its equipment stay outside the ring.
    if (gs.arenaFight) {
        const Content& c = gs.content;
        s.arena = true;
        s.playerTroops.assign(c.troops.size(), 0);
        s.enemyTroops.assign(c.troops.size(), 0);
        // The bracket narrows (K3): an even melee (4-beside-you v 4), then
        // 2 v 2, then the final duel, hero against champion — your own blade
        // is the edge in every round. TODO(balance): field sizes.
        const int round  = gs.arenaRound > 0 ? gs.arenaRound : 1;
        const int mine   = c.troops.find("recruit");
        const int theirs = c.troops.find("brigand");
        const int allies  = round == 1 ? 4 : round == 2 ? 2 : 0;
        const int enemies = round == 1 ? 4 : round == 2 ? 2 : 1;
        if (mine >= 0)   s.playerTroops[mine] = allies;
        if (theirs >= 0) s.enemyTroops[theirs] = enemies;
        s.heroLoadout = Loadout{};
        s.heroLoadout.addWeapon(c.weapons.find("sword"));           // practice blade
        s.heroMaxHp = gs.playerHero.maxHp;
        if (gs.currentSettlement >= 0 && gs.currentSettlement < (int)gs.towns.size())
            s.campaignPos = gs.towns[gs.currentSettlement].pos;
        return s;
    }

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
    s.gearOverrides = gs.companionGear;   // fitted companions fight dressed (K6)
    // Battlefield biome from the moddable map (K8): the battle module stays
    // world-blind — it just receives the field it will be fought on.
    const MapDef& m = gs.content.map;
    s.hilliness     = Clamp(0.5f + 0.5f * BiomeHillNoise(m, s.campaignPos), 0.0f, 1.0f);
    s.forestDensity = Clamp(0.5f + 0.5f * BiomeForestNoise(m, s.campaignPos), 0.0f, 1.0f);
    return s;
}

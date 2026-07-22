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
        // A friendly lord in reach rides to the defence (P2): his host
        // fights beside the garrison in the same battle.
        const int relief = ReliefLordFor(gs, gs.siegeTownIndex);
        if (relief >= 0) {
            const Party& r = gs.parties[relief];
            if (s.enemyTroops.size() < r.troopCounts.size())
                s.enemyTroops.resize(r.troopCounts.size(), 0);
            for (int i = 0; i < (int)r.troopCounts.size(); ++i)
                s.enemyTroops[i] += r.troopCounts[i];
        }
        s.campaignPos = t.pos;            // battlefield looks like the town's land
        s.siege       = true;
        s.siegeType   = t.type;
        s.siegePrep   = gs.siegeLaunchPrep;   // what the camp built (N1)
    } else {
        s.enemyTroops = gs.parties[gs.battlePartyIndex].troopCounts;
        s.campaignPos = gs.player.pos;
    }
    if (gs.battleAllyIndex >= 0 && gs.battleAllyIndex < (int)gs.parties.size())
        s.allyTroops = gs.parties[gs.battleAllyIndex].troopCounts;
    s.heroLoadout = gs.playerHero.loadout;
    s.heroMaxHp   = gs.playerHero.maxHp;
    s.gearOverrides = gs.companionGear;   // fitted companions fight dressed (K6)
    // The hero's body rides too (V14).
    s.heroStr = HeroAttr(gs, 0);
    s.heroAgi = HeroAttr(gs, 1);

    // The hour rides into battle with you (O3). 60 s/day mirrors the
    // campaign's DAY_LENGTH (TODO(balance) there).
    s.timeOfDay = gs.dayTimer / 60.0f;

    // Battlefield biome from the moddable map (K8): the battle module stays
    // world-blind — it just receives the field it will be fought on.
    const MapDef& m = gs.content.map;
    s.hilliness     = Clamp(0.5f + 0.5f * BiomeHillNoise(m, s.campaignPos), 0.0f, 1.0f);
    s.forestDensity = Clamp(0.5f + 0.5f * BiomeForestNoise(m, s.campaignPos), 0.0f, 1.0f);
    return s;
}

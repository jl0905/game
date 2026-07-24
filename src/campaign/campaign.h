#pragma once
#include "../world.h"
#include "../input.h"

// ---------------------------------------------------------------------------
// Campaign module public interface. Owns the overworld: party movement and AI,
// towns/recruiting, economy, skirmishes, and applying battle outcomes.
//
// Each screen is split three ways so the game can be driven programmatically:
//   Gather*  — read the real devices into an input-intent struct (windowed only)
//   *Update  — pure simulation; touches no raylib input or drawing
//   *Draw    — render the current state (windowed only)
//
// The campaign never runs a battle itself. When a battle should start it sets
// GameState::screen = Screen::Battle (+ battlePartyIndex / battleAllyIndex);
// the caller snapshots the world into a BattleSetup and runs the battle.
// ---------------------------------------------------------------------------

void CampaignInit(GameState& gs);

// Title screen (windowed entry point). TitleUpdate returns false on Quit.
bool TitleUpdate(GameState& gs, const CampaignInput& in);
void TitleDraw(const GameState& gs);

// Victory screen — the campaign is won; any choice returns to the title.
bool VictoryUpdate(GameState& gs, const CampaignInput& in);
void VictoryDraw(const GameState& gs);

CampaignInput GatherCampaignInput(const GameState& gs);   // covers settlement too

void CampaignUpdate(GameState& gs, float dt, const CampaignInput& in);
void CampaignDraw(const GameState& gs);

// (The settlement itself is a walkable 3D scene — see src/town/town.h.)

// Kingdom ledger (O1): the ruler's one-screen view — fiefs and their lords,
// armies afield, wars, and income by source. Read-only; B on the map.
void KingdomUpdate(GameState& gs, const CampaignInput& in);
void KingdomDraw(const GameState& gs);

// Quest journal (V124): the task at hand in full, and the ledger of tasks
// done and failed. Q on the map.
void QuestsUpdate(GameState& gs, const CampaignInput& in);
void QuestsDraw(const GameState& gs);

// The personal estate (V135): found it near a friendly town (E on the map),
// raise buildings from the shared registry, reap the effects daily.
void EstateUpdate(GameState& gs, const CampaignInput& in);
void EstateDraw(const GameState& gs);

// Pre-battle parley (V136): words before steel — fight, bribe, demand
// surrender, or slip away. Shown when a hostile party catches you.
void ParleyUpdate(GameState& gs, const CampaignInput& in);
void ParleyDraw(const GameState& gs);

// Load menu (N3): pick the autosave or a quicksave slot from the title.
void LoadMenuUpdate(GameState& gs, const CampaignInput& in);
void LoadMenuDraw(const GameState& gs);

// Character creation (N2): the background-choice screen after New Game, and
// the shared effect application (also driven by the harness `background` cmd).
void BackgroundUpdate(GameState& gs, const CampaignInput& in);
void BackgroundDraw(const GameState& gs);
void ApplyBackground(GameState& gs, int choice);   // 1 noble, 2 merchant, 3 deserter

// Settlement marketplace (buy/sell trade goods), opened with M in a settlement.
void MarketUpdate(GameState& gs, const CampaignInput& in);
void MarketDraw(const GameState& gs);

// Live prices (base × town offset × scarcity) — the market screen, the
// harness dump, and any trader AI must all quote the same numbers.
int MarketBuyPrice(const Content& c, const Town& t, int g);
int MarketSellPrice(const Content& c, const Town& t, int g);

// A ruler's purse (S5): the whole day's flows, quoted identically by the
// day tick, the party screen, the kingdom ledger, and the harness dump.
struct DayLedger {
    int income = 0, enterprise = 0, wages = 0, lordPay = 0, garrisonPay = 0;
    int net() const { return income + enterprise - wages - lordPay - garrisonPay; }
};
DayLedger ComputeLedger(const GameState& gs);

// Party management screen (roster + veterancy upgrades), opened with P.
void PartyUpdate(GameState& gs, const CampaignInput& in);
void PartyDraw(const GameState& gs);

// Tiled inventory screen (grid loot + equipping), opened with I.
void InventoryUpdate(GameState& gs, const CampaignInput& in);
void InventoryDraw(const GameState& gs);

// Settings screen (K1), opened with O from the title or the map.
void SettingsUpdate(GameState& gs, const CampaignInput& in);
void SettingsDraw(const GameState& gs);

// Character sheet (level / XP / attributes), opened with C.
void CharacterUpdate(GameState& gs, const CampaignInput& in);
void CharacterDraw(const GameState& gs);

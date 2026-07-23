#pragma once
#include "raylib.h"

// ---------------------------------------------------------------------------
// Per-frame player intent, decoupled from real devices. Windowed play fills
// these from raylib (Gather* functions); the headless script harness fills
// them programmatically. Simulation code (Update* functions) reads ONLY these
// structs — never the keyboard/mouse directly — so the game is fully drivable
// without a window.
// ---------------------------------------------------------------------------

struct CampaignInput {
    // overworld
    Vector2 move{};                // travel direction (unnormalized ok)
    bool    wait            = false;  // let time pass while standing (SPACE)
    int     clickSettlement = -1;     // settlement index to enter, -1 none
    bool    forceEnter      = false;  // harness/arrival bypass of the gate-distance rule (V120)
    int     clickLair       = -1;     // bandit den to storm, -1 none (H2)
    int     joinSide        = 0;      // join nearby skirmish: 1 = side a, 2 = side b
    bool    restart         = false;  // restart after warband destroyed (R)

    // settlement screen
    int     recruitSlot     = -1;     // roster slot to recruit, -1 none
    bool    ransom          = false;  // sell captives at the tavern (R)
    bool    interact        = false;  // enter/leave a building (E)
    bool    leaveSettlement = false;
    bool    openMarket      = false;  // browse the settlement market (M)
    bool    tournament      = false;  // enter the arena bracket in a town (T)
    bool    tournamentBet   = false;  // ...staking gold on yourself (Shift+T)
    bool    swear           = false;  // swear fealty at a kingdom's settlement (V)
    bool    quest           = false;  // ask the local giver for work (G)
    bool    hire            = false;  // hire the tavern's companion (H)

    // market screen (rows are goods-registry order)
    bool    sendCaravan = false;   // outfit a trade convoy here (M4, C at market)
    int     bankMove = 0;          // the moneylender (V5): +100 deposit / -100
                                   // withdraw at the market (D / Shift+D)
    int     saveSlot = 0;          // quicksave to slot 1..3 (N3, F5-F7 on the map)
    int     equipSlotHit = -1;     // paper-doll slot under the cursor (U8)
    bool    sellItem = false;      // sell the carried piece (V19, S in a town)
    bool    openLedger = false;    // kingdom ledger screen (O1, B on the map)
    bool    garrisonOne   = false; // leave a soldier on your walls (S2, F)
    bool    ungarrisonOne = false; // ...or call one down (Shift+F)
    bool    ransomLords  = false;  // prisoner lords (O2): sell them back (U)
    bool    pressPrisoner = false; // party screen R: a captive takes the coin (V36)
    bool    cycleTax      = false; // kingdom screen T: light/customary/heavy (V55)
    int     declareWar    = -1;    // kingdom screen: click a peaceful crown (V112)
    bool    buyWarhorse   = false; // market W: a destrier for life (V82)
    bool    loan          = false; // market L: borrow 300 / repay the debt (V84)
    bool    releaseLords = false;  // ...or set them free (Y)
    int     buyGood  = -1;            // good row to buy one unit of
    int     sellGood = -1;            // good row to sell one unit of (Shift+key)
    bool    buyEnterprise = false;    // buy a business in this town (B)

    bool    crown    = false;         // claim your own crown on the map (K)
    bool    rallyLords = false;       // crowned: call your lords to the banner (J)
    bool    parley = false;           // hail the nearest lord party on the map (S4, T)
    bool    raiseLord = false;        // raise a lord at your settlement (L)

    // party screen
    bool    openParty   = false;      // P on the map
    int     upgradeSlot = -1;         // troop row to promote one unit of
    int     dismissSlot = -1;         // troop row to dismiss one unit of (Shift+key)

    // inventory screen (cells are grid coordinates)
    bool    openInventory = false;    // I on the map
    int     invCellX = -1, invCellY = -1;   // cell under the cursor this frame
    bool    invPick  = false;         // pick up / place at the cell (LMB)
    bool    invEquip = false;         // equip the item at the cell (E / RMB)
    bool    invCycleTarget = false;   // Tab: fit the hero / a hired companion (K6)

    // character sheet
    bool    openCharacter = false;    // C on the map
    int     spendAttr     = -1;       // attribute row to put a point into

    // settings screen (K1)
    bool    openSettings = false;     // O on the map / title
    int     settingsRow  = -1;        // option row to toggle/cycle

    // title screen: 1 = new game, 2 = continue (autosave), 3 = quit
    int     menuChoice = 0;

    // meta
    bool    quickSave = false;        // F5
    bool    quickLoad = false;        // F9
};

struct BattleInput {
    float   moveForward = 0;       // -1..1  (S..W), camera-relative
    float   moveRight   = 0;       // -1..1  (A..D), camera-relative
    Vector2 lookDelta{};           // camera rotation this frame (mouse pixels)
    bool    jump          = false;
    bool    block         = false; // held
    bool    attackPress   = false; // begin readying a swing (LMB down)
    bool    attackRelease = false; // release the swing (LMB up)
    bool    swapWeapon    = false; // Q
    bool    toggleMenu    = false; // ~ strategy menu
    int     formationSelect = 0;   // 1..4 while menu open, 0 = none
    int     ranksDelta      = 0;   // -1 / +1 ([ / ])
    int     order           = 0;   // battlefield order (M2): 1 hold, 2 follow,
                                   // 3 charge (F1/F2/F3, no menu needed)
    bool    beginBattle     = false;   // sound the horn: end deployment (R2)
    bool    mountToggle     = false;   // Z: dismount / remount (U11)
    bool    kick            = false;   // E: the boot that opens a guard (V33)
    bool    pickup          = false;   // G: take up a fallen man's weapon (V39)
    bool    autoResolve     = false;   // N: send them in without you (V41)
    bool    warCry          = false;   // V: the horn that turns routed men (V66)
    bool    duel            = false;   // D at deployment: single combat (V102)
};

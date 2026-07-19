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
    int     joinSide        = 0;      // join nearby skirmish: 1 = side a, 2 = side b
    bool    restart         = false;  // restart after warband destroyed (R)

    // settlement screen
    int     recruitSlot     = -1;     // roster slot to recruit, -1 none
    bool    leaveSettlement = false;

    // party screen
    bool    openParty   = false;      // P on the map
    int     upgradeSlot = -1;         // troop row to promote one unit of
    int     dismissSlot = -1;         // troop row to dismiss one unit of (Shift+key)

    // inventory screen (cells are grid coordinates)
    bool    openInventory = false;    // I on the map
    int     invCellX = -1, invCellY = -1;   // cell under the cursor this frame
    bool    invPick  = false;         // pick up / place at the cell (LMB)
    bool    invEquip = false;         // equip the item at the cell (E / RMB)

    // character sheet
    bool    openCharacter = false;    // C on the map
    int     spendAttr     = -1;       // attribute row to put a point into

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
};

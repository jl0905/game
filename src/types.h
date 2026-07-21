#pragma once
#include "raylib.h"

// ---------------------------------------------------------------------------
// Core enums shared across the whole game. These describe *kinds* of things,
// never balance values. Extend the enums here and the registries in content.*
// pick the new kinds up.
// ---------------------------------------------------------------------------

// Top-level screen / flow state.
enum class Screen { Title, Campaign, Settlement, Market, Party, Inventory, Character,
                    Battle, BattleResult, Victory };

// Kind of settlement on the overworld. Drives the map icon and the greeting /
// available actions on the settlement menu, not balance.
enum class SettlementType { Village, Town, Castle };

// Equipment slots a character can fill. Add coverage here (e.g. Legs, Cape)
// and both stats and rendering will account for it.
enum class EquipSlot { Head, Body, Hands, Feet, Weapon, Count };
inline constexpr int EQUIP_SLOT_COUNT = static_cast<int>(EquipSlot::Count);

// Broad weapon families. Drives animation + reach behaviour, not tuning.
enum class WeaponClass { OneHanded, TwoHanded, Polearm, Axe, Ranged };

// Mount & Blade style four-way attack / guard directions.
enum class AttackDir { Up, Down, Left, Right };

// How a campaign party behaves when it has no immediate objective.
enum class PartyBehavior { Passive, Patrol, Aggressive };

// What a campaign party is *currently doing* — derived every tick from its
// behaviour, its surroundings and its fatigue. Purely observational: the map
// label, the party tooltip and the harness all read it, and it never feeds back
// into balance. Add a state here and give it a name in PartyStateName().
enum class PartyState { Patrolling, Travelling, Pursuing, Fleeing, Resting, Besieging, Engaged };

inline const char* PartyStateName(PartyState s) {
    switch (s) {
        case PartyState::Patrolling:  return "patrolling";
        case PartyState::Travelling:  return "travelling";
        case PartyState::Pursuing:    return "pursuing";
        case PartyState::Fleeing:     return "fleeing";
        case PartyState::Resting:     return "resting";
        case PartyState::Besieging:   return "besieging";
        case PartyState::Engaged:     return "engaged";
    }
    return "?";
}

// What a quest asks of the player (direction F4). Extend here and handle the
// new shape where quests are assigned/completed in campaign/town code.
enum class QuestType { HuntBandits, DeliverGrain };

// Which side a combatant fights for in a battle.
enum class Team { Player, Enemy };

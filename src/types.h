#pragma once
#include "raylib.h"

// ---------------------------------------------------------------------------
// Core enums shared across the whole game. These describe *kinds* of things,
// never balance values. Extend the enums here and the registries in content.*
// pick the new kinds up.
// ---------------------------------------------------------------------------

// Top-level screen / flow state.
enum class Screen { Title, Campaign, Settlement, Party, Inventory, Character, Battle, BattleResult };

// Kind of settlement on the overworld. Drives the map icon and the greeting /
// available actions on the settlement menu, not balance.
enum class SettlementType { Village, Town, Castle };

// Equipment slots a character can fill. Add coverage here (e.g. Legs, Cape)
// and both stats and rendering will account for it.
enum class EquipSlot { Head, Body, Hands, Feet, Weapon, Count };
inline constexpr int EQUIP_SLOT_COUNT = static_cast<int>(EquipSlot::Count);

// Broad weapon families. Drives animation + reach behaviour, not tuning.
enum class WeaponClass { OneHanded, TwoHanded, Polearm, Ranged };

// Mount & Blade style four-way attack / guard directions.
enum class AttackDir { Up, Down, Left, Right };

// How a campaign party behaves when it has no immediate objective.
enum class PartyBehavior { Passive, Patrol, Aggressive };

// Which side a combatant fights for in a battle.
enum class Team { Player, Enemy };

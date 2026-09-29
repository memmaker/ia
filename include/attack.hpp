// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#ifndef ATTACK_HPP
#define ATTACK_HPP

#include "global.hpp"
#include "query.hpp"

namespace item
{
class Wpn;
}  // namespace item

namespace actor
{
class Actor;
}  // namespace actor

struct P;

namespace attack
{
enum class AttackSource
{
    normal,
    // Attack does not spend time, status effects such as terrified cannot prevent melee attacks.
    magical,
};

void melee(
    actor::Actor* attacker,
    const P& origin,
    const P& aim_pos,
    item::Wpn& wpn,
    AttackSource attack_source = AttackSource::normal);

DidAction ranged(
    actor::Actor* attacker,
    const P& origin,
    const P& aim_pos,
    item::Wpn& wpn);

void ranged_hit_chance(
    const actor::Actor& attacker,
    const actor::Actor& defender,
    const item::Wpn& wpn);

BinaryAnswer query_player_attack_mon_with_ranged_wpn(
    const item::Wpn& wpn,
    const actor::Actor& mon);

}  // namespace attack

#endif  // ATTACK_HPP

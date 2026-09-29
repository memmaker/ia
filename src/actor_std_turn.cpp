// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#include "actor_std_turn.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <ostream>
#include <vector>

#include "actor.hpp"
#include "actor_data.hpp"
#include "actor_hit.hpp"
#include "actor_player_state.hpp"
#include "ai.hpp"
#include "array2.hpp"
#include "debug.hpp"
#include "game_time.hpp"
#include "global.hpp"
#include "inventory.hpp"
#include "item.hpp"
#include "item_data.hpp"
#include "item_explosive.hpp"
#include "map.hpp"
#include "msg_log.hpp"
#include "player_bon.hpp"
#include "property.hpp"
#include "property_data.hpp"
#include "property_factory.hpp"
#include "property_handler.hpp"
#include "random.hpp"
#include "smell.hpp"

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------
static bool mon_ai_allows_adopting_leader(const actor::Actor& mon)
{
    // Only allow adopting a leader if:
    // * the monster follows leaders, and
    // * does not stay in their lair
    //
    // (They should actually follow their new leader around).
    //
    return (
        mon.m_data->ai[(size_t)actor::AiId::moves_to_leader] &&
        !mon.m_data->ai[(size_t)actor::AiId::moves_to_lair]);
}

static void try_make_mon_join_other_mon(actor::Actor& mon)
{
    if (!mon_ai_allows_adopting_leader(mon)) {
        return;
    }

    if (!actor::is_alive(mon)) {
        return;
    }

    // Unique/named monsters should not follow common monsters.
    if (mon.m_data->is_unique) {
        return;
    }

    // Roll to try adopting a leader.
    if (!rnd::percent(25)) {
        return;
    }

    // Don't allow adopting a leader if monster is already in a group (i.e. has leader or has
    // followers).
    if (!actor::other_actors_in_same_group(&mon).empty()) {
        return;
    }

    std::vector<actor::Actor*> leader_bucket;

    for (actor::Actor* const actor : game_time::g_actors) {
        if ((actor == &mon) ||
            !actor::is_alive(*actor) ||
            // The potential leader must not have their own leader:
            actor->m_leader ||
            // The potential leader must also have the required AI behavior:
            !mon_ai_allows_adopting_leader(*actor) ||
            // Do not follow monsters that teleport all over the map:
            actor->m_properties.has(prop::Id::teleports)) {
            continue;
        }

        if (!mon.m_pos.is_adjacent(actor->m_pos)) {
            continue;
        }

        leader_bucket.push_back(actor);
    }

    if (leader_bucket.empty()) {
        return;
    }

    actor::Actor* const leader = rnd::element(leader_bucket);

    mon.m_leader = leader;

    TRACE
        << "Monster '" << actor::name_a(mon)
        << "' (" << &mon << ")"
        << " adopted monster '" << actor::name_a(*leader)
        << "' (" << leader << ") as leader"
        << "\n";
}

static int calc_player_turns_per_hp_regen_rate()
{
    int nr_turns_per_hp = 0;

    // Rapid Recoverer trait affects hp regen?
    if (player_bon::has_trait(TraitId::rapid_recoverer)) {
        nr_turns_per_hp = 3;
    }
    else {
        nr_turns_per_hp = 20;
    }

    // Wounds affect hp regen?
    int nr_wounds = 0;

    if (map::g_player->m_properties.has(prop::Id::wound)) {
        auto* const wound =
            static_cast<prop::Wound*>(
                map::g_player->m_properties.prop(prop::Id::wound));

        nr_wounds = wound->nr_wounds();
    }

    if (player_bon::has_trait(TraitId::survivalist)) {
        nr_wounds /= 2;
    }

    const int wound_turns_per_hp_penalty = nr_wounds * 4;

    nr_turns_per_hp += wound_turns_per_hp_penalty;

    // Items affect hp regen?
    for (const InvSlot& slot : map::g_player->m_inv.m_slots) {
        if (slot.item) {
            nr_turns_per_hp += slot.item->hp_regen_change(InvType::slots);
        }
    }

    for (const item::Item* const item : map::g_player->m_inv.m_backpack) {
        nr_turns_per_hp += item->hp_regen_change(InvType::backpack);
    }

    nr_turns_per_hp = std::max(1, nr_turns_per_hp);

    return nr_turns_per_hp;
}

static int calc_mon_turns_per_hp_regen_rate()
{
    return 30;
}

static void regen_hp(actor::Actor& actor)
{
    if (game_time::turn_nr() <= 1) {
        return;
    }

    if (!actor::is_alive(actor)) {
        return;
    }

    const bool is_below_max_hp = actor.m_hp >= actor::max_hp(actor);

    if (is_below_max_hp) {
        return;
    }

    const std::vector<prop::Id> props_preventing_regen = {
        prop::Id::poisoned,
        prop::Id::disabled_hp_regen,
    };

    const bool has_prop_preventing_regen = actor.m_properties.has_any(props_preventing_regen);

    if (has_prop_preventing_regen) {
        return;
    }

    // Well, if player Ghouls are not regenerating hit points, then neither should monster
    // Ghouls?
    const bool is_ghoul =
        actor::is_player(&actor)
        ? (player_bon::bg() == Bg::ghoul)
        : actor.m_data->is_ghoul;

    if (is_ghoul) {
        return;
    }

    // OK, the actor can regenerate hit points. Check if it is time to regenerate now.

    const int nr_turns_per_hp =
        actor::is_player(&actor)
        ? calc_player_turns_per_hp_regen_rate()
        : calc_mon_turns_per_hp_regen_rate();

    if ((game_time::turn_nr() % nr_turns_per_hp) != 0) {
        return;
    }

    ++actor.m_hp;
}

static Range calc_nr_turns_range_to_recharge_spell_shield()
{
    Range range;

    if (player_bon::has_trait(TraitId::mighty_spirit)) {
        range = {25, 50};
    }
    else if (player_bon::has_trait(TraitId::strong_spirit)) {
        range = {75, 100};
    }
    else {
        range = {125, 150};
    }

    // Halved number of turns due to the Talisman of Arcane Void?
    if (map::g_player->m_inv.has_item_in_backpack(item::Id::talisman_of_arcane_void)) {
        range.min /= 2;
        range.max /= 2;
    }

    return range;
}

static void player_regen_spell_shield()
{
    if (map::g_player->m_properties.has(prop::Id::r_spell)) {
        // Player already has spell resistance. Keep resetting the countdown to
        // "uninitialized" while in this state, and do nothing else. This will trigger a
        // reroll of the duration when the countdown can begin again.
        actor::player_state::g_nr_turns_until_r_spell = -1;

        return;
    }

    // Spell shield not currently active.

    if (!player_bon::has_trait(TraitId::stout_spirit)) {
        return;
    }

    // Player has at least stout spirit.

    if (actor::player_state::g_nr_turns_until_r_spell <= 0) {
        // Cooldown has finished, OR countdown not initialized.

        if (actor::player_state::g_nr_turns_until_r_spell == 0) {
            // Cooldown has finished
            prop::Prop* prop = prop::make(prop::Id::r_spell);

            prop->set_indefinite();

            map::g_player->m_properties.apply(prop);
        }

        actor::player_state::g_nr_turns_until_r_spell =
            calc_nr_turns_range_to_recharge_spell_shield()
                .roll();
    }

    if (!map::g_player->m_properties.has(prop::Id::r_spell) &&
        (actor::player_state::g_nr_turns_until_r_spell > 0)) {
        // Spell resistance is in cooldown state, decrement number of remaining turns.
        --actor::player_state::g_nr_turns_until_r_spell;
    }
}

static void player_regen_meditative_focused()
{
    if (map::g_player->m_properties.has(prop::Id::meditative_focused) ||
        map::g_player->m_properties.has(prop::Id::frenzied)) {
        // Player is already focused, or is frenzied. Keep resetting the countdown to
        // "uninitialized" while in this state, and do nothing else. This will trigger a
        // reroll of the duration when the countdown can begin again.
        actor::player_state::g_nr_turns_until_meditative_focused = -1;

        return;
    }

    // Meditative focused not currently active.

    if (!player_bon::has_trait(TraitId::meditative)) {
        return;
    }

    // Player has meditative trait.

    int& nr_turns_until_focused = actor::player_state::g_nr_turns_until_meditative_focused;

    if (nr_turns_until_focused <= 0) {
        // Cooldown has finished, OR countdown not initialized.

        if (nr_turns_until_focused == 0) {
            // Cooldown has finished
            prop::Prop* prop = prop::make(prop::Id::meditative_focused);

            prop->set_indefinite();

            map::g_player->m_properties.apply(prop);
        }

        const auto duration_range =
            player_bon::has_trait(TraitId::sage)
            ? Range(75, 100)
            : Range(125, 150);

        nr_turns_until_focused = duration_range.roll();
    }

    if (!map::g_player->m_properties.has(prop::Id::meditative_focused) &&
        (nr_turns_until_focused > 0)) {
        // Meditative focused is in cooldown state, decrement number of remaining turns.
        --nr_turns_until_focused;
    }
}

static void player_std_turn()
{
#ifndef NDEBUG
    // Disease and infection should not be active at the same time
    ASSERT(
        !map::g_player->m_properties.has(prop::Id::diseased) ||
        !map::g_player->m_properties.has(prop::Id::infected));
#endif  // NDEBUG

    if (!actor::is_alive(*map::g_player)) {
        return;
    }

    player_regen_spell_shield();

    player_regen_meditative_focused();

    if (actor::player_state::g_active_explosive) {
        actor::player_state::g_active_explosive->on_std_turn_player_hold_ignited();

        if (!actor::is_alive(*map::g_player)) {
            return;
        }
    }

    regen_hp(*map::g_player);
}

static void mon_std_turn(actor::Actor& mon)
{
    try_make_mon_join_other_mon(mon);

    smell::put_smell_for_mon(mon);

    // Countdown all spell cooldowns
    for (actor::MonSpell& spell : mon.m_mon_spells) {
        int& cooldown = spell.cooldown;

        if (cooldown > 0) {
            --cooldown;
        }
    }

    // NOTE: Monsters try to detect the player visually on standard turns, otherwise very fast
    // monsters are much better at finding the player.
    const bool should_look =
        actor::is_alive(mon) &&
        mon.m_data->ai[(size_t)actor::AiId::looks] &&
        !actor::is_player(mon.m_leader) &&
        !map::g_player->m_properties.has(prop::Id::sanctuary) &&
        (actor::is_player(mon.m_ai_state.target) || !mon.m_ai_state.target);

    if (should_look) {
        ai::info::look(mon);
    }

    regen_hp(mon);
}

static void std_turn_common(actor::Actor& actor)
{
    // Do light damage if in lit cell
    if (map::g_light.at(actor.m_pos)) {
        actor::hit(actor, 1, DmgType::light, nullptr);
    }

    if (!actor::is_alive(actor)) {
        return;
    }

    // Slowly decrease current HP/spirit if above max
    const int decr_above_max_n_turns = 7;

    // Monsters decrement their HP every standard turn when above max (so that things draining
    // max HP will have an affect faster), while the player can have HP above max longer.
    bool decr_this_turn = true;

    if (actor::is_player(&actor)) {
        decr_this_turn = ((game_time::turn_nr() % decr_above_max_n_turns) == 0);
    }

    const bool is_hp_above_max = (actor.m_hp > actor::max_hp(actor));

    if (is_hp_above_max && decr_this_turn) {
        TRACE << actor.m_hp << "\n";

        --actor.m_hp;
    }

    const bool is_sp_above_max = (actor.m_sp > actor::max_sp(actor));

    if (is_sp_above_max && decr_this_turn) {
        --actor.m_sp;
    }

    // Regenerate spirit
    int regen_sp_n_turns = 18;

    if (actor::is_player(&actor)) {
        if (player_bon::has_trait(TraitId::stout_spirit)) {
            regen_sp_n_turns -= 4;
        }

        if (player_bon::has_trait(TraitId::strong_spirit)) {
            regen_sp_n_turns -= 4;
        }

        if (player_bon::has_trait(TraitId::mighty_spirit)) {
            regen_sp_n_turns -= 4;
        }
    }
    else {
        // Is monster

        // Monsters regen spirit very quickly, so spell casters doesn't suddenly get
        // completely prevented from casting spells.
        regen_sp_n_turns = 1;
    }

    const bool regen_sp_this_turn = ((game_time::turn_nr() % regen_sp_n_turns) == 0);

    if (regen_sp_this_turn) {
        actor::restore_sp(
            actor,
            1,
            actor::AllowRestoreAboveMax::no,
            Verbose::no);
    }
}

// -----------------------------------------------------------------------------
// actor
// -----------------------------------------------------------------------------
namespace actor
{
void std_turn(Actor& actor)
{
    std_turn_common(actor);

    if (actor::is_player(&actor)) {
        player_std_turn();
    }
    else {
        mon_std_turn(actor);
    }
}

}  // namespace actor

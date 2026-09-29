// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#include "spells.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <ostream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "actor.hpp"
#include "actor_data.hpp"
#include "actor_death.hpp"
#include "actor_eat.hpp"
#include "actor_factory.hpp"
#include "actor_hit.hpp"
#include "actor_player_state.hpp"
#include "actor_see.hpp"
#include "array2.hpp"
#include "attack.hpp"
#include "audio.hpp"
#include "audio_data.hpp"
#include "colors.hpp"
#include "common_text.hpp"
#include "config.hpp"
#include "debug.hpp"
#include "direction.hpp"
#include "draw_blast.hpp"
#include "drop.hpp"
#include "explosion.hpp"
#include "flood.hpp"
#include "fov.hpp"
#include "game_time.hpp"
#include "gfx.hpp"
#include "global.hpp"
#include "inventory.hpp"
#include "inventory_handling.hpp"
#include "io.hpp"
#include "item.hpp"
#include "item_data.hpp"
#include "item_factory.hpp"
#include "item_weapon.hpp"
#include "knockback.hpp"
#include "map.hpp"
#include "map_parsing.hpp"
#include "marker.hpp"
#include "misc.hpp"
#include "msg_log.hpp"
#include "pathfind.hpp"
#include "player_bon.hpp"
#include "player_spells.hpp"
#include "pos.hpp"
#include "property.hpp"
#include "property_data.hpp"
#include "property_factory.hpp"
#include "property_handler.hpp"
#include "rect.hpp"
#include "sound.hpp"
#include "state.hpp"
#include "teleport.hpp"
#include "terrain.hpp"
#include "terrain_data.hpp"
#include "terrain_door.hpp"
#include "terrain_factory.hpp"
#include "terrain_trap.hpp"
#include "text_format.hpp"
#include "viewport.hpp"
#include "wpn_dmg.hpp"

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------
static const std::unordered_map<std::string, SpellId> s_str_to_spell_id_map = {
    {"SPELL_AURA_OF_DECAY", SpellId::aura_of_decay},
    {"SPELL_AZA_GAZE", SpellId::aza_gaze},
    {"SPELL_BLESS", SpellId::bless},
    {"SPELL_BLIND", SpellId::blind},
    {"SPELL_BURN", SpellId::burn},
    {"SPELL_CANCELLATION", SpellId::cancellation},
    {"SPELL_CATACLYSM", SpellId::cataclysm},
    {"SPELL_CLAIRVOYANCE", SpellId::clairvoyance},
    {"SPELL_BRILLIANCE", SpellId::brilliance},
    {"SPELL_CLEANSING_FIRE", SpellId::cleansing_fire},
    {"SPELL_CONTROL_OBJECT", SpellId::control_object},
    {"SPELL_CRIMSON_PASSAGE", SpellId::crimson_passage},
    {"SPELL_CURSE", SpellId::curse},
    {"SPELL_DARKBOLT", SpellId::darkbolt},
    {"SPELL_DEAFEN", SpellId::deafen},
    {"SPELL_ENFEEBLE", SpellId::enfeeble},
    {"SPELL_ERUDITION", SpellId::erudition},
    {"SPELL_EXPULSION", SpellId::expulsion},
    {"SPELL_FORCE_BOLT", SpellId::force_bolt},
    {"SPELL_FRENZY", SpellId::frenzy},
    {"SPELL_GNAWING_TORRENT", SpellId::gnawing_torrent},
    {"SPELL_HASTE", SpellId::haste},
    {"SPELL_HEAL", SpellId::heal},
    {"SPELL_HEAL_OTHERS", SpellId::heal_others},
    {"SPELL_IDENTIFY", SpellId::identify},
    {"SPELL_KNOCKBACK", SpellId::knockback},
    {"SPELL_LIGHT", SpellId::light},
    {"SPELL_MIRROR_IMAGE", SpellId::mirror_images},
    {"SPELL_MI_GO_HYPNO", SpellId::mi_go_hypno},
    {"SPELL_PESTILENCE", SpellId::pestilence},
    {"SPELL_POISON", SpellId::poison},
    {"SPELL_PREMONITION", SpellId::premonition},
    {"SPELL_PROJECTED_STRIKE", SpellId::projected_strike},
    {"SPELL_PURGE", SpellId::purge},
    {"SPELL_SANCTUARY", SpellId::sanctuary},
    {"SPELL_SEE_INVIS", SpellId::see_invis},
    {"SPELL_SLOW", SpellId::slow},
    {"SPELL_SPELL_SHIELD", SpellId::spell_shield},
    {"SPELL_SUMMON_RANDOM", SpellId::summon_random},
    {"SPELL_SUMMON_TENTACLES", SpellId::summon_tentacles},
    {"SPELL_SUMMON_WATER_CREATURE", SpellId::summon_water_creature},
    {"SPELL_TELEPORT", SpellId::teleport},
    {"SPELL_TEMPORAL_ECHO", SpellId::temporal_echo},
    {"SPELL_TERRIFY", SpellId::terrify},
    {"SPELL_THREAT_PROJECTION", SpellId::threat_projection},
    {"SPELL_TRANSMUT", SpellId::transmut},
};

static const std::unordered_map<std::string, SpellSkill> s_str_to_spell_skill_map = {
    {"SPELLSKILL_BASIC", SpellSkill::basic},
    {"SPELLSKILL_EXPERT", SpellSkill::expert},
    {"SPELLSKILL_MASTER", SpellSkill::master},
    {"SPELLSKILL_TRANSCENDENT", SpellSkill::transcendent},
};

static const std::unordered_map<SpellDomain, ShockSrc> s_spell_domain_to_shock_type_map = {
    {SpellDomain::blood, ShockSrc::cast_intr_spell_blood},
    {SpellDomain::channeling, ShockSrc::cast_intr_spell_channeling},
    {SpellDomain::corruption, ShockSrc::cast_intr_spell_corruption},
    {SpellDomain::illusion, ShockSrc::cast_intr_spell_illusion},
    {SpellDomain::mind, ShockSrc::cast_intr_spell_mind},
    {SpellDomain::time, ShockSrc::cast_intr_spell_time},
    {SpellDomain::warding, ShockSrc::cast_intr_spell_warding},
    // NOTE: Not all spells belong to a domain:
    {SpellDomain::END, ShockSrc::cast_intr_spell_general},
};

static const std::string s_not_alerting_mon_descr =
    "Casting this spell does not alert the victim to the caster's presence.";

static DmgType s_bolt_dmg_type = DmgType::blunt;

namespace spell_side_effects
{
struct Context
{
    Context(actor::Actor& spell_caster, const std::vector<P>& caster_nearby_positions) :
        caster(spell_caster),
        nearby_positions(caster_nearby_positions) {}

    actor::Actor& caster;
    const std::vector<P>& nearby_positions;
};

static void print_side_effect_trigger_message()
{
    msg_log::add("An unexpected effect was induced by the spell.");
}

static void side_effect_spawn_monsters(const Context& context)
{
    TRACE_FUNC_BEGIN;

    const P p = rnd::element(context.nearby_positions);

    const std::string id = "MON_TENTACLE_CLUSTER";

    actor::MonSpawnResult spawned = actor::spawn(p, {id});

    bool printed_msg = false;

    for (auto* const actor : spawned.monsters) {
        if (!printed_msg) {
            print_side_effect_trigger_message();
            printed_msg = true;
        }

        prop::Prop* const conflicted = prop::make(prop::Id::conflict);

        conflicted->set_indefinite();

        actor->m_properties.apply(
            conflicted,
            prop::PropSrc::intr,
            false,
            Verbose::no);

        prop::Prop* const waiting = prop::make(prop::Id::waiting);

        waiting->set_duration(2);

        actor->m_properties.apply(waiting);

        prop::Prop* const summoned = prop::make(prop::Id::summoned);

        summoned->set_duration(rnd::range(3, 20));

        actor->m_properties.apply(summoned);
    }

    map::update_vision();
    actor::make_player_aware_seen_monsters();

    TRACE_FUNC_END;
}

static void side_effect_swap_wall_floor(const Context& context)
{
    TRACE_FUNC_BEGIN;

    print_side_effect_trigger_message();

    Array2<bool> blocked(map::dims());

    map_parsers::BlocksWalking(ParseActors::no)
        .run(blocked, blocked.rect());

    const std::vector<terrain::Id> free_terrains = {
        terrain::Id::door,
    };

    for (const P& p : blocked.rect().positions()) {
        const bool is_free_terrain =
            map_parsers::IsAnyOfTerrains(free_terrains)
                .run(p);

        if (is_free_terrain) {
            blocked.at(p) = false;
        }
    }

    Array2<bool> has_actor(map::dims());

    for (auto* actor : game_time::g_actors) {
        if (actor->m_state != ActorState::destroyed) {
            has_actor.at(actor->m_pos) = true;
        }
    }

    for (const auto& p : context.nearby_positions) {
        if (!map::is_pos_inside_outer_walls(p) ||
            has_actor.at(p) ||
            map::g_items.at(p) ||
            !rnd::one_in(14)) {
            continue;
        }

        const auto terrain_id = map::g_terrain.at(p)->id();

        if (terrain_id == terrain::Id::wall) {
            blocked.at(p) = false;

            if (map_parsers::is_map_connected(blocked)) {
                map::update_terrain(terrain::make(terrain::Id::floor, p));
            }
            else {
                // Map would not be connected
                blocked.at(p) = true;
            }
        }
        else if (terrain_id == terrain::Id::floor) {
            blocked.at(p) = true;

            if (map_parsers::is_map_connected(blocked)) {
                map::update_terrain(terrain::make(terrain::Id::wall, p));
            }
            else {
                // Map would not be connected
                blocked.at(p) = false;
            }
        }
    }

    TRACE_FUNC_END;
}  // swap_wall_floor

static void side_effect_ignite_terrain(const Context& context)
{
    TRACE_FUNC_BEGIN;

    Array2<bool> has_actor(map::dims());

    for (auto* actor : game_time::g_actors) {
        if (actor->m_state != ActorState::destroyed) {
            has_actor.at(actor->m_pos) = true;
        }
    }

    bool printed_msg = false;
    for (const auto& p : context.nearby_positions) {
        if (has_actor.at(p)) {
            continue;
        }

        if (!rnd::one_in(14)) {
            continue;
        }

        if (!printed_msg) {
            print_side_effect_trigger_message();
            printed_msg = true;
        }

        terrain::Terrain* const terrain = map::g_terrain.at(p);

        terrain->hit(DmgType::fire, nullptr);
    }

    TRACE_FUNC_END;
}

static void side_effect_open_close_doors(const Context& context)
{
    TRACE_FUNC_BEGIN;

    // Open or close doors
    const bool should_open = (bool)rnd::coin_toss();

    Array2<bool> has_actor(map::dims());

    for (actor::Actor* actor : game_time::g_actors) {
        if (actor->m_state != ActorState::destroyed) {
            has_actor.at(actor->m_pos) = true;
        }
    }

    bool printed_msg = false;
    for (const P& pos : context.nearby_positions) {
        if (has_actor.at(pos) || map::g_items.at(pos)) {
            continue;
        }

        terrain::Terrain* const terrain = map::g_terrain.at(pos);

        if (terrain->id() != terrain::Id::door) {
            continue;
        }

        if (static_cast<terrain::Door*>(terrain)->is_warded()) {
            continue;
        }

        if (!printed_msg) {
            print_side_effect_trigger_message();
            printed_msg = true;
        }

        // NOTE: Warded doors are skipped, so it's OK to just run
        // open/close here.
        if (should_open) {
            terrain->open(nullptr);
        }
        else {
            terrain->close(nullptr);
        }
    }

    TRACE_FUNC_END;
}

static void side_effect_flay_human(const Context& context)
{
    TRACE_FUNC_BEGIN;

    std::vector<actor::Actor*> actors = actor::seen_actors(context.caster);

    actors.push_back(&context.caster);

    rnd::shuffle(actors);

    actor::Actor* target_actor = nullptr;

    for (actor::Actor* const actor : actors) {
        const actor::ActorData* const actor_data = actor->m_data;

        const prop::PropHandler& properties = actor->m_properties;

        // NOTE: The target of the spell side effect may be the caster itself, if caster is
        // a monster.
        if (!actor::is_player(actor) &&
            actor::is_alive(*actor) &&
            actor_data->is_humanoid &&
            actor_data->can_leave_corpse &&
            (actor_data->mon_shock_lvl <= MonShockLvl::frightening) &&
            !actor->m_properties.has(prop::Id::undead) &&
            !actor_data->is_unique &&
            !properties.has(prop::Id::ethereal) &&
            !properties.has(prop::Id::possessed_by_zuul) &&
            !properties.has(prop::Id::spawns_zombie_parts_on_destroyed)) {
            target_actor = actor;

            break;
        }
    }

    if (!target_actor) {
        return;
    }

    print_side_effect_trigger_message();

    if (actor::can_player_see_actor(*target_actor)) {
        const std::string name =
            text_format::first_to_upper(
                actor::name_the(*target_actor));

        msg_log::add(name + " is suddenly flayed alive!");
    }

    actor::kill(*target_actor, IsDestroyed::yes, AllowGore::yes, AllowDropItems::yes);

    actor::spawn(target_actor->m_pos, {"MON_INTESTINAL_MASS"});

    TRACE_FUNC_END;
}

static void side_effect_create_water(const Context& context)
{
    TRACE_FUNC_BEGIN;

    bool printed_msg = false;

    for (const auto& p : context.nearby_positions) {
        if ((map::g_terrain.at(p)->id() != terrain::Id::floor) ||
            !rnd::one_in(8)) {
            continue;
        }

        if (!printed_msg) {
            print_side_effect_trigger_message();
            printed_msg = true;
        }

        auto* const liquid =
            static_cast<terrain::Liquid*>(
                terrain::make(
                    terrain::Id::liquid,
                    p));

        liquid->m_type = LiquidType::water;

        map::update_terrain(liquid);
    }

    TRACE_FUNC_END;
}

static void side_effect_alter_env(const Context& context)
{
    TRACE_FUNC_BEGIN;

    print_side_effect_trigger_message();

    prop::run_alter_env_effect(context.caster.m_pos);

    TRACE_FUNC_END;
}

static void side_effect_create_doors(const Context& context)
{
    TRACE_FUNC_BEGIN;

    const auto adj_door_checker = map_parsers::AnyAdjIsAnyOfTerrains(terrain::Id::door);
    const auto adj_floor_checker = map_parsers::AnyAdjIsAnyOfTerrains(terrain::Id::floor);

    bool printed_msg = false;
    for (const auto& p : context.nearby_positions) {
        const auto id = map::g_terrain.at(p)->id();

        if (!rnd::one_in(2) ||
            (id != terrain::Id::wall) ||
            adj_door_checker.run(p) ||
            !adj_floor_checker.run(p)) {
            continue;
        }

        if (!printed_msg) {
            print_side_effect_trigger_message();
            printed_msg = true;
        }

        auto* const mimic = terrain::make(terrain::Id::wall, p);

        auto* const door =
            static_cast<terrain::Door*>(
                terrain::make(
                    terrain::Id::door,
                    p));

        door->set_mimic_terrain(mimic);

        door->init_type_and_state(
            terrain::DoorType::wood,
            terrain::DoorSpawnState::closed);

        map::update_terrain(door);
    }

    TRACE_FUNC_END;
}

static void side_effect_create_dark_void(const Context& context)
{
    TRACE_FUNC_BEGIN;

    std::vector<P> sorted_positions = context.nearby_positions;

    std::sort(
        std::begin(sorted_positions),
        std::end(sorted_positions),
        [context](const auto& p1, const auto& p2) {
            const auto caster_p = context.caster.m_pos;

            const int d1 = king_dist(p1, caster_p);
            const int d2 = king_dist(p2, caster_p);

            return d1 < d2;
        });

    Array2<bool> blocked(map::dims());

    map_parsers::BlocksWalking(ParseActors::no)
        .run(blocked, blocked.rect());

    const std::vector<terrain::Id> free_terrains = {
        terrain::Id::door,
    };

    for (const P& p : blocked.rect().positions()) {
        const bool is_free_terrain = map_parsers::IsAnyOfTerrains(free_terrains).run(p);

        if (is_free_terrain) {
            blocked.at(p) = false;
        }
    }

    print_side_effect_trigger_message();

    for (const auto& p : sorted_positions) {
        if (!map::is_pos_inside_outer_walls(p)) {
            continue;
        }

        map::g_dark.at(p) = true;
        map::g_light.at(p) = false;

        if (map::g_terrain.at(p)->id() == terrain::Id::wall) {
            blocked.at(p) = false;

            if (map_parsers::is_map_connected(blocked)) {
                map::update_terrain(
                    terrain::make(terrain::Id::floor, p));
            }
            else {
                blocked.at(p) = true;
            }
        }
    }

    TRACE_FUNC_END;
}

static void side_effect_push_statue(const Context& context)
{
    TRACE_FUNC_BEGIN;

    for (const auto& p : context.nearby_positions) {
        auto* const terrain = map::g_terrain.at(p);

        if (terrain->id() != terrain::Id::statue) {
            continue;
        }

        print_side_effect_trigger_message();

        auto* const statue = static_cast<terrain::Statue*>(terrain);

        const auto direction = dir_utils::dir(rnd::element(dir_utils::g_dir_list));

        statue->topple(direction);

        break;
    }

    TRACE_FUNC_END;
}

using SpellSideEffect = std::function<void(const Context&)>;

WeightedItems<SpellSideEffect> s_spell_side_effects {
    {
        side_effect_create_dark_void,
        side_effect_create_doors,
        side_effect_alter_env,
        side_effect_create_water,
        side_effect_flay_human,
        side_effect_ignite_terrain,
        side_effect_open_close_doors,
        side_effect_push_statue,
        side_effect_spawn_monsters,
        side_effect_swap_wall_floor,
    },
    {
        10,  // create_dark_void
        15,  // create_doors
        30,  // create_trees
        30,  // create_water
        50,  // flay_human
        50,  // ignite_terrain
        90,  // open_close_doors
        90,  // push_statue
        50,  // spawn_monsters
        70,  // swap_wall_floor
    }};

static void run_random_side_effect(actor::Actor& caster)
{
    // Run a random side effect.
    const int d = 3;

    const R rect(
        {std::max(0, caster.m_pos.x - d),
         std::max(0, caster.m_pos.y - d)},
        {std::min(map::w() - 1, caster.m_pos.x + d),
         std::min(map::h() - 1, caster.m_pos.y + d)});

    std::vector<P> nearby_positions = rect.positions();

    rnd::shuffle(nearby_positions);

    const SpellSideEffect& side_effect_function = s_spell_side_effects.roll();

    TRACE << "Running spell side effect" << "\n";

    side_effect_function({caster, nearby_positions});
}

}  // namespace spell_side_effects

static std::string get_noise_descr(const bool is_noisy)
{
    std::string str =
        is_noisy
        ? "Casting this spell requires making sounds."
        : "The spell can be cast silently.";

    return str;
}

static std::string get_skill_descr(
    const SpellSkill skill,
    const SpellSrc source)
{
    std::string str =
        "The spell can be cast at " +
        spells::skill_to_str(skill) +
        " level";

    std::vector<std::string> bon_words;

    const prop::PropHandler& properties = map::g_player->m_properties;

    if (source == SpellSrc::manuscript) {
        bon_words.emplace_back("manuscript");
    }

    if (player_spells::is_getting_altar_bonus()) {
        bon_words.emplace_back("altar");
    }

    if (properties.has(prop::Id::erudition)) {
        bon_words.emplace_back("erudition");
    }

    if (map::g_player->m_inv.has_item_in_backpack(item::Id::necronomicon)) {
        bon_words.emplace_back("necronomicon");
    }

    for (size_t i = 0; i < bon_words.size(); ++i) {
        if (i == 0) {
            str += " (";
        }

        str += bon_words[i];

        if (i < (bon_words.size() - 1)) {
            str += ", ";
        }
        else {
            str += ")";
        }
    }

    str += ".";

    return str;
}

static void end_properties_for_casting_spell(
    actor::Actor& caster,
    const SpellId spell_id)
{
    // End cloaking (unless invisibility was cast now).
    if (spell_id != SpellId::invis) {
        caster.m_properties.end_prop(prop::Id::cloaked);
    }

    // End focused
    caster.m_properties.end_prop(prop::Id::meditative_focused);

    // End erudition (unless that was the spell that was cast now).
    if (spell_id != SpellId::erudition) {
        const auto* const prop =
            caster.m_properties.prop(prop::Id::erudition);

        const bool should_end =
            prop &&
            static_cast<const prop::Erudition*>(prop)->should_end_on_spell_cast();

        if (should_end) {
            caster.m_properties.end_prop(prop::Id::erudition);
        }
    }
}

static std::string generate_mon_cast_sound_msg(const actor::Actor& caster)
{
    std::string spell_msg = caster.m_data->spell_msg_sound;

    if (spell_msg.empty()) {
        return "";
    }

    const bool is_mon_seen = actor::can_player_see_actor(caster);

    const std::string mon_name =
        is_mon_seen
        ? text_format::first_to_upper(actor::name_the(caster))
        : (caster.m_data->is_humanoid ? "Someone" : "Something");

    spell_msg = mon_name + " " + spell_msg;

    return spell_msg;
}

static std::string generate_mon_cast_visual_msg(const actor::Actor& caster)
{
    // NOTE: This assumes that the monster is seen.

    std::string spell_msg = caster.m_data->spell_msg_visual;

    if (spell_msg.empty()) {
        return "";
    }

    const std::string mon_name = text_format::first_to_upper(actor::name_the(caster));

    spell_msg = mon_name + " " + spell_msg;

    return spell_msg;
}

static int absorb_sp_cost_with_exorcist_fervor(int sp_cost)
{
    const int missing_sp = (sp_cost - map::g_player->m_sp) + 1;

    if (missing_sp > 0) {
        const int cost_reduction =
            std::min(
                missing_sp,
                actor::player_state::g_exorcist_fervor);

        sp_cost -= cost_reduction;

        actor::player_state::g_exorcist_fervor -= cost_reduction;
    }

    return sp_cost;
}

static void apply_spell_cost(
    actor::Actor& caster,
    int cost,
    const SpellCostType cost_type)
{
    switch (cost_type) {
    case SpellCostType::spirit: {
        if (actor::is_player(&caster) && player_bon::is_bg(Bg::exorcist)) {
            cost = absorb_sp_cost_with_exorcist_fervor(cost);
        }

        if (cost > 0) {
            actor::hit_sp(caster, cost, nullptr, Verbose::no);
        }
    } break;

    case SpellCostType::hit_points: {
        actor::hit(caster, cost, DmgType::pure, nullptr, AllowWound::no);
    } break;
    }
}

static bool should_give_regen_from_flagellant_trait(
    const actor::Actor& caster,
    const int hp_before_casting,
    const SpellDomain spell_domain)
{
    return (
        actor::is_player(&caster) &&
        player_bon::has_trait(TraitId::galvanization) &&
        (spell_domain == SpellDomain::blood) &&
        (caster.m_hp < hp_before_casting));
}

static void apply_regen_from_flagellant_trait()
{
    prop::Prop* const regen = prop::make(prop::Id::regenerating);

    regen->set_duration(rnd::range(4, 6));

    map::g_player->m_properties.apply(regen);
}

static void give_player_sp_for_resist_with_absorption_trait()
{
    int sp = rnd::range(1, 6);

    // Talisman of Arcane Void doubles the granted spirit points.
    if (map::g_player->m_inv.has_item_in_backpack(item::Id::talisman_of_arcane_void)) {
        sp *= 2;
    }

    actor::restore_sp(*map::g_player, sp, actor::AllowRestoreAboveMax::no, Verbose::yes);
}

static bool is_player_caster_or_target(
    const actor::Actor* const caster,
    const std::vector<actor::Actor*>& targets)
{
    if (actor::is_player(caster)) {
        return true;
    }

    if (std::any_of(
            std::begin(targets),
            std::end(targets),
            [](const actor::Actor* actor) {
                return actor::is_player(actor);
            })) {
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------------
// spells
// -----------------------------------------------------------------------------
namespace spells
{
Spell* make(const SpellId spell_id)
{
    switch (spell_id) {
    case SpellId::aura_of_decay:
        return new SpellAuraOfDecay();

    case SpellId::enfeeble:
        return new SpellEnfeeble();

    case SpellId::curse:
        return new SpellCurse();

    case SpellId::poison:
        return new SpellPoison();

    case SpellId::slow:
        return new SpellSlow();

    case SpellId::terrify:
        return new SpellTerrify();

    case SpellId::threat_projection:
        return new SpellThreatProjection();

    case SpellId::force_bolt:
        return new SpellBolt(new ForceBolt);

    case SpellId::darkbolt:
        return new SpellBolt(new Darkbolt);

    case SpellId::gnawing_torrent:
        return new SpellBolt(new GnawingTorrent);

    case SpellId::aza_gaze:
        return new SpellAzaGaze();

    case SpellId::summon_random:
        return new SpellSummon(new SummonRandom);

    case SpellId::summon_water_creature:
        return new SpellSummon(new SummonWaterCreature);

    case SpellId::summon_tentacles:
        return new SpellSummon(new SummonTentacles);

    case SpellId::heal:
        return new SpellHeal();

    case SpellId::knockback:
        return new SpellKnockBack();

    case SpellId::teleport:
        return new SpellTeleport();

    case SpellId::temporal_echo:
        return new SpellTemporalEcho();

    case SpellId::cataclysm:
        return new SpellCataclysm();

    case SpellId::pestilence:
        return new SpellPestilence();

    case SpellId::mirror_images:
        return new SpellMirrorImages();

    case SpellId::projected_strike:
        return new SpellProjectedStrike();

    case SpellId::control_object:
        return new SpellControlObject();

    case SpellId::cleansing_fire:
        return new SpellCleansingFire();

    case SpellId::brilliance:
        return new SpellBrilliance();

    case SpellId::sanctuary:
        return new SpellSanctuary();

    case SpellId::purge:
        return new SpellPurge();

    case SpellId::frenzy:
        return new SpellFrenzy();

    case SpellId::inscribe_boundary_sigil:
        return new SpellInscribeBoundarySigil();

    case SpellId::bless:
        return new SpellBless();

    case SpellId::cancellation:
        return new SpellCancellation();

    case SpellId::mi_go_hypno:
        return new SpellMiGoHypno();

    case SpellId::burn:
        return new SpellBurn();

    case SpellId::blind:
        return new SpellBlind();

    case SpellId::deafen:
        return new SpellDeafen();

    case SpellId::light:
        return new SpellLight();

    case SpellId::transmut:
        return new SpellTransmut();

    case SpellId::clairvoyance:
        return new SpellClairvoyance();

    case SpellId::invis:
        return new SpellInvis();

    case SpellId::see_invis:
        return new SpellSeeInvis();

    case SpellId::spell_shield:
        return new SpellSpellShield();

    case SpellId::haste:
        return new SpellHaste();

    case SpellId::premonition:
        return new SpellPremonition();

    case SpellId::erudition:
        return new SpellErudition();

    case SpellId::expulsion:
        return new SpellExpulsion();

    case SpellId::identify:
        return new SpellIdentify();

    case SpellId::blood_tempering:
        return new SpellBloodTempering();

    case SpellId::sacrifice_life:
        return new SpellSacrificeLife();

    case SpellId::shed_impurity:
        return new SpellShedImpurity();

    case SpellId::thorns:
        return new SpellThorns();

    case SpellId::crimson_passage:
        return new SpellCrimsonPassage();

    case SpellId::heal_others:
        return new SpellHealOthers();

    case SpellId::END:
        break;
    }

    ASSERT(false);

    return nullptr;
}

SpellId str_to_spell_id(const std::string& str)
{
    return s_str_to_spell_id_map.at(str);
}

SpellSkill str_to_spell_skill_id(const std::string& str)
{
    return s_str_to_spell_skill_map.at(str);
}

std::string spell_domain_title(const SpellDomain domain)
{
    switch (domain) {
    case SpellDomain::channeling:
        return "Channeling";

    case SpellDomain::corruption:
        return "Corruption";

    case SpellDomain::illusion:
        return "Illusion";

    case SpellDomain::mind:
        return "Mind";

    case SpellDomain::time:
        return "Time";

    case SpellDomain::warding:
        return "Warding";

    case SpellDomain::blood:
        return "Blood";

    case SpellDomain::END:
        break;
    }

    ASSERT(false);

    return "";
}

std::string skill_to_str(const SpellSkill skill)
{
    switch (skill) {
    case SpellSkill::basic:        return "basic";
    case SpellSkill::expert:       return "expert";
    case SpellSkill::master:       return "master";
    case SpellSkill::transcendent: return "transcendent";
    }

    ASSERT(false);

    return "";
}

ShockSrc spell_domain_to_shock_type(const SpellDomain domain)
{
    return s_spell_domain_to_shock_type_map.at(domain);
}

terrain::DidOpen run_opening_spell_effect_at(
    const P& pos,
    const SpellSkill skill)
{
    (void)skill;

    terrain::Terrain* const terrain = map::g_terrain.at(pos);

    if (terrain->id() == terrain::Id::door) {
        auto* const door = static_cast<terrain::Door*>(terrain);

        if (door->is_open()) {
            return terrain::DidOpen::no;
        }
    }

    const auto did_open = terrain->open(nullptr);

    return did_open;
}

terrain::DidClose run_close_spell_effect_at(
    const P& pos,
    const SpellSkill skill)
{
    (void)skill;

    terrain::Terrain* const terrain = map::g_terrain.at(pos);

    if (terrain->id() == terrain::Id::door) {
        if (!static_cast<terrain::Door*>(terrain)->is_open()) {
            return terrain::DidClose::no;
        }
    }

    // TODO: Shouldn't the actor parameter be the caster here?
    const auto did_close = terrain->close(nullptr);

    return did_close;
}

void run_mi_go_hypno_effect(actor::Actor& target)
{
    prop::Prop* prop_fainted = prop::make(prop::Id::fainted);

    prop_fainted->set_duration(rnd::range(2, 10));

    target.m_properties.apply(prop_fainted);
}

bool allow_properties_prevent_player_intr_spell(const SpellId id)
{
    switch (id) {
    case SpellId::crimson_passage: {
        // Casting crimson passage while it is already active just disables it, this should always
        // be allowed even while confused etc.
        return !map::g_player->m_properties.has(prop::Id::crimson_passage);
    }

    default: {
        return true;
    }
    }
}

}  // namespace spells

// -----------------------------------------------------------------------------
// Spell
// -----------------------------------------------------------------------------
Range Spell::cost_range(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    const int cost_max = base_max_cost(skill, caster);
    const int cost_min = (cost_max + 1) / 2;

    Range range(cost_min, cost_max);

    if (actor::is_player(caster) &&
        caster->m_properties.has(prop::Id::meditative_focused)) {
        if (player_bon::has_trait(TraitId::sage)) {
            range.min = 0;
            range.max = 0;
        }
        else {
            --range.min;
            --range.max;
        }
    }

    range.min = std::max(0, range.min);
    range.max = std::max(0, range.max);

    return range;
}

void Spell::cast(
    actor::Actor* const caster,
    const SpellSkill skill,
    const SpellSrc spell_src,
    const std::vector<actor::Actor*>& seen_targets) const
{
    TRACE_FUNC_BEGIN;

    ASSERT(caster);

    prop::PropHandler& properties = caster->m_properties;

    const bool allow_properties_prevent_intr_spell =
        !actor::is_player(caster) ||
        spells::allow_properties_prevent_player_intr_spell(id());

    // If this is an intrinsic cast, check properties which NEVER allows casting or speaking.
    //
    // NOTE: If this is a non-intrinsic cast (e.g. from a scroll), then we assume that the caller
    // has made all checks themselves.
    //
    if (spell_src == SpellSrc::learned) {
        if (allow_properties_prevent_intr_spell) {
            const bool allow_cast = properties.allow_cast_intr_spell_absolute(Verbose::yes);

            if (!allow_cast) {
                return;
            }

            if (!properties.allow_speak(Verbose::yes)) {
                // TODO: Not all spells "require making noise", it seems insconsistent to outright
                // prevent casting when the caster cannot speak.
                return;
            }
        }
    }

    // OK, we can try to cast

    PlayerAwareOfCast player_aware = PlayerAwareOfCast::no;

    if (actor::is_player(caster)) {
        TRACE << "Player casting spell" << "\n";

        player_aware = PlayerAwareOfCast::yes;

        const ShockSrc shock_src =
            (spell_src == SpellSrc::learned)
            ? s_spell_domain_to_shock_type_map.at(domain())
            : ShockSrc::use_strange_item;

        int shock = shock_value();

        if (map::g_player->m_inv.has_item_in_backpack(item::Id::necronomicon)) {
            shock *= 2;
        }

        if (shock > 0) {
            map::g_player->incr_shock((double)shock, shock_src);
        }

        // Make sound if noisy - casting from scrolls is always noisy.
        if (is_noisy(skill) || (spell_src == SpellSrc::manuscript)) {
            Snd snd(
                "",
                audio::SfxId::END,
                IgnoreMsgIfOriginSeen::yes,
                caster->m_pos,
                caster,
                SndVol::low,
                AlertsMon::yes);

            snd.run();
        }
    }
    else {
        // Caster is monster
        TRACE << "Monster casting spell" << "\n";

        // If sound is noisy, print a sound message. Also if no sound was heard by the player and
        // the monster is seen, print a "visual" message.

        bool did_player_hear_sound = false;

        if (is_noisy(skill)) {
            Snd snd(
                generate_mon_cast_sound_msg(*caster),
                audio::SfxId::END,
                IgnoreMsgIfOriginSeen::no,
                caster->m_pos,
                caster,
                SndVol::low,
                AlertsMon::no);

            snd.run();

            did_player_hear_sound = snd.did_player_hear_sound();

            if (did_player_hear_sound) {
                player_aware = PlayerAwareOfCast::yes;
            }
        }

        if (actor::can_player_see_actor(*caster)) {
            player_aware = PlayerAwareOfCast::yes;

            if (!did_player_hear_sound) {
                const std::string visual_msg = generate_mon_cast_visual_msg(*caster);

                if (!visual_msg.empty()) {
                    msg_log::add(generate_mon_cast_visual_msg(*caster));
                }
            }
        }
    }

    bool allow_cast = true;

    const int hp_before = caster->m_hp;

    if (spell_src == SpellSrc::learned) {
        const Range range = cost_range(skill, caster);

        if (range.min > 0) {
            TRACE
                << "Applying spell cost for spell "
                << "'" << name() << "', "
                << "caster "
                << "'" << actor::name_a(*caster) << "'"
                << "\n";

            apply_spell_cost(*caster, range.roll(), cost_type());
        }

        // Check properties which MAY allow casting.
        allow_cast = true;

        if (allow_properties_prevent_intr_spell) {
            allow_cast = properties.allow_cast_intr_spell_chance(Verbose::yes);
        }
    }

    const bool is_focused_player =
        actor::is_player(caster) &&
        caster->m_properties.has(prop::Id::meditative_focused);

    if (allow_cast && actor::is_alive(*caster)) {
        TRACE
            << "Running spell effect for spell "
            << "'" << name() << "', "
            << "caster "
            << "'" << actor::name_a(*caster) << "'"
            << "\n";

        // Here we run the actual casting of the spell itself:
        run_effect(caster, skill, seen_targets, player_aware);

        end_properties_for_casting_spell(*caster, id());

        if (should_give_regen_from_flagellant_trait(*caster, hp_before, domain())) {
            apply_regen_from_flagellant_trait();
        }

        // Disable tenebrous spell for the player?
        const bool should_forget_spell =
            actor::is_player(caster) &&
            is_tenebrous() &&
            (spell_src == SpellSrc::learned);

        if (should_forget_spell) {
            player_spells::forget_spell(id());
        }
    }

    const bool allow_side_effect =
        actor::is_player(caster) &&
        actor::is_alive(*caster) &&
        !player_bon::is_bg(Bg::exorcist) &&
        allow_cast &&
        (base_max_cost(skill, caster) > 0);

    if (allow_side_effect && rnd::one_in(7)) {
        spell_side_effects::run_random_side_effect(*caster);
    }

    const bool is_casting_from_item = (spell_src == SpellSrc::item);

    if (!is_casting_from_item && !is_focused_player) {
        game_time::tick();
    }

    TRACE_FUNC_END;
}

void Spell::on_resist(actor::Actor& target, const actor::Actor* const caster) const
{
    const bool is_player_target = actor::is_player(&target);

    const bool player_see_target = actor::can_player_see_actor(target);

    if (player_see_target) {
        std::string resist_msg;

        if (is_player_target) {
            resist_msg = "I resist " + name(SpellNameType::a) + " spell!";
        }
        else {
            const std::string mon_name = text_format::first_to_upper(actor::name_the(target));
            const std::string article = actor::is_player(caster) ? "the" : "a";

            resist_msg = mon_name + "resists " + article + " spell!";
        }

        msg_log::add(resist_msg);

        if (is_player_target) {
            audio::play(audio::SfxId::spell_shield_break);
        }

        draw_blast_at_cells({target.m_pos}, colors::white());
    }

    // End spell resistance if not a natural property.
    if (!target.m_data->natural_props[(size_t)prop::Id::r_spell]) {
        const bool is_ended = target.m_properties.end_prop(prop::Id::r_spell);

        const bool has_absorption = player_bon::has_trait(TraitId::absorption);

        if (is_ended && is_player_target && has_absorption) {
            give_player_sp_for_resist_with_absorption_trait();
        }
    }
}

std::vector<std::string> Spell::descr(
    const SpellSkill skill,
    const SpellSrc spell_src) const
{
    std::vector<std::string> lines = descr_specific(skill);

    if (spell_src != SpellSrc::manuscript) {
        lines.push_back(get_noise_descr(is_noisy(skill)));
    }

    if (spell_src == SpellSrc::learned) {
        const std::string forgotten_hint_str =
            "Forgotten spells can be recalled by "
            "studying inscribed objects "
            "or by casting them from a manuscript.";

        if (player_spells::is_spell_forgotten(id())) {
            lines.emplace_back(
                "Forgotten - this spell can no longer be "
                "cast from memory. " +
                forgotten_hint_str);
        }
        else if (is_tenebrous()) {
            lines.emplace_back(
                "Tenebrous - this spell will be instantly "
                "forgotten if cast from memory. " +
                forgotten_hint_str);
        }
    }

    std::string str;

    if (can_be_improved_with_skill()) {
        str = get_skill_descr(skill, spell_src);
    }

    if (!player_bon::is_bg(Bg::exorcist)) {
        text_format::append_with_space(str, domain_descr());
    }

    if (!str.empty()) {
        lines.push_back(str);
    }

    return lines;
}

std::string Spell::domain_descr() const
{
    const SpellDomain my_domain = domain();

    if (my_domain == SpellDomain::END) {
        return "";
    }

    const std::string domain_title =
        text_format::first_to_upper(
            spells::spell_domain_title(domain()));

    return "It belongs to the \"" + domain_title + "\" domain.";
}

int Spell::shock_value() const
{
    const SpellShock type = shock_type();

    int value = 0;

    switch (type) {
    case SpellShock::none:
        value = 0;
        break;

    case SpellShock::mild:
        value = 4;
        break;

    case SpellShock::disturbing:
        value = 16;
        break;

    case SpellShock::severe:
        value = 24;
        break;
    }

    return value;
}

// -----------------------------------------------------------------------------
// Aura of Decay
// -----------------------------------------------------------------------------
std::string SpellAuraOfDecay::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "an " : "";

    return a + "Aura of Decay";
}

SpellId SpellAuraOfDecay::id() const
{
    return SpellId::aura_of_decay;
}

SpellDomain SpellAuraOfDecay::domain() const
{
    return SpellDomain::corruption;
}

SpellShock SpellAuraOfDecay::shock_type() const
{
    return SpellShock::disturbing;
}

bool SpellAuraOfDecay::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

Range SpellAuraOfDecay::dmg_range(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {1, 1};  // Avg 1.0
    case SpellSkill::expert:       return {1, 2};  // Avg 1.5
    case SpellSkill::master:
    case SpellSkill::transcendent: return {1, 3};  // Avg 2.0
    }

    ASSERT(false);

    return {1, 1};
}

Range SpellAuraOfDecay::duration_range(const SpellSkill skill) const
{
    const int k = std::min(3, (int)skill + 1);

    Range duration_range;
    duration_range.min = 15 * k;
    duration_range.max = duration_range.min * 2;

    return duration_range;
}

int SpellAuraOfDecay::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 6;
}

void SpellAuraOfDecay::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    auto* prop = static_cast<prop::AuraOfDecay*>(prop::make(prop::Id::aura_of_decay));

    prop->set_duration(duration_range(skill).roll());

    prop->set_dmg_range(dmg_range(skill));

    if (skill == SpellSkill::transcendent) {
        prop->set_allow_instant_kill();
    }

    caster->m_properties.apply(prop);
}

std::vector<std::string> SpellAuraOfDecay::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "The caster exudes death and decay. Creatures within a "
        "distance of two steps take damage each standard turn.");

    descr.push_back(
        "The spell deals " +
        dmg_range(skill).str() +
        " damage to each creature.");

    if (skill == SpellSkill::transcendent) {
        descr.emplace_back(
            "Any time a creature takes damage from the spell, "
            "they may be destroyed immediately (2% chance).");
    }

    descr.push_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

int SpellAuraOfDecay::mon_cooldown() const
{
    return 30;
}

bool SpellAuraOfDecay::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)skill;

    return (
        !seen_targets.empty() &&
        !mon.m_properties.has(prop::Id::aura_of_decay));
}

// -----------------------------------------------------------------------------
// Bolt spells
// -----------------------------------------------------------------------------
int SpellBolt::mon_cooldown() const
{
    return m_impl->mon_cooldown();
}

std::string SpellBolt::name(const SpellNameType name_type) const
{
    return m_impl->name(name_type);
}

SpellId SpellBolt::id() const
{
    return m_impl->id();
}

SpellDomain SpellBolt::domain() const
{
    return SpellDomain::channeling;
}

SpellShock SpellBolt::shock_type() const
{
    return m_impl->shock_type();
}

std::vector<std::string> SpellBolt::descr_specific(const SpellSkill skill) const
{
    return m_impl->descr_specific(skill);
}

int SpellBolt::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)caster;

    return m_impl->base_max_cost(skill, caster);
}

bool SpellBolt::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

void SpellBolt::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    const int nr_projectiles = m_impl->nr_projectiles(skill);

    for (int i = 0; i < nr_projectiles; ++i) {
        map::update_vision();

        const std::vector<actor::Actor*>& current_seen_targets = actor::seen_foes(*caster);

        if (current_seen_targets.empty()) {
            if (actor::is_player(caster)) {
                msg_log::add("A dark sphere materializes, but quickly fizzles out.");
            }

            break;
        }

        actor::Actor* const target =
            map::random_closest_actor(
                caster->m_pos,
                current_seen_targets);

        run_bolt_on_target(*caster, *target, skill, player_aware);

        if (!actor::is_alive(*map::g_player)) {
            break;
        }
    }
}

void SpellBolt::run_bolt_on_target(
    actor::Actor& caster,
    actor::Actor& target,
    SpellSkill skill,
    PlayerAwareOfCast player_aware) const
{
    if (target.m_properties.has(prop::Id::r_spell)) {
        on_resist(target, &caster);
        return;
    }

    Snd release_snd(
        "I hear something rush through the air.",
        audio::SfxId::darkbolt_release,
        IgnoreMsgIfOriginSeen::yes,
        caster.m_pos,
        &caster,
        SndVol::low,
        AlertsMon::yes);

    release_snd.run();

    if (release_snd.did_player_hear_sound()) {
        player_aware = PlayerAwareOfCast::yes;
    }

    if (player_aware == PlayerAwareOfCast::yes) {
        draw_projectile_travel(caster, target, skill);
    }

    Snd impact_snd(
        "I hear an impact.",
        m_impl->impact_sfx(),
        IgnoreMsgIfOriginSeen::yes,
        target.m_pos,
        nullptr,
        SndVol::low,
        AlertsMon::yes);

    impact_snd.run();

    const P& target_p = target.m_pos;
    const bool player_see_pos = map::g_seen.at(target_p);
    const bool player_see_tgt = actor::can_player_see_actor(target);

    if (player_see_tgt || player_see_pos) {
        const int delay_div = (m_impl->nr_projectiles(skill) > 1) ? 4 : 2;

        draw_blast_at_cells({target.m_pos}, colors::magenta(), delay_div);

        Color msg_clr = colors::msg_good();

        std::string str_begin = "I am";

        if (actor::is_player(&target)) {
            msg_clr = colors::msg_bad();
        }
        else {
            // Target is monster
            const std::string name_the =
                player_see_tgt
                ? text_format::first_to_upper(actor::name_the(target))
                : "It";

            str_begin = name_the + " is";

            if (map::g_player->is_leader_of(&target)) {
                msg_clr = colors::white();
            }
        }

        const std::string hit_msg = str_begin + " " + m_impl->hit_msg_ending();

        msg_log::add(hit_msg, msg_clr);
    }

    const Range dmg_range = m_impl->damage(skill);

    actor::hit(target, dmg_range.roll(), s_bolt_dmg_type, &caster, AllowWound::no);

    m_impl->on_hit(target, caster, skill);

    if (!actor::is_player(&target)) {
        target.become_aware_player(actor::AwareSource::spell_victim);
    }
}

void SpellBolt::draw_projectile_travel(
    const actor::Actor& caster,
    const actor::Actor& target,
    SpellSkill skill) const
{
    (void)skill;

    Array2<bool> blocked(map::dims());

    map_parsers::BlocksProjectiles()
        .run(blocked, blocked.rect());

    const auto flood = floodfill(caster.m_pos, blocked);

    const auto path = pathfind_with_flood(caster.m_pos, target.m_pos, flood);

    if (!path.empty()) {
        states::draw();

        const int idx_0 = (int)(path.size()) - 1;

        for (int i = idx_0; i > 0; --i) {
            const auto& p = path[i];

            if (!map::g_seen.at(p)) {
                continue;
            }

            states::draw();

            io::MapDrawObj draw_obj;
            draw_obj.tile = gfx::TileId::blast1;
            draw_obj.character = '*';
            draw_obj.pos = viewport::to_view_pos(p);
            draw_obj.color = colors::magenta();

            draw_obj.draw();

            io::update_screen();

            int delay = config::base_delay();

            if (m_impl->nr_projectiles(skill) > 1) {
                delay /= 2;
            }

            io::sleep(delay);
        }
    }
}

bool SpellBolt::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

SpellShock BoltImpl::shock_type() const
{
    return SpellShock::mild;
}

audio::SfxId BoltImpl::impact_sfx() const
{
    return audio::SfxId::darkbolt_impact;
}

void ForceBolt::on_hit(
    actor::Actor& actor_hit,
    actor::Actor& caster,
    const SpellSkill skill) const
{
    (void)actor_hit;
    (void)caster;
    (void)skill;
}

std::string ForceBolt::hit_msg_ending() const
{
    return "struck by a bolt!";
}

int ForceBolt::mon_cooldown() const
{
    return 3;
}

std::string ForceBolt::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Force Bolt";
}

SpellId ForceBolt::id() const
{
    return SpellId::force_bolt;
}

int ForceBolt::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 2;
}

Range ForceBolt::damage(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {3, 4};  // Avg 3.5
    case SpellSkill::expert:       return {5, 7};  // Avg 6.0
    case SpellSkill::master:
    case SpellSkill::transcendent: return {9, 12};  // Avg 10.5
    }

    ASSERT(false);

    return {1, 1};
}

std::vector<std::string> ForceBolt::descr_specific(const SpellSkill skill) const
{
    (void)skill;

    return {};
}

std::string Darkbolt::hit_msg_ending() const
{
    return "struck by a blast!";
}

int Darkbolt::mon_cooldown() const
{
    return 5;
}

std::string Darkbolt::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Darkbolt";
}

SpellId Darkbolt::id() const
{
    return SpellId::darkbolt;
}

int Darkbolt::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 4;
}

Range Darkbolt::damage(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {4, 9};   // Avg 6.5
    case SpellSkill::expert:       return {5, 11};  // Avg 8.0
    case SpellSkill::master:
    case SpellSkill::transcendent: return {6, 13};  // Avg 9.5
    }

    ASSERT(false);

    return {1, 1};
}

std::vector<std::string> Darkbolt::descr_specific(const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "A bolt of siphoned energy is hurled towards a target "
        "with great force. "
        "The conjured bolt has some will on its own - "
        "once released, it seeks creatures that pose a threat, "
        "precise control is therefore not possible.");

    const Range dmg_range = damage(skill);

    std::string effect_str = "The impact deals " + dmg_range.str() + " damage.";

    if (skill >= SpellSkill::master) {
        effect_str += " The target is paralyzed and set aflame.";

        if (skill == SpellSkill::transcendent) {
            effect_str +=
                " If the target is sufficiently far away from "
                "the caster, the bolt explodes on impact.";
        }
    }
    else {
        // <= Expert
        effect_str += " The target is paralyzed.";
    }

    descr.push_back(effect_str);

    return descr;
}

void Darkbolt::on_hit(
    actor::Actor& actor_hit,
    actor::Actor& caster,
    const SpellSkill skill) const
{
    if (skill == SpellSkill::transcendent) {
        const int dist = king_dist(caster.m_pos, actor_hit.m_pos);

        if (dist > g_expl_std_radi) {
            explosion::run(actor_hit.m_pos, ExplType::expl);
        }
    }

    if (!actor::is_alive(actor_hit)) {
        return;
    }

    if (!actor_hit.m_properties.is_resisting_dmg(s_bolt_dmg_type, Verbose::no)) {
        prop::Prop* paralyzed = prop::make(prop::Id::paralyzed);

        paralyzed->set_duration(rnd::range(1, 2));

        actor_hit.m_properties.apply(paralyzed);

        if (skill >= SpellSkill::master) {
            prop::Prop* burning = prop::make(prop::Id::burning);

            burning->set_duration(rnd::range(2, 3));

            actor_hit.m_properties.apply(burning);
        }
    }
}

int GnawingTorrent::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 4;
}

Range GnawingTorrent::damage(const SpellSkill skill) const
{
    if (skill == SpellSkill::transcendent) {
        return {1, 2};
    }
    else {
        return {1, 1};
    }
}

int GnawingTorrent::nr_projectiles(const SpellSkill skill) const
{
    // Damage with 5/7/9/11 bolts dealing 1 damage per bolt (1-2 for transcendent skill):
    // Basic:        5
    // Expert:       7
    // Master:       9
    // Transcendent: Average 16.5 (11-22)

    return 5 + ((int)skill * 2);
}

int GnawingTorrent::mon_cooldown() const
{
    return 3;
}

void GnawingTorrent::on_hit(
    actor::Actor& actor_hit,
    actor::Actor& caster,
    const SpellSkill skill) const
{
    (void)actor_hit;
    (void)skill;

    if (actor::is_edible_living_creature(actor_hit)) {
        actor::restore_hp(caster, 1, actor::AllowRestoreAboveMax::yes, Verbose::no);
    }
}

std::string GnawingTorrent::hit_msg_ending() const
{
    return "fed upon!";
}

audio::SfxId GnawingTorrent::impact_sfx() const
{
    return audio::SfxId::gnawing_torrent_impact;
}

std::string GnawingTorrent::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Gnawing Torrent";
}

SpellId GnawingTorrent::id() const
{
    return SpellId::gnawing_torrent;
}

std::vector<std::string> GnawingTorrent::descr_specific(const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back("Unleashes a stream of devouring energy upon the caster's victims.");

    descr.emplace_back(
        std::to_string(nr_projectiles(skill)) +
        " projectiles are conjured, each dealing " +
        damage(skill).str() +
        " damage.");

    descr.emplace_back(
        "Each impact feeds life force back to the caster, providing 1 hit point "
        "(only against creatures of flesh and blood; "
        "ethereal creatures cannot be fed upon for example).");

    descr.emplace_back("Hit points can be raised above the normal maximum level.");

    return descr;
}

// -----------------------------------------------------------------------------
// Azathoths wrath
// -----------------------------------------------------------------------------
int SpellAzaGaze::mon_cooldown() const
{
    return 6;
}

std::string SpellAzaGaze::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "an " : "";

    return a + "Azathoth's Gaze";
}

SpellId SpellAzaGaze::id() const
{
    return SpellId::aza_gaze;
}

SpellDomain SpellAzaGaze::domain() const
{
    return SpellDomain::channeling;
}

SpellShock SpellAzaGaze::shock_type() const
{
    return SpellShock::disturbing;
}

int SpellAzaGaze::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 8;
}

bool SpellAzaGaze::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

Range SpellAzaGaze::dmg_range(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {2, 5};  // Avg 3.5
    case SpellSkill::expert:       return {4, 8};  // Avg 6.0
    case SpellSkill::master:
    case SpellSkill::transcendent: return {6, 11};  // Avg 8.5
    }

    ASSERT(false);

    return {1, 1};
}

Range SpellAzaGaze::faint_duration_range(SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {2, 6};
    case SpellSkill::expert:       return {3, 7};
    case SpellSkill::master:
    case SpellSkill::transcendent: return {4, 8};
    }

    ASSERT(false);

    return {1, 1};
}

Range SpellAzaGaze::conflict_duration_range(SpellSkill skill) const
{
    (void)skill;

    return {10, 12};
}

void SpellAzaGaze::do_damage_on_target(
    actor::Actor& target,
    SpellSkill const skill,
    actor::Actor* const caster) const
{
    const int dmg = dmg_range(skill).roll();

    actor::hit(target, dmg, DmgType::explosion, caster, AllowWound::no);
}

void SpellAzaGaze::apply_properties_on_target(
    actor::Actor& target,
    SpellSkill skill) const
{
    if (!actor::is_alive(target)) {
        return;
    }

    {
        prop::Prop* prop = prop::make(prop::Id::fainted);

        const int duration = faint_duration_range(skill).roll();

        prop->set_duration(duration);

        target.m_properties.apply(prop);
    }

    if (skill >= SpellSkill::transcendent) {
        prop::Prop* prop = prop::make(prop::Id::conflict);

        const int duration = conflict_duration_range(skill).roll();

        prop->set_duration(duration);

        target.m_properties.apply(prop);
    }
}

void SpellAzaGaze::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    Snd snd(
        "An insane cacophony resounds through the air!",
        audio::SfxId::aza_gaze,
        IgnoreMsgIfOriginSeen::no,
        caster->m_pos,
        caster,
        SndVol::high,
        AlertsMon::no);

    snd.run();

    if (!snd.did_player_hear_sound() && is_player_caster_or_target(caster, seen_targets)) {
        // Player did not hear the sound (deaf), but is caster or target - print an alternative
        // message instead.
        msg_log::add("A storm of mad tremors fills the air!");
    }

    for (actor::Actor* const target : seen_targets) {
        run_effect_on_target(caster, *target, skill);
    }
}

void SpellAzaGaze::run_effect_on_target(
    actor::Actor* const caster,
    actor::Actor& target,
    const SpellSkill skill) const
{
    if (target.m_properties.has(prop::Id::r_spell)) {
        on_resist(target, caster);
        return;
    }

    if (actor::can_player_see_actor(target)) {
        Color msg_clr = colors::msg_good();

        std::string hit_msg;

        if (actor::is_player(&target)) {
            hit_msg = "I am";

            msg_clr = colors::msg_bad();
        }
        else {
            hit_msg = text_format::first_to_upper(actor::name_the(target)) + " is";

            if (map::g_player->is_leader_of(&target)) {
                msg_clr = colors::white();
            }
        }

        hit_msg += " wracked by chaos.";

        msg_log::add(hit_msg, msg_clr);

        draw_blast_at_cells({target.m_pos}, colors::light_red());
    }

    do_damage_on_target(target, skill, caster);

    if (!actor::is_player(&target)) {
        target.become_aware_player(actor::AwareSource::spell_victim);
    }

    apply_properties_on_target(target, skill);

    Snd snd(
        "",
        audio::SfxId::END,
        IgnoreMsgIfOriginSeen::yes,
        target.m_pos,
        nullptr,
        SndVol::high,
        AlertsMon::yes);

    snd.run();
}

std::vector<std::string> SpellAzaGaze::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Channels the chaos of Azathoth unto all visible enemies. "
        "The channel can only be opened for a fraction of a second, "
        "but even this is enough to cause great physical and mental "
        "devastation.");

    descr.push_back(
        "The spell deals " +
        dmg_range(skill).str() +
        " damage to each creature.");

    descr.push_back(
        "Causes the victims to faint for " +
        faint_duration_range(skill).str() +
        " turns, if they are susceptible.");

    if (skill == SpellSkill::transcendent) {
        descr.push_back(
            "The victims become conflicted for " +
            conflict_duration_range(skill).str() +
            " turns, causing them to view any creature as " +
            "their enemy.");
    }

    return descr;
}

bool SpellAzaGaze::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

// -----------------------------------------------------------------------------
// Cataclysm
// -----------------------------------------------------------------------------
std::string SpellCataclysm::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Cataclysm";
}

SpellId SpellCataclysm::id() const
{
    return SpellId::cataclysm;
}

SpellDomain SpellCataclysm::domain() const
{
    return SpellDomain::channeling;
}

SpellShock SpellCataclysm::shock_type() const
{
    return SpellShock::disturbing;
}

bool SpellCataclysm::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

int SpellCataclysm::destruction_radi(const SpellSkill skill) const
{
    return g_fov_radi_int + 1 + ((int)skill * 2);
}

int SpellCataclysm::nr_destruction_sweeps(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return 2;
    case SpellSkill::expert:
    case SpellSkill::master:
    case SpellSkill::transcendent: return 3;
    }

    ASSERT(false);

    return 1;
}

int SpellCataclysm::nr_explosions(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return 6;
    case SpellSkill::expert:       return 9;
    case SpellSkill::master:       return 12;
    case SpellSkill::transcendent: return 20;
    }

    ASSERT(false);

    return 1;
}

int SpellCataclysm::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 7;
}

void SpellCataclysm::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    const bool is_player = actor::is_player(caster);

    if (actor::can_player_see_actor(*caster)) {
        std::string caster_name = is_player ? "me" : actor::name_the(*caster);

        msg_log::add("Destruction rages around " + caster_name + "!");
    }

    const auto& caster_pos = caster->m_pos;

    const int destr_radi = destruction_radi(skill);

    const R area(
        std::max(1, caster_pos.x - destr_radi),
        std::max(1, caster_pos.y - destr_radi),
        std::min(map::w() - 1, caster_pos.x + destr_radi) - 1,
        std::min(map::h() - 1, caster_pos.y + destr_radi) - 1);

    const auto positions = area.positions();

    // Run explosions
    std::vector<P> p_bucket;

    const int expl_radi_diff = -1;

    for (const auto& p : positions) {
        const auto* const terrain = map::g_terrain.at(p);

        if (!terrain->is_walkable()) {
            continue;
        }

        const int dist = king_dist(caster_pos, p);

        const int min_dist = g_expl_std_radi + 1 + expl_radi_diff;

        if (dist >= min_dist) {
            p_bucket.push_back(p);
        }
    }

    const int nr_expl = nr_explosions(skill);

    for (int i = 0; i < nr_expl; ++i) {
        if (p_bucket.empty()) {
            return;
        }

        const auto idx = rnd::range(0, (int)p_bucket.size() - 1);

        const auto& p = rnd::element(p_bucket);

        explosion::run(p, ExplType::expl, EmitExplSnd::yes, expl_radi_diff);

        p_bucket.erase(std::begin(p_bucket) + idx);
    }

    // Explode braziers
    for (const auto& p : positions) {
        const auto terrain_id = map::g_terrain.at(p)->id();

        if (terrain_id == terrain::Id::brazier) {
            Snd snd(
                "I hear an explosion!",
                audio::SfxId::explosion_molotov,
                IgnoreMsgIfOriginSeen::yes,
                p,
                nullptr,
                SndVol::high,
                AlertsMon::yes);

            snd.run();

            map::update_terrain(terrain::make(terrain::Id::rubble_low, p));

            prop::Prop* const burning = prop::make(prop::Id::burning);

            explosion::run(
                p,
                ExplType::apply_prop,
                EmitExplSnd::yes,
                0,
                ExplExclCenter::yes,
                {burning});
        }
    }

    // Destroy the surrounding environment
    const int nr_sweeps = nr_destruction_sweeps(skill);

    for (int i = 0; i < nr_sweeps; ++i) {
        for (const auto& p : positions) {
            if (!rnd::one_in(8)) {
                continue;
            }

            bool is_adj_to_walkable_cell = false;

            for (const P& d : dir_utils::g_dir_list) {
                const auto p_adj = p + d;

                if (map::g_terrain.at(p_adj)->is_walkable()) {
                    is_adj_to_walkable_cell = true;
                }
            }

            if (is_adj_to_walkable_cell) {
                map::g_terrain.at(p)->hit(DmgType::explosion, nullptr);
            }
        }
    }

    // Put blood, and set stuff on fire
    for (const auto& p : positions) {
        auto* const terrain = map::g_terrain.at(p);

        if (rnd::one_in(10)) {
            terrain->try_make_bloody();

            if (rnd::one_in(3)) {
                terrain->try_put_gore();
            }
        }

        if ((p != caster->m_pos) && rnd::one_in(6)) {
            terrain->hit(DmgType::fire, nullptr);
        }
    }

    Snd snd(
        "",
        audio::SfxId::END,
        IgnoreMsgIfOriginSeen::yes,
        caster_pos,
        nullptr,
        SndVol::high,
        AlertsMon::yes);

    snd.run();
}

std::vector<std::string> SpellCataclysm::descr_specific(
    const SpellSkill skill) const
{
    (void)skill;

    std::vector<std::string> descr;

    descr.emplace_back("Blasts the surrounding area with terrible force.");

    descr.emplace_back("Higher skill levels increases the magnitude of the destruction.");

    return descr;
}

bool SpellCataclysm::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)skill;

    // Always allow casting with a visible target.
    if (!seen_targets.empty()) {
        return true;
    }

    // Sometimes allow casting if monster has an unseen target.
    if (mon.m_ai_state.target && rnd::one_in(20)) {
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------------
// Pestilence
// -----------------------------------------------------------------------------
int SpellPestilence::mon_cooldown() const
{
    return 21;
}

std::string SpellPestilence::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Pestilence";
}

SpellId SpellPestilence::id() const
{
    return SpellId::pestilence;
}

SpellDomain SpellPestilence::domain() const
{
    return SpellDomain::corruption;
}

SpellShock SpellPestilence::shock_type() const
{
    return SpellShock::disturbing;
}

bool SpellPestilence::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

int SpellPestilence::nr_rats_summoned(SpellSkill skill) const
{
    if (skill == SpellSkill::transcendent) {
        return 3;
    }
    else {
        return 6 + (int)skill * 3;
    }
}

Range SpellPestilence::duration_range(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {8, 12};
    case SpellSkill::expert:       return {12, 16};
    // NOTE: On master level, the rats are hasted, meaning they disappear twice as fast from
    // the perspective of a normal speed player.
    case SpellSkill::master:       return {40, 60};
    case SpellSkill::transcendent: return {40, 60};
    }

    ASSERT(false);

    return {1, 1};
}

int SpellPestilence::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 7;
}

void SpellPestilence::on_rat_summoned(
    actor::Actor* const mon,
    const SpellSkill skill) const
{
    {
        prop::Prop* prop = prop::make(prop::Id::summoned);
        const int duration = duration_range(skill).roll();
        prop->set_duration(duration);
        mon->m_properties.apply(prop);
    }

    {
        prop::Prop* prop = prop::make(prop::Id::waiting);
        prop->set_duration(1);
        mon->m_properties.apply(prop);
    }

    if (skill == SpellSkill::master) {
        prop::Prop* prop = prop::make(prop::Id::hasted);

        prop->set_indefinite();

        mon->m_properties.apply(prop, prop::PropSrc::intr, true, Verbose::no);
    }
}

void SpellPestilence::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    const size_t nr_mon = nr_rats_summoned(skill);

    actor::Actor* leader = nullptr;

    if (actor::is_player(caster)) {
        leader = caster;
    }
    else {
        // Caster is monster
        actor::Actor* const caster_leader = caster->m_leader;

        leader = caster_leader ? caster_leader : caster;
    }

    std::vector<std::pair<SpellSkill, std::string>> to_summon;

    if (skill == SpellSkill::transcendent) {
        // On transcendent level, spawn a bunch of normal rats as if on expert level, plus
        // some magical "transcendent rats".
        to_summon.emplace_back(SpellSkill::expert, "MON_RAT");

        to_summon.emplace_back(SpellSkill::transcendent, "MON_TRANSCENDENT_RAT");
    }
    else {
        to_summon.emplace_back(skill, "MON_RAT");
    }

    bool is_any_summoned = false;
    bool is_any_seen_by_player = false;

    for (const auto& summon_entry : to_summon) {
        const SpellSkill skill_to_use = summon_entry.first;
        const std::string id = summon_entry.second;

        const actor::MonSpawnResult mon_summoned =
            actor::spawn(
                caster->m_pos,
                {nr_mon, id},
                g_fov_radi_int,
                actor::SpawnScattered::yes)
                .make_aware_of_player()
                .set_leader(leader);

        is_any_summoned = !mon_summoned.monsters.empty() || is_any_summoned;

        is_any_seen_by_player =
            std::any_of(
                std::begin(mon_summoned.monsters),
                std::end(mon_summoned.monsters),
                [](auto* const mon) {
                    return actor::can_player_see_actor(*mon);
                }) ||
            is_any_seen_by_player;

        std::for_each(
            std::begin(mon_summoned.monsters),
            std::end(mon_summoned.monsters),
            [skill_to_use, this](auto& mon) {
                on_rat_summoned(mon, skill_to_use);
            });
    }

    if (!is_any_summoned) {
        return;
    }

    if (actor::is_player(caster) || is_any_seen_by_player) {
        msg_log::add("Rats appear!");
    }
}

std::vector<std::string> SpellPestilence::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back("A pack of rats appear around the caster.");

    if (skill < SpellSkill::transcendent) {
        // Normal description (basic/expert/master).

        const size_t nr_mon = nr_rats_summoned(skill);

        const Range duration = duration_range(skill);

        descr.emplace_back(
            "Summons " +
            std::to_string(nr_mon) +
            " rats. They exist for " +
            duration.str() +
            " turns (their own turns).");

        if (skill == SpellSkill::master) {
            descr.emplace_back("The rats are Hasted (moves faster).");
        }
    }
    else {
        // Transcendent description.

        descr.emplace_back(
            "Some of the rats are ethereal "
            "(much harder to hit, can move through solid objects), "
            "are immune to magic, can cast spells, and have "
            "extra hit points and damage.");
    }

    return descr;
}

bool SpellPestilence::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)skill;

    // Always allow casting with a visible target.
    if (!seen_targets.empty()) {
        return true;
    }

    // Sometimes allow casting if monster has an unseen target.
    if (mon.m_ai_state.target && rnd::one_in(30)) {
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------------
// Mirror Images
// -----------------------------------------------------------------------------
std::string SpellMirrorImages::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Mirror Images";
}

SpellId SpellMirrorImages::id() const
{
    return SpellId::mirror_images;
}

SpellDomain SpellMirrorImages::domain() const
{
    return SpellDomain::illusion;
}

SpellShock SpellMirrorImages::shock_type() const
{
    return SpellShock::mild;
}

bool SpellMirrorImages::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

int SpellMirrorImages::nr_mirror_images_summoned(SpellSkill skill) const
{
    if (skill == SpellSkill::transcendent) {
        return 6;
    }
    else {
        return 2 + (int)skill;
    }
}

Range SpellMirrorImages::duration_range(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {8, 12};
    case SpellSkill::expert:       return {12, 16};
    case SpellSkill::master:       return {16, 20};
    case SpellSkill::transcendent: return {40, 60};
    }

    ASSERT(false);

    return {1, 1};
}

int SpellMirrorImages::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 5;
}

void SpellMirrorImages::on_mirror_image_summoned(
    actor::Actor* const mon,
    const SpellSkill skill) const
{
    {
        prop::Prop* prop = prop::make(prop::Id::summoned);
        const int duration = duration_range(skill).roll();
        prop->set_duration(duration);
        mon->m_properties.apply(prop);
    }

    {
        prop::Prop* prop = prop::make(prop::Id::waiting);
        prop->set_duration(1);
        mon->m_properties.apply(prop);
    }
}

void SpellMirrorImages::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    ASSERT(actor::is_player(caster));

    const size_t nr_mon = nr_mirror_images_summoned(skill);

    const std::string id = "MON_MIRROR_IMAGE";

    const actor::MonSpawnResult mon_summoned =
        actor::spawn(
            caster->m_pos,
            {nr_mon, id},
            g_fov_radi_int,
            actor::SpawnScattered::no)
            .make_aware_of_player()
            .set_leader(map::g_player);

    std::for_each(
        std::begin(mon_summoned.monsters),
        std::end(mon_summoned.monsters),
        [skill, this](auto& mon) {
            on_mirror_image_summoned(mon, skill);
        });

    if (mon_summoned.monsters.empty()) {
        return;
    }

    draw_blast_at_seen_actors(mon_summoned.monsters, colors::magenta());

    msg_log::add("Images appear!");
}

std::vector<std::string> SpellMirrorImages::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Conjures illusory duplicates of the caster "
        "to mislead enemies and draw their attacks.");

    descr.emplace_back(
        "The mirror images project a powerful magical presence, "
        "causing attackers to prefer them over the caster. "
        "As magical apparitions rather than living creatures, "
        "they are extremely difficult to strike with conventional attacks. "
        "They are immune to elemental damage and largely unaffected by physical "
        "or mental afflictions.");

    const size_t nr_mon = nr_mirror_images_summoned(skill);

    const Range duration = duration_range(skill);

    descr.emplace_back(
        "Creates " +
        std::to_string(nr_mon) +
        " mirror images. They exist for " +
        duration.str() +
        " turns (their own turns).");

    return descr;
}

// -----------------------------------------------------------------------------
// Projected Strike
// -----------------------------------------------------------------------------
std::string SpellProjectedStrike::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Projected Strike";
}

SpellId SpellProjectedStrike::id() const
{
    return SpellId::projected_strike;
}

SpellDomain SpellProjectedStrike::domain() const
{
    return SpellDomain::mind;
}

SpellShock SpellProjectedStrike::shock_type() const
{
    return SpellShock::mild;
}

bool SpellProjectedStrike::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

int SpellProjectedStrike::max_nr_weapons(const SpellSkill skill) const
{
    if (skill == SpellSkill::transcendent) {
        return -1;
    }
    else {
        return 3 + ((int)skill * 3);
    }
}

int SpellProjectedStrike::hit_chance_bonus(SpellSkill skill) const
{
    return 10 * ((int)skill + 1);
}

int SpellProjectedStrike::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 6;
}

std::vector<const item::Item*> SpellProjectedStrike::get_weapons(SpellSkill skill) const
{
    auto is_melee_wpn = [](const auto* const item) {
        return item && (item->data().type == ItemType::melee_wpn);
    };

    std::vector<const item::Item*> weapons;

    // Assuming the caster is always the player.
    for (const auto& slot : map::g_player->m_inv.m_slots) {
        if (is_melee_wpn(slot.item)) {
            weapons.push_back(slot.item);
        }
    }

    for (const auto& item : map::g_player->m_inv.m_backpack) {
        if (is_melee_wpn(item)) {
            weapons.push_back(item);
        }
    }

    // Cap the number of weapons spawned
    rnd::shuffle(weapons);

    const int nr_max = max_nr_weapons(skill);

    if ((nr_max != -1) && ((size_t)nr_max < weapons.size())) {
        weapons.resize(nr_max);
    }

    return weapons;
}

void SpellProjectedStrike::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    TRACE_FUNC_BEGIN;

    (void)player_aware;

    if (!actor::is_player(caster)) {
        ASSERT(false);

        return;
    }

    std::vector<const item::Item*> weapons = get_weapons(skill);

    if (seen_targets.empty() || weapons.empty()) {
        msg_log::add("Visions of hacking, crushing and stabbing fill my mind.");

        return;
    }

    std::vector<actor::Actor*> targets = seen_targets;

    rnd::shuffle(weapons);
    rnd::shuffle(targets);

    auto remove_actor = [](std::vector<actor::Actor*>& actors, const actor::Actor* actor) {
        actors.erase(
            std::remove(std::begin(actors), std::end(actors), actor),
            std::end(actors));
    };

    auto remove_dead_actors = [](std::vector<actor::Actor*>& actors) {
        actors.erase(
            std::remove_if(
                std::begin(actors), std::end(actors), [](const actor::Actor* actor) {
                    return !actor::is_alive(*actor);
                }),
            std::end(actors));
    };

    for (size_t i = 0; i < weapons.size(); ++i) {
        const item::Item* const origin_wpn = weapons[i];

        std::unique_ptr<item::Item> new_wpn(item::make(origin_wpn->id()));

        new_wpn->m_melee_hit_chance_mod += hit_chance_bonus(skill);

        actor::Actor* const target = rnd::element(targets);

        // Calculate an origin adjacent to the target creature, for correct knockback direction
        // based on the relative positions of the caster and the target creature.
        const P attack_origin = target->m_pos + (caster->m_pos - target->m_pos).signs();

        attack::melee(
            caster,
            attack_origin,
            target->m_pos,
            *static_cast<item::Wpn*>(new_wpn.get()),
            attack::AttackSource::magical);

        // Each target can only be hit once, remove this target from the list of possible targets.
        remove_actor(targets, target);

        // Remove all dead actors to handle cases like the attacked actor being a creature that
        // explodes on death, killing other actors.
        remove_dead_actors(targets);

        if (targets.empty()) {
            break;
        }

        // Run a sleep if more attacks will happen, to avoid a bunch of sounds playing at exactly
        // the same time.
        if (i < (weapons.size() - 1)) {
            states::draw();
            io::update_screen();
            io::sleep(config::base_delay());
        }
    }

    TRACE_FUNC_END;
}

std::vector<std::string> SpellProjectedStrike::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back("Launches a psychic projection of the caster's carried melee weapons.");

    descr.emplace_back(
        "Each projection attacks a visible enemy, using the caster's combat skill with +" +
        std::to_string(hit_chance_bonus(skill)) +
        "% hit chance bonus. "
        "No enemy can be targeted more than once.");

    const int nr_max = max_nr_weapons(skill);

    std::string nr_str;

    if (nr_max == -1) {
        nr_str = "An unlimited number of weapons can be used for atacking.";
    }
    else {
        nr_str = "A maximum of " + std::to_string(nr_max) + " ";

        if (nr_max == 1) {
            nr_str += "weapon";
        }
        else {
            nr_str += "weapons";
        }

        nr_str += " may be used for attacking.";
    }

    descr.push_back(nr_str);

    descr.emplace_back(
        "The caster acts as attacker - all normal conditions that affect "
        "hit chance or damage apply "
        "(e.g. bonus damage from melee traits, or damage penalty from being weakened).");

    return descr;
}

// -----------------------------------------------------------------------------
// Control Object
// -----------------------------------------------------------------------------
std::string SpellControlObject::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Control Object";
}

SpellId SpellControlObject::id() const
{
    return SpellId::control_object;
}

SpellDomain SpellControlObject::domain() const
{
    return SpellDomain::mind;
}

SpellShock SpellControlObject::shock_type() const
{
    return SpellShock::mild;
}

int SpellControlObject::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)caster;

    if (skill == SpellSkill::transcendent) {
        return 1;
    }
    else {
        return 4;
    }
}

int SpellControlObject::max_dist(const SpellSkill skill) const
{
    int dist = (int)skill + 3;

    dist = std::min(g_fov_radi_int, dist);

    return dist;
}

void SpellControlObject::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    const auto origin = caster->m_pos;

    auto ctrl_obj_state = std::make_unique<CtrlObj>(origin, max_dist(skill), skill);

    // Run the state immediately, so that spell side effects happen AFTER
    // the player has finished casting the spell.
    states::run_until_state_done(std::move(ctrl_obj_state));
}

std::vector<std::string> SpellControlObject::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    std::string control_descr =
        "Opens doors, chests, tombs, or cabinets. "
        "Closes or jams doors. "
        "Strikes doors, braziers, or statues.";

    if (skill == SpellSkill::transcendent) {
        control_descr += " Walls can be destroyed.";
    }

    descr.emplace_back(control_descr);

    descr.emplace_back("Maximum control distance is " + std::to_string(max_dist(skill)) + ".");

    descr.emplace_back(
        "When casting the spell, select a seen object to control "
        "within the maximum distance.");

    return descr;
}

bool SpellControlObject::is_noisy(const SpellSkill skill) const
{
    return (skill == SpellSkill::basic);
}

// -----------------------------------------------------------------------------
// Exorcist Brilliance
// -----------------------------------------------------------------------------
std::string SpellBrilliance::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Brilliance";
}

SpellId SpellBrilliance::id() const
{
    return SpellId::brilliance;
}

SpellDomain SpellBrilliance::domain() const
{
    return SpellDomain::END;
}

SpellShock SpellBrilliance::shock_type() const
{
    return SpellShock::mild;
}

int SpellBrilliance::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 4;
}

bool SpellBrilliance::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

Range SpellBrilliance::light_sensitive_duration_range(const SpellSkill skill) const
{
    if (skill == SpellSkill::basic) {
        return {10, 20};
    }
    else {
        return {20, 30};
    }
}

Range SpellBrilliance::light_dmg_range(const SpellSkill skill) const
{
    if (skill == SpellSkill::basic) {
        return {3, 6};  // Avg 4.5
    }
    else {
        return {5, 9};  // Avg 7.0
    }
}

void SpellBrilliance::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    if (!caster) {
        return;
    }

    audio::play(audio::SfxId::brilliance);

    if (seen_targets.empty()) {
        return;
    }

    draw_blast_at_seen_actors(seen_targets, colors::yellow());

    for (actor::Actor* const actor : seen_targets) {
        if (actor->m_properties.has(prop::Id::r_spell)) {
            on_resist(*actor, caster);
            continue;
        }

        run_effect_on_creature(actor, skill);
    }
}

void SpellBrilliance::run_effect_on_creature(actor::Actor* actor, const SpellSkill skill) const
{
    if (actor->m_properties.has(prop::Id::light_sensitive)) {
        // Target is already light sensitive.
        const int dmg = light_dmg_range(skill).roll();

        actor::hit(*actor, dmg, DmgType::light, map::g_player, AllowWound::no);
    }

    prop::Prop* const lgt_sens = prop::make(prop::Id::light_sensitive);

    const int duration = light_sensitive_duration_range(skill).roll();

    lgt_sens->set_duration(duration);

    actor->m_properties.apply(lgt_sens);
}

std::vector<std::string> SpellBrilliance::descr_specific(SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Draws on the power of light to smite the caster's enemies. "
        "Any radiance becomes a searing torment for those it touches.");

    descr.emplace_back(
        "Any visible hostile creature becomes light sensitive for " +
        light_sensitive_duration_range(skill).str() +
        " turns, taking 1 damage each turn they spend in a fully lit position. "
        "Those already sensitive to light also suffer " +
        light_dmg_range(skill).str() +
        " light damage.");

    return descr;
}

// -----------------------------------------------------------------------------
// Exorcist Cleansing Fire
// -----------------------------------------------------------------------------
std::string SpellCleansingFire::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Cleansing Fire";
}

SpellId SpellCleansingFire::id() const
{
    return SpellId::cleansing_fire;
}

SpellDomain SpellCleansingFire::domain() const
{
    return SpellDomain::END;
}

SpellShock SpellCleansingFire::shock_type() const
{
    return SpellShock::disturbing;
}

int SpellCleansingFire::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 7;
}

bool SpellCleansingFire::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

Range SpellCleansingFire::burn_duration_range() const
{
    return {3, 5};
}

void SpellCleansingFire::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    if (!caster) {
        return;
    }

    std::vector<actor::Actor*> targets;

    if (seen_targets.empty()) {
        return;
    }

    if (skill == SpellSkill::basic) {
        targets.push_back(rnd::element(seen_targets));
    }
    else {
        // Skill greater than basic - target all seen foes
        targets = seen_targets;
    }

    for (actor::Actor* const actor : targets) {
        if (actor->m_properties.has(prop::Id::r_spell)) {
            on_resist(*actor, caster);
            continue;
        }

        for (const auto& d : dir_utils::g_dir_list) {
            const auto p(actor->m_pos + d);

            // Hit the terrain with burning several times, to increase the chance of it
            // catching fire.
            for (int i = 0; i < 6; ++i) {
                map::g_terrain.at(p)->hit(DmgType::fire, nullptr);
            }
        }

        prop::Prop* const burning = prop::make(prop::Id::burning);

        burning->set_duration(burn_duration_range().roll());

        actor->m_properties.apply(burning);
    }
}

std::vector<std::string> SpellCleansingFire::descr_specific(
    SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Causes the spell's victims to burn for " +
        burn_duration_range().str() +
        " turns, and scorches the ground around them with fire "
        "(be careful with hitting adjacent creatures).");

    descr.emplace_back(
        skill == SpellSkill::basic
            ? "Affects one random visible hostile creature."
            : "Affects all visible hostile creatures.");

    return descr;
}

// -----------------------------------------------------------------------------
// Exorcist Sanctuary
// -----------------------------------------------------------------------------
std::string SpellSanctuary::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Sanctuary";
}

SpellId SpellSanctuary::id() const
{
    return SpellId::sanctuary;
}

SpellDomain SpellSanctuary::domain() const
{
    return SpellDomain::END;
}

SpellShock SpellSanctuary::shock_type() const
{
    return SpellShock::mild;
}

int SpellSanctuary::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 5;
}

bool SpellSanctuary::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

Range SpellSanctuary::duration(const SpellSkill skill) const
{
    if (skill == SpellSkill::basic) {
        return {3, 5};
    }
    else {
        return {5, 10};
    }
}

void SpellSanctuary::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    if (!caster) {
        return;
    }

    const auto prop_duration = duration(skill);

    auto* const sanctuary = prop::make(prop::Id::sanctuary);

    sanctuary->set_duration(prop_duration.roll());

    caster->m_properties.apply(sanctuary);
}

std::vector<std::string> SpellSanctuary::descr_specific(
    SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "The caster is ignored by all hostile creatures for the "
        "duration of the spell. The effect is interrupted if the "
        "caster moves or performs a melee or ranged attack.");

    descr.emplace_back("The spell lasts " + duration(skill).str() + " turns.");

    return descr;
}

// -----------------------------------------------------------------------------
// Exorcist Purge
// -----------------------------------------------------------------------------
std::string SpellPurge::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Purge";
}

SpellId SpellPurge::id() const
{
    return SpellId::purge;
}

SpellDomain SpellPurge::domain() const
{
    return SpellDomain::END;
}

bool SpellPurge::can_be_improved_with_skill() const
{
    return false;
}

SpellShock SpellPurge::shock_type() const
{
    return SpellShock::mild;
}

int SpellPurge::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 4;
}

bool SpellPurge::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

Range SpellPurge::dmg_range() const
{
    return {5, 10};
}

Range SpellPurge::fear_duration_range() const
{
    return {3, 6};
}

void SpellPurge::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)skill;
    (void)seen_targets;
    (void)player_aware;

    if (!caster) {
        return;
    }

    for (const P& d : dir_utils::g_dir_list) {
        const auto p(caster->m_pos + d);

        terrain::Terrain* const terrain = map::g_terrain.at(p);

        switch (terrain->id()) {
        case terrain::Id::altar:
        case terrain::Id::monolith:
        case terrain::Id::mirror:
        case terrain::Id::gong:     {
            if (map::g_seen.at(p)) {
                draw_blast_at_cells({p}, colors::light_white());
            }

            terrain->hit(DmgType::pure, caster);
        } break;

        default: {
        } break;
        }
    }

    for (actor::Actor* const actor : game_time::g_actors) {
        if ((actor == caster) ||
            !actor->m_pos.is_adjacent(caster->m_pos) ||
            !actor->m_properties.has(prop::Id::undead)) {
            continue;
        }

        // Is adjacent undead creature

        if (actor::can_player_see_actor(*actor)) {
            const auto name = text_format::first_to_upper(actor::name_the(*actor));

            msg_log::add(name + " is struck.", colors::msg_good());

            draw_blast_at_cells({actor->m_pos}, colors::light_white());
        }

        actor::hit(
            *actor,
            dmg_range().roll(),
            DmgType::pure,
            caster);

        if (actor::is_alive(*actor)) {
            prop::Prop* const fear = prop::make(prop::Id::terrified);

            fear->set_duration(fear_duration_range().roll());

            actor->m_properties.apply(fear);
        }
    }
}

std::vector<std::string> SpellPurge::descr_specific(
    SpellSkill skill) const
{
    (void)skill;

    std::vector<std::string> descr;

    descr.emplace_back(
        "Destroys any altars, monoliths, gongs, or mirrors adjacent to the caster.");

    descr.emplace_back(
        "All Undead creatures adjacent to the caster (seen or not) are "
        "struck with " +
        dmg_range().str() +
        " damage, and become terrified for " +
        fear_duration_range().str() +
        " turns (unless they resist fear).");

    return descr;
}

// -----------------------------------------------------------------------------
// Ghoul frenzy
// -----------------------------------------------------------------------------
std::string SpellFrenzy::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "an " : "";

    return a + "Incite Frenzy";
}

SpellId SpellFrenzy::id() const
{
    return SpellId::frenzy;
}

SpellDomain SpellFrenzy::domain() const
{
    return SpellDomain::END;
}

bool SpellFrenzy::can_be_improved_with_skill() const
{
    return false;
}

SpellShock SpellFrenzy::shock_type() const
{
    return SpellShock::mild;
}

int SpellFrenzy::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 0;
}

bool SpellFrenzy::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

void SpellFrenzy::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)skill;
    (void)seen_targets;
    (void)player_aware;

    prop::Prop* prop = prop::make(prop::Id::frenzied);

    prop->set_duration(rnd::range(30, 40));

    caster->m_properties.apply(prop);
}

std::vector<std::string> SpellFrenzy::descr_specific(
    const SpellSkill skill) const
{
    (void)skill;

    return {
        "Incites a great rage in the caster, who will charge their "
        "enemies with a terrible, uncontrollable fury."};
}

// -----------------------------------------------------------------------------
// Bless
// -----------------------------------------------------------------------------
std::string SpellBless::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Bless";
}

SpellId SpellBless::id() const
{
    return SpellId::bless;
}

SpellDomain SpellBless::domain() const
{
    return SpellDomain::warding;
}

bool SpellBless::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

SpellShock SpellBless::shock_type() const
{
    return SpellShock::mild;
}

Range SpellBless::duration_range(SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:  return {15, 30};
    case SpellSkill::expert: return {60, 120};
    case SpellSkill::master: return {150, 300};

    case SpellSkill::transcendent:
        // Unexpected, the spell should be indefinite
        break;
    }

    ASSERT(false);

    return {1, 1};
}

int SpellBless::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 6;
}

void SpellBless::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    prop::Prop* prop = prop::make(prop::Id::blessed);

    if (skill == SpellSkill::transcendent) {
        prop->set_indefinite();
    }
    else {
        prop->set_duration(duration_range(skill).roll());
    }

    caster->m_properties.apply(prop);
}

std::vector<std::string> SpellBless::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "The caster becomes more lucky "
        "(+10% to hit chance, evasion, stealth, and searching).");

    if (skill == SpellSkill::transcendent) {
        descr.emplace_back("The spell lasts indefinitely.");
    }
    else {
        descr.push_back("The spell lasts " + duration_range(skill).str() + " turns.");
    }

    return descr;
}

// -----------------------------------------------------------------------------
// Cancellation
// -----------------------------------------------------------------------------
std::string SpellCancellation::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Cancellation";
}

SpellId SpellCancellation::id() const
{
    return SpellId::cancellation;
}

SpellDomain SpellCancellation::domain() const
{
    return SpellDomain::warding;
}

bool SpellCancellation::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

SpellShock SpellCancellation::shock_type() const
{
    return SpellShock::mild;
}

int SpellCancellation::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 4;
}

int SpellCancellation::max_dist(SpellSkill skill) const
{
    return 3 + ((int)skill * 2);
}

Range SpellCancellation::damage_for_vulnerable_creatures() const
{
    return {1, 4};
}

std::vector<CancelledPropData> SpellCancellation::negative_effect_types_cancelled() const
{
    // NOTE: Do not overlap with the Heal spell. Keep it to more "magical" or mental effects,
    // rather than physical/mundane things like poisoning.
    return {
        {prop::Id::cursed},
        {prop::Id::doomed},
        {prop::Id::slowed},
        {prop::Id::terrified},
        {prop::Id::confused},
        {prop::Id::fainted},
        {prop::Id::conflict},
        {prop::Id::hallucinating},
    };
}

std::vector<CancelledPropData> SpellCancellation::positive_effect_types_cancelled() const
{
    return {
        {prop::Id::r_spell, CancelledPropIncludeInDescr::no, CancelledPropAllowPermanent::yes},
        {prop::Id::r_phys, CancelledPropIncludeInDescr::no},
        {prop::Id::r_fire, CancelledPropIncludeInDescr::no},
        {prop::Id::r_poison, CancelledPropIncludeInDescr::no},
        {prop::Id::r_elec, CancelledPropIncludeInDescr::no},
        {prop::Id::r_sleep, CancelledPropIncludeInDescr::no},
        {prop::Id::r_fear, CancelledPropIncludeInDescr::no},
        {prop::Id::r_slow, CancelledPropIncludeInDescr::no},
        {prop::Id::r_conf, CancelledPropIncludeInDescr::no},
        {prop::Id::blessed},
        {prop::Id::radiant_fov},
        {prop::Id::hasted},
        {prop::Id::extra_hasted, CancelledPropIncludeInDescr::no},
        {prop::Id::frenzied},
        {prop::Id::cloaked},
        {prop::Id::invis},
        {prop::Id::premonition},
        {prop::Id::erudition},
        {prop::Id::magic_carapace},
        {prop::Id::extra_skill},
    };
}

void SpellCancellation::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;

    const int dist = max_dist(skill);

    std::vector<actor::Actor*> reached_actors {caster};

    for (actor::Actor* const actor : game_time::g_actors) {
        const bool include_actor =
            (actor != caster) &&
            actor::is_alive(*actor) &&
            (king_dist(caster->m_pos, actor->m_pos) <= dist);

        if (include_actor) {
            reached_actors.push_back(actor);
        }
    }

    if (player_aware == PlayerAwareOfCast::yes) {
        msg_log::add("A negating force sweeps the area.");

        audio::play(audio::SfxId::cancellation);

        draw_blast_at_seen_actors({reached_actors}, colors::light_white());
    }

    for (actor::Actor* const actor : reached_actors) {
        if (!actor::is_alive(*map::g_player)) {
            break;
        }

        // Effects ending or creatures being damaged could potentially kill other creatures.
        if (!actor::is_alive(*actor)) {
            continue;
        }

        if (actor::is_alive(*actor)) {
            run_effect_on_actor(*actor, *caster);
        }
    }
}

void SpellCancellation::run_effect_on_actor(
    actor::Actor& actor,
    actor::Actor& caster) const
{
    TRACE << "Cancelling effects on actor '" << actor::name_a(actor) << "'" << std::endl;

    if (actor::is_allied(&caster, &actor)) {
        TRACE << "Caster and target are allied" << std::endl;

        cancel_negative_effects(actor);
    }
    else {
        TRACE << "Caster and target are enemies" << std::endl;

        cancel_positive_effects(actor);

        if (actor::is_alive(actor) && actor::is_alive(*map::g_player)) {
            do_damage_vulnerable_creature(actor, caster);
        }
    }
}

void SpellCancellation::cancel_negative_effects(actor::Actor& actor) const
{
    TRACE << "Cancelling negative effects" << std::endl;

    for (const CancelledPropData& data : negative_effect_types_cancelled()) {
        TRACE << "Ending '" << prop::g_data[(size_t)data.id].name << "'\n";

        if (data.allow_cancel_permanent_effect == CancelledPropAllowPermanent::yes) {
            actor.m_properties.end_prop(data.id);
        }
        else {
            actor.m_properties.end_temporary_prop(data.id);
        }

        if (!actor::is_alive(actor) || !actor::is_alive(*map::g_player)) {
            return;
        }
    }
}

void SpellCancellation::cancel_positive_effects(actor::Actor& actor) const
{
    TRACE << "Cancelling positive effects" << std::endl;

    for (const CancelledPropData& data : positive_effect_types_cancelled()) {
        TRACE << "Ending '" << prop::g_data[(size_t)data.id].name << "'\n";

        bool did_end = false;

        if (data.allow_cancel_permanent_effect == CancelledPropAllowPermanent::yes) {
            did_end = actor.m_properties.end_prop(data.id);
        }
        else {
            did_end = actor.m_properties.end_temporary_prop(data.id);
        }

        if (did_end) {
            // The spell "pierces through" spell shield, but a player with the absorption trait
            // shall still receive spirit points (this is also consistent with the absorption trait
            // description).
            if (data.id == prop::Id::r_spell &&
                actor::is_player(&actor) &&
                player_bon::has_trait(TraitId::absorption)) {
                give_player_sp_for_resist_with_absorption_trait();
            }
        }

        if (!actor::is_alive(actor) || !actor::is_alive(*map::g_player)) {
            return;
        }
    }
}

void SpellCancellation::do_damage_vulnerable_creature(
    actor::Actor& actor,
    actor::Actor& caster) const
{
    const std::vector<prop::Id> vulnerable_props = {
        prop::Id::outer_being,
        prop::Id::undead,
        prop::Id::summoned,
    };

    if (!actor.m_properties.has_any(vulnerable_props)) {
        return;
    }

    if (actor::can_player_see_actor(actor)) {
        const std::string name = text_format::first_to_lower(actor::name_the(actor));

        msg_log::add(name + " unravels.");
    }

    const int dmg = damage_for_vulnerable_creatures().roll();

    actor::hit(actor, dmg, DmgType::pure, &caster);

    if (!actor::is_player(&actor)) {
        actor.become_aware_player(actor::AwareSource::spell_victim);
    }
}

std::vector<std::string> SpellCancellation::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr = {
        "Cancels temporary effects on nearby creatures. "
        "Pierces through and removes Spell Shield."};

    descr.push_back(
        "Outer Beings, Undead or Summoned creatures also take " +
        damage_for_vulnerable_creatures().str() +
        " damage.");

    descr.push_back(
        "The spell has a maximum range of " +
        std::to_string(max_dist(skill)) +
        " steps, reaching through solid obstacles.");

    auto to_names = [](const std::vector<CancelledPropData>& entries) {
        std::vector<std::string> names;

        for (const CancelledPropData& data : entries) {
            if (data.include_in_descr == CancelledPropIncludeInDescr::yes) {
                names.push_back(prop::name(data.id));
            }
        }

        return names;
    };

    const std::vector<std::string> negative_effect_names =
        to_names(negative_effect_types_cancelled());

    std::vector<std::string> positive_effect_names =
        to_names(positive_effect_types_cancelled());

    descr.push_back(
        "Effects removed from enemies: All resistances, " +
        text_format::make_comma_and_str(positive_effect_names) +
        ".");

    descr.push_back(
        "From caster/allies: " +
        text_format::make_comma_and_str(negative_effect_names) +
        ".");

    return descr;
}

int SpellCancellation::mon_cooldown() const
{
    return 5;
}

bool SpellCancellation::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)seen_targets;

    // Player-hostile creatures (normal case) can cast the spell if either:
    // * They can cancel a negative effect on themselves, OR
    // * They can cancel a positive effect on the player.
    //
    // Player-friendly creatures can only cast the spell if they can cancel a negative effect on
    // themselves.
    //

    const std::vector<CancelledPropData> negative_cancelled = negative_effect_types_cancelled();

    const bool has_negative = std::any_of(
        std::begin(negative_cancelled),
        std::end(negative_cancelled),
        [&](const CancelledPropData& d) { return mon.m_properties.has(d.id); });

    if (has_negative) {
        return true;
    }

    // Cancelling negative effect on self not possible. Check if player-hostile monster can cancel
    // an effect on the player.

    if (actor::is_in_same_group(&mon, map::g_player) ||
        !actor::is_aware_of_player(mon) ||
        (king_dist(mon.m_pos, map::g_player->m_pos) > max_dist(skill))) {
        return false;
    }

    const std::vector<CancelledPropData> positive_cancelled = positive_effect_types_cancelled();

    for (const CancelledPropData& d : positive_cancelled) {
        if (map::g_player->m_properties.has(d.id)) {
            return true;
        }
    }

    const bool has_positive = std::any_of(
        std::begin(positive_cancelled),
        std::end(positive_cancelled),
        [](const CancelledPropData& d) { return map::g_player->m_properties.has(d.id); });

    return has_positive;
}

// -----------------------------------------------------------------------------
// Inscribe Boundary Sigil
// -----------------------------------------------------------------------------
std::string SpellInscribeBoundarySigil::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "an " : "";

    return a + "Inscribe Boundary Sigil";
}

SpellId SpellInscribeBoundarySigil::id() const
{
    return SpellId::inscribe_boundary_sigil;
}

SpellDomain SpellInscribeBoundarySigil::domain() const
{
    return SpellDomain::warding;
}

bool SpellInscribeBoundarySigil::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

SpellShock SpellInscribeBoundarySigil::shock_type() const
{
    return SpellShock::disturbing;
}

Range SpellInscribeBoundarySigil::nr_actions_prevented(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {3, 9};
    case SpellSkill::expert:       return {3, 12};
    case SpellSkill::master:       return {3, 15};
    case SpellSkill::transcendent: return {6, 20};
    }

    ASSERT(false);
    return {1, 1};
}

int SpellInscribeBoundarySigil::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 5;
}

void SpellInscribeBoundarySigil::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    // TODO: There should be a casting sound.

    const terrain::Terrain* prev_terrain = map::g_terrain.at(caster->m_pos);
    terrain::Id prev_terrain_id = prev_terrain->id();

    const bool is_trap_here = prev_terrain_id == terrain::Id::trap;
    const bool is_floor_here = prev_terrain_id == terrain::Id::floor;

    if (!is_floor_here && !is_trap_here) {
        if (map::g_player->m_properties.allow_see()) {
            msg_log::add("A symbol flickers briefly, but fails to bind here.");
        }
        else {
            // NOTE: Assuming that the player is casting an already known spell (not possible to
            // cast from Manuscripts while blind).
            msg_log::add("I sense that the sigil failed to bind here.");
        }

        return;
    }

    msg_log::add("A sigil is written on the ground.");

    if (is_trap_here) {
        // A trap existed here, clear the previous trap first by placing another mimic terrain
        // (placing traps directly on other traps is not allowed).
        terrain::Terrain* const tmp_mimic = make_trap_mimic_terrain(caster->m_pos);
        map::update_terrain(tmp_mimic);
    }

    terrain::Terrain* const mimic = make_trap_mimic_terrain(caster->m_pos);

    auto* const trap =
        static_cast<terrain::Trap*>(
            terrain::make(terrain::Id::trap, caster->m_pos));

    trap->set_mimic_terrain(mimic);

    const bool is_trap_ok = trap->try_init_type(terrain::TrapId::boundary);

    if (!is_trap_ok) {
        // There shouldn't be any reason for this to happen.
        ASSERT(false);

        delete trap;

        return;
    }

    auto* const boundary = static_cast<terrain::TrapBoundary*>(trap->trap_impl());

    boundary->set_nr_actions_to_prevent(nr_actions_prevented(skill).roll());

    map::update_terrain(trap);

    trap->reveal(terrain::PrintRevealMsg::no);
}

terrain::Terrain* SpellInscribeBoundarySigil::make_trap_mimic_terrain(const P& from_pos) const
{
    const terrain::Terrain* terrain = map::g_terrain.at(from_pos);

    if (terrain->id() == terrain::Id::trap) {
        // A trap is here, use its mimic terrain as mimic terrain instead.
        terrain = static_cast<const terrain::Trap*>(terrain)->get_mimic_terrain();
    }

    terrain::Terrain* const mimic = terrain::make(terrain->id(), from_pos);

    if (terrain->id() == terrain::Id::floor) {
        // The terrain to mimic is a floor, set correct floor type.
        static_cast<terrain::Floor*>(mimic)->m_type =
            static_cast<const terrain::Floor*>(terrain)->m_type;
    }

    return mimic;
}

std::vector<std::string> SpellInscribeBoundarySigil::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Inscribes a magical sigil upon the ground, "
        "preventing Outer Beings, Undead and Summoned creatures "
        "from entering it or making melee attacks across its boundary.");

    descr.emplace_back(
        "The sigil can prevent " +
        nr_actions_prevented(skill).str() +
        " actions before it fades, "
        "though it also has a small chance to fade each turn.");

    descr.emplace_back("Can only be inscribed on floor, but may overwrite an existing sigil.");

    return descr;
}

// -----------------------------------------------------------------------------
// Light
// -----------------------------------------------------------------------------
std::string SpellLight::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Light";
}

SpellId SpellLight::id() const
{
    return SpellId::light;
}

SpellDomain SpellLight::domain() const
{
    return SpellDomain::warding;
}

SpellShock SpellLight::shock_type() const
{
    return SpellShock::mild;
}

int SpellLight::base_max_cost(
    SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 5;
}

bool SpellLight::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

Range SpellLight::light_duration_range(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {10, 20};
    case SpellSkill::expert:       return {15, 30};
    case SpellSkill::master:
    case SpellSkill::transcendent: return {20, 40};
    }

    ASSERT(false);

    return {1, 1};
}

Range SpellLight::blind_duration_range(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:
    case SpellSkill::expert:
        // Not expected, should not cause blinding at these levels.
        break;

    case SpellSkill::master:       return {1, 3};
    case SpellSkill::transcendent: return {3, 5};
    }

    ASSERT(false);

    return {1, 1};
}

Range SpellLight::burning_duration_range() const
{
    return {3, 6};
}

void SpellLight::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    prop::Prop* radiant = prop::make(prop::Id::radiant_fov);

    radiant->set_duration(light_duration_range(skill).roll());

    caster->m_properties.apply(radiant);

    std::vector<prop::Prop*> properties;

    if (skill >= SpellSkill::master) {
        prop::Prop* const prop = prop::make(prop::Id::blind);

        prop->set_duration(blind_duration_range(skill).roll());

        properties.push_back(prop);
    }

    if (skill == SpellSkill::transcendent) {
        prop::Prop* const prop = prop::make(prop::Id::burning);

        prop->set_duration(burning_duration_range().roll());

        properties.push_back(prop);
    }

    if (!properties.empty()) {
        explosion::run(
            caster->m_pos,
            ExplType::apply_prop,
            EmitExplSnd::no,
            -1,
            ExplExclCenter::yes,
            properties,
            colors::yellow());
    }
}

std::vector<std::string> SpellLight::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back("Illuminates the area around the caster.");

    descr.push_back("The spell lasts " + light_duration_range(skill).str() + " turns.");

    if (skill >= SpellSkill::master) {
        descr.push_back(
            "On casting, causes a blinding flash centered on the "
            "caster (but not affecting the caster itself). "
            "The blinding effect lasts " +
            blind_duration_range(skill).str() +
            " turns.");
    }

    if (skill == SpellSkill::transcendent) {
        descr.push_back(
            "The flash is so intense that any victim caught in it "
            "will also burn for " +
            burning_duration_range().str() +
            " turns.");
    }

    return descr;
}

// -----------------------------------------------------------------------------
// Invisibility
// -----------------------------------------------------------------------------
std::string SpellInvis::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "an " : "";

    return a + "Invisibility";
}

SpellId SpellInvis::id() const
{
    return SpellId::invis;
}

SpellDomain SpellInvis::domain() const
{
    return SpellDomain::illusion;
}

bool SpellInvis::is_tenebrous() const
{
    return true;
}

SpellShock SpellInvis::shock_type() const
{
    return SpellShock::mild;
}

int SpellInvis::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 8;
}

Range SpellInvis::duration_range(SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {4, 6};
    case SpellSkill::expert:       return {5, 7};
    case SpellSkill::master:       return {6, 8};
    case SpellSkill::transcendent: return {8, 10};
    }

    ASSERT(false);

    return {1, 1};
}

bool SpellInvis::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

void SpellInvis::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    const prop::Id prop_id = (skill == SpellSkill::basic) ? prop::Id::cloaked : prop::Id::invis;

    prop::Prop* const prop = prop::make(prop_id);

    prop->set_duration(duration_range(skill).roll());

    caster->m_properties.apply(prop);
}

std::vector<std::string> SpellInvis::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Makes the caster invisible to normal vision for a "
        "brief time.");

    if (skill == SpellSkill::basic) {
        descr.emplace_back(
            "Attacking or casting spells reveals the caster.");
    }
    else {
        descr.emplace_back(
            "The caster is truly invisible for the duration of "
            "the the spell, and can freely attack or cast "
            "spells without breaking the invisibility.");
    }

    descr.push_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

// -----------------------------------------------------------------------------
// See Invisible
// -----------------------------------------------------------------------------
int SpellSeeInvis::mon_cooldown() const
{
    return 30;
}

std::string SpellSeeInvis::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "See Invisible";
}

SpellId SpellSeeInvis::id() const
{
    return SpellId::see_invis;
}

SpellDomain SpellSeeInvis::domain() const
{
    return SpellDomain::mind;
}

SpellShock SpellSeeInvis::shock_type() const
{
    return SpellShock::mild;
}

bool SpellSeeInvis::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

int SpellSeeInvis::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 8;
}

Range SpellSeeInvis::duration_range(SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:  return {15, 30};
    case SpellSkill::expert: return {60, 120};
    case SpellSkill::master: return {250, 500};

    case SpellSkill::transcendent:
        // Unexpected, the spell should be indefinite
        break;
    }

    ASSERT(false);

    return {1, 1};
}

void SpellSeeInvis::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    prop::Prop* prop = prop::make(prop::Id::see_invis);

    if (skill == SpellSkill::transcendent) {
        prop->set_indefinite();
    }
    else {
        prop->set_duration(duration_range(skill).roll());
    }

    caster->m_properties.apply(prop);
}

std::vector<std::string> SpellSeeInvis::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back("Grants the caster the ability to see the invisible.");

    if (skill == SpellSkill::transcendent) {
        descr.emplace_back("The spell lasts indefinitely.");
    }
    else {
        descr.push_back("The spell lasts " + duration_range(skill).str() + " turns.");
    }

    return descr;
}

bool SpellSeeInvis::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)seen_targets;
    (void)skill;

    return (
        !mon.m_properties.has(prop::Id::see_invis) &&
        actor::is_aware_of_player(mon) &&
        rnd::one_in(8));
}

// -----------------------------------------------------------------------------
// Spell Shield
// -----------------------------------------------------------------------------
int SpellSpellShield::mon_cooldown() const
{
    return 3;
}

std::string SpellSpellShield::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Spell Shield";
}

SpellId SpellSpellShield::id() const
{
    return SpellId::spell_shield;
}

SpellDomain SpellSpellShield::domain() const
{
    return SpellDomain::warding;
}

SpellShock SpellSpellShield::shock_type() const
{
    return SpellShock::mild;
}

bool SpellSpellShield::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

int SpellSpellShield::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)caster;

    return 5 - (int)skill;
}

void SpellSpellShield::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)skill;
    (void)seen_targets;
    (void)player_aware;

    prop::Prop* prop = prop::make(prop::Id::r_spell);

    prop->set_indefinite();

    caster->m_properties.apply(prop);
}

std::vector<std::string> SpellSpellShield::descr_specific(
    const SpellSkill skill) const
{
    (void)skill;

    std::vector<std::string> descr;

    descr.emplace_back(
        "Grants protection against harmful spells. The effect lasts "
        "until a spell is blocked.");

    return descr;
}

bool SpellSpellShield::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)seen_targets;
    (void)skill;

    return !mon.m_properties.has(prop::Id::r_spell);
}

// -----------------------------------------------------------------------------
// Haste
// -----------------------------------------------------------------------------
std::string SpellHaste::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Haste";
}

SpellId SpellHaste::id() const
{
    return SpellId::haste;
}

SpellDomain SpellHaste::domain() const
{
    return SpellDomain::time;
}

SpellShock SpellHaste::shock_type() const
{
    return SpellShock::mild;
}

bool SpellHaste::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

Range SpellHaste::duration_range(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {5, 10};
    case SpellSkill::expert:       return {10, 20};
    case SpellSkill::master:       return {15, 30};
    case SpellSkill::transcendent: return {300, 600};
    }

    ASSERT(false);

    return {1, 1};
}

int SpellHaste::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 7;
}

void SpellHaste::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    prop::Prop* prop = prop::make(prop::Id::hasted);

    prop->set_duration(duration_range(skill).roll());

    caster->m_properties.apply(prop);
}

std::vector<std::string> SpellHaste::descr_specific(const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back("The caster moves faster relative to the world around them.");

    descr.push_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

int SpellHaste::mon_cooldown() const
{
    return 20;
}

bool SpellHaste::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)skill;

    return (
        !seen_targets.empty() &&
        !mon.m_properties.has(prop::Id::hasted));
}

// -----------------------------------------------------------------------------
// Premonition
// -----------------------------------------------------------------------------
std::string SpellPremonition::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Premonition";
}

SpellId SpellPremonition::id() const
{
    return SpellId::premonition;
}

SpellDomain SpellPremonition::domain() const
{
    return SpellDomain::mind;
}

SpellShock SpellPremonition::shock_type() const
{
    return SpellShock::mild;
}

bool SpellPremonition::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

Range SpellPremonition::duration_range(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {4, 8};
    case SpellSkill::expert:       return {8, 16};
    case SpellSkill::master:       return {12, 24};
    case SpellSkill::transcendent: return {20, 40};
    }

    ASSERT(false);

    return {1, 1};
}

int SpellPremonition::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 7;
}

void SpellPremonition::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    prop::Prop* prop = prop::make(prop::Id::premonition);

    prop->set_duration(duration_range(skill).roll());

    caster->m_properties.apply(prop);
}

std::vector<std::string> SpellPremonition::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Grants foresight of attacks against the caster, "
        "making it extremely difficult for assailants to achieve a "
        "succesful hit.");

    descr.emplace_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

bool SpellPremonition::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)skill;

    return (
        !seen_targets.empty() &&
        !mon.m_properties.has(prop::Id::premonition));
}

// -----------------------------------------------------------------------------
// Erudition
// -----------------------------------------------------------------------------
std::string SpellErudition::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "an " : "";

    return a + "Erudition";
}

SpellId SpellErudition::id() const
{
    return SpellId::erudition;
}

SpellDomain SpellErudition::domain() const
{
    return SpellDomain::mind;
}

SpellShock SpellErudition::shock_type() const
{
    return SpellShock::disturbing;
}

bool SpellErudition::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

int SpellErudition::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)caster;

    return 7 - (int)skill;
}

Range SpellErudition::get_duration_range(SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {2, 4};
    case SpellSkill::expert:       return {4, 8};
    case SpellSkill::master:
    case SpellSkill::transcendent: return {6, 12};
    }

    ASSERT(false);

    return {1, 1};
}

void SpellErudition::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    {
        prop::Prop* prop = prop::make(prop::Id::erudition);

        prop->set_duration(get_duration_range(skill).roll());

        caster->m_properties.apply(prop);
    }

    if (skill == SpellSkill::transcendent) {
        auto* const prop =
            caster->m_properties.prop(prop::Id::erudition);

        if (!prop) {
            ASSERT(false);

            return;
        }

        auto* const erudition = static_cast<prop::Erudition*>(prop);

        erudition->disable_end_on_spell_cast();
    }
}

std::vector<std::string> SpellErudition::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Temporarily bestows the caster with an expanded understanding "
        "of the esoteric mechanisms behind magical practice. "
        "The caster's skill is improved by one level for all spells.");

    std::string duration_descr =
        "The spell lasts " +
        get_duration_range(skill).str() +
        " turns";

    if (skill == SpellSkill::transcendent) {
        duration_descr +=
            ". The effect does not end when casting spells, "
            "only when the duration expires.";
    }
    else {
        duration_descr +=
            ", or until a spell is cast (either from a Manuscript "
            "or from memory).";
    }

    descr.push_back(duration_descr);

    return descr;
}

// -----------------------------------------------------------------------------
// Identify
// -----------------------------------------------------------------------------
std::string SpellIdentify::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "an " : "";

    return a + "Identify";
}

SpellId SpellIdentify::id() const
{
    return SpellId::identify;
}

SpellDomain SpellIdentify::domain() const
{
    return SpellDomain::mind;
}

bool SpellIdentify::is_tenebrous() const
{
    return true;
}

SpellShock SpellIdentify::shock_type() const
{
    return SpellShock::disturbing;
}

bool SpellIdentify::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

int SpellIdentify::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 7;
}

void SpellIdentify::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    msg_log::add(common_text::g_player_magic_identify);

    std::vector<ItemType> item_types_allowed;

    if (skill != SpellSkill::master) {
        item_types_allowed.push_back(ItemType::scroll);

        if (skill == SpellSkill::expert) {
            item_types_allowed.push_back(ItemType::potion);
        }
    }

    if (skill == SpellSkill::transcendent) {
        // Immediately identify all items.
        for (item::Item* const item : caster->m_inv.all_items()) {
            item->identify(Verbose::yes);
        }
    }
    else {
        // Run identify selection menu to select one item.
        auto state = std::make_unique<SelectIdentify>(item_types_allowed);

        states::push(std::move(state));
    }

    msg_log::more_prompt();
}

std::vector<std::string> SpellIdentify::descr_specific(
    const SpellSkill skill) const
{
    if (skill == SpellSkill::transcendent) {
        return {"Immediately identifies all carried items."};
    }

    std::vector<std::string> descr;

    descr.emplace_back("Identifies one carried item.");

    std::string identifies_str = "The spell can identify ";

    switch (skill) {
    case SpellSkill::basic:  identifies_str += "Manuscripts"; break;
    case SpellSkill::expert: identifies_str += "Manuscripts and Potions"; break;
    case SpellSkill::master: identifies_str += "all items"; break;

    case SpellSkill::transcendent:
        ASSERT(false);
        break;
    }

    identifies_str += ".";

    descr.push_back(identifies_str);

    return descr;
}

// -----------------------------------------------------------------------------
// Teleport
// -----------------------------------------------------------------------------
int SpellTeleport::mon_cooldown() const
{
    return 20;
}

std::string SpellTeleport::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Teleport";
}

SpellId SpellTeleport::id() const
{
    return SpellId::teleport;
}

SpellDomain SpellTeleport::domain() const
{
    return SpellDomain::time;
}

SpellShock SpellTeleport::shock_type() const
{
    return SpellShock::disturbing;
}

int SpellTeleport::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 8;
}

bool SpellTeleport::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

int SpellTeleport::max_dist(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return 5;
    case SpellSkill::expert:       return 10;
    case SpellSkill::master:
    case SpellSkill::transcendent: return 15;
    }

    ASSERT(false);
    return -1;
}

int SpellTeleport::invis_duration(const SpellSkill skill) const
{
    return ((skill == SpellSkill::transcendent) ? 6 : 3);
}

void SpellTeleport::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    if (skill >= SpellSkill::master) {
        auto* const invis = prop::make(prop::Id::invis);

        invis->set_duration(invis_duration(skill));

        caster->m_properties.apply(invis);
    }

    const int max_d = max_dist(skill);

    teleport(*caster, ShouldCtrlTele::if_tele_ctrl_prop, max_d);
}

bool SpellTeleport::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)skill;

    const bool is_low_hp = (mon.m_hp <= (actor::max_hp(mon) / 2));

    return !seen_targets.empty() && is_low_hp && rnd::fraction(3, 4);
}

std::vector<std::string> SpellTeleport::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back("Instantly moves the caster to a different position.");

    descr.emplace_back(
        "Maximum teleport distance is " +
        std::to_string(max_dist(skill)) +
        ".");

    if (skill >= SpellSkill::master) {
        descr.push_back(
            "On teleporting, the caster is invisible for " +
            std::to_string(invis_duration(skill)) +
            " turns.");
    }

    return descr;
}

// -----------------------------------------------------------------------------
// Expulsion
// -----------------------------------------------------------------------------
SpellId SpellExpulsion::id() const
{
    return SpellId::expulsion;
}

SpellDomain SpellExpulsion::domain() const
{
    return SpellDomain::time;
}

SpellShock SpellExpulsion::shock_type() const
{
    return SpellShock::mild;
}

bool SpellExpulsion::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

std::string SpellExpulsion::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "an " : "";

    return a + "Expulsion";
}

int SpellExpulsion::max_dist(SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return 8;
    case SpellSkill::expert:       return 14;
    case SpellSkill::master:       return 20;
    case SpellSkill::transcendent: return -1;
    }

    ASSERT(false);
    return -1;
}

int SpellExpulsion::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)caster;
    (void)skill;

    return 7;
}

void SpellExpulsion::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    if (seen_targets.empty()) {
        if (actor::is_player(caster)) {
            msg_log::add("A momentary void opens and closes.");
        }

        return;
    }

    // There are targets available

    const std::vector<actor::Actor*> targets =
        (skill == SpellSkill::transcendent)
        ? seen_targets
        : std::vector {rnd::element(seen_targets)};

    if ((player_aware == PlayerAwareOfCast::yes) && is_player_caster_or_target(caster, targets)) {
        draw_blast_at_seen_actors(targets, colors::gray());
    }

    for (actor::Actor* const target : targets) {
        if (target->m_properties.has(prop::Id::r_spell)) {
            on_resist(*target, caster);
            continue;
        }

        teleport(*target, ShouldCtrlTele::never, max_dist(skill));

        if (!actor::is_player(target)) {
            target->m_mon_aware_state.aware_counter = 0;
            target->m_mon_aware_state.wary_counter = 0;
        }
    }
}

std::vector<std::string> SpellExpulsion::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    if (skill == SpellSkill::transcendent) {
        descr.emplace_back("All visible hostile creatures are teleported away.");
    }
    else {
        descr.emplace_back("One random visible hostile creature is teleported away.");
    }

    descr.emplace_back("Max distance is " + std::to_string(max_dist(skill)) + " steps.");

    descr.emplace_back("The teleportation is forced; the target can never control it.");

    return descr;
}

int SpellExpulsion::mon_cooldown() const
{
    return 30;
}

bool SpellExpulsion::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)skill;

    const bool is_low_hp = (mon.m_hp <= (actor::max_hp(mon) / 3));

    return !seen_targets.empty() && is_low_hp && rnd::coin_toss();
}

// -----------------------------------------------------------------------------
// Knockback
// -----------------------------------------------------------------------------
int SpellKnockBack::mon_cooldown() const
{
    return 5;
}

std::string SpellKnockBack::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Push";
}

SpellId SpellKnockBack::id() const
{
    return SpellId::knockback;
}

SpellDomain SpellKnockBack::domain() const
{
    return SpellDomain::END;
}

SpellShock SpellKnockBack::shock_type() const
{
    return SpellShock::mild;
}

std::vector<std::string> SpellKnockBack::descr_specific(const SpellSkill skill) const
{
    (void)skill;

    return {};
}

int SpellKnockBack::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 8;
}

bool SpellKnockBack::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

void SpellKnockBack::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)skill;
    (void)player_aware;

    actor::Actor* target = map::random_closest_actor(caster->m_pos, seen_targets);

    if (!target) {
        ASSERT(false);

        return;
    }

    if (target->m_properties.has(prop::Id::r_spell)) {
        on_resist(*target, caster);
        return;
    }

    std::string target_str;
    Color msg_clr;

    if (actor::is_player(target)) {
        target_str = "me";

        msg_clr = colors::msg_bad();
    }
    else {
        // Target is monster
        target_str = actor::name_the(*target);

        msg_clr = map::g_player->is_leader_of(target) ? colors::white() : colors::msg_good();
    }

    if (actor::can_player_see_actor(*target)) {
        msg_log::add("A force pushes " + target_str + "!", msg_clr);
    }

    knockback::run(
        *target,
        caster->m_pos,
        knockback::KnockbackSource::other);

    if (!actor::is_player(target)) {
        target->become_aware_player(actor::AwareSource::spell_victim);
    }
}

bool SpellKnockBack::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

// -----------------------------------------------------------------------------
// Curse
// -----------------------------------------------------------------------------
int SpellCurse::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 3;
}

std::string SpellCurse::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Curse";
}

SpellId SpellCurse::id() const
{
    return SpellId::curse;
}

SpellDomain SpellCurse::domain() const
{
    return SpellDomain::corruption;
}

SpellShock SpellCurse::shock_type() const
{
    return SpellShock::mild;
}

int SpellCurse::mon_cooldown() const
{
    return 10;
}

bool SpellCurse::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

Range SpellCurse::duration_range(const SpellSkill skill) const
{
    Range duration_range;
    duration_range.min = 15 * ((int)skill + 1);
    duration_range.max = duration_range.min * 2;

    return duration_range;
}

int SpellCurse::pct_chance_doom(SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:  return 5;
    case SpellSkill::expert: return 10;
    case SpellSkill::master:
    case SpellSkill::transcendent:
        // Not applicable.
        break;
    }

    ASSERT(false);

    return 0;
}

void SpellCurse::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    const int duration = duration_range(skill).roll();

    if (seen_targets.empty()) {
        if (actor::is_player(caster)) {
            msg_log::add("The air feels accursed.");
        }

        return;
    }

    // There are targets available

    const std::vector<actor::Actor*> targets =
        (skill == SpellSkill::basic)
        ? std::vector {rnd::element(seen_targets)}
        : seen_targets;

    auto prop_id = prop::Id::cursed;
    auto sfx_id = audio::SfxId::curse_spell;

    if ((skill >= SpellSkill::master) || rnd::percent(pct_chance_doom(skill))) {
        prop_id = prop::Id::doomed;
        sfx_id = audio::SfxId::doom_spell;
    }

    if ((player_aware == PlayerAwareOfCast::yes) && is_player_caster_or_target(caster, targets)) {
        audio::play(sfx_id);

        draw_blast_at_seen_actors(targets, colors::magenta());
    }

    for (actor::Actor* const target : targets) {
        if (target->m_properties.has(prop::Id::r_spell)) {
            on_resist(*target, caster);
            continue;
        }

        prop::Prop* const prop = prop::make(prop_id);

        prop->set_duration(duration);

        target->m_properties.apply(prop);
    }
}

std::vector<std::string> SpellCurse::descr_specific(SpellSkill skill) const
{
    std::vector<std::string> descr;

    const bool is_below_master = skill < SpellSkill::master;

    const prop::PropData& cursed_data = prop::g_data[(size_t)prop::Id::cursed];
    const prop::PropData& doomed_data = prop::g_data[(size_t)prop::Id::doomed];

    const prop::PropData& main_prop_data = is_below_master ? cursed_data : doomed_data;

    descr.emplace_back(
        "The spell's victims are " +
        text_format::first_to_lower(main_prop_data.name) +
        " (" +
        main_prop_data.descr +
        ")");

    if (is_below_master) {
        descr.emplace_back(
            "With " +
            std::to_string(pct_chance_doom(skill)) +
            "% chance, the victims instead become " +
            text_format::first_to_lower(doomed_data.name) +
            " (" +
            doomed_data.descr +
            ")");
    }

    descr.emplace_back(s_not_alerting_mon_descr);

    descr.emplace_back(
        skill == SpellSkill::basic
            ? "Affects one random visible hostile creature."
            : "Affects all visible hostile creatures.");

    descr.emplace_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

bool SpellCurse::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

// -----------------------------------------------------------------------------
// Poison
// -----------------------------------------------------------------------------
int SpellPoison::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 5;
}

std::string SpellPoison::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Poison";
}

SpellId SpellPoison::id() const
{
    return SpellId::poison;
}

SpellDomain SpellPoison::domain() const
{
    return SpellDomain::corruption;
}

SpellShock SpellPoison::shock_type() const
{
    return SpellShock::mild;
}

int SpellPoison::mon_cooldown() const
{
    return 6;
}

bool SpellPoison::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

Range SpellPoison::duration_range(const SpellSkill skill) const
{
    Range duration_range;
    duration_range.min = 15 * ((int)skill + 1);
    duration_range.max = duration_range.min * 2;

    return duration_range;
}

void SpellPoison::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    const int duration = duration_range(skill).roll();

    if (seen_targets.empty()) {
        if (actor::is_player(caster)) {
            msg_log::add("The air feels noxious.");
        }
        return;
    }

    // There are targets available

    const std::vector<actor::Actor*> targets =
        (skill == SpellSkill::basic)
        ? std::vector {rnd::element(seen_targets)}
        : seen_targets;

    if ((player_aware == PlayerAwareOfCast::yes) && is_player_caster_or_target(caster, targets)) {
        audio::play(audio::SfxId::poison_spell);

        draw_blast_at_seen_actors(targets, colors::light_green());
    }

    for (actor::Actor* const target : targets) {
        if (target->m_properties.has(prop::Id::r_spell)) {
            on_resist(*target, caster);
            continue;
        }

        auto id = prop::Id::poisoned;

        prop::Prop* const prop = prop::make(id);

        prop->set_duration(duration);

        target->m_properties.apply(prop);

        if (!actor::is_player(target)) {
            target->become_aware_player(actor::AwareSource::spell_victim);
        }
    }
}

std::vector<std::string> SpellPoison::descr_specific(SpellSkill skill) const
{
    std::vector<std::string> descr;

    const prop::PropData& prop_data = prop::g_data[(size_t)prop::Id::poisoned];

    descr.emplace_back(
        "The spell's victims are " +
        text_format::first_to_lower(prop_data.name) +
        " (" +
        prop_data.descr +
        ")");

    descr.emplace_back(
        skill == SpellSkill::basic
            ? "Affects one random visible hostile creature."
            : "Affects all visible hostile creatures.");

    descr.push_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

bool SpellPoison::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

// -----------------------------------------------------------------------------
// Heal Others
// -----------------------------------------------------------------------------
int SpellHealOthers::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 6;
}

std::string SpellHealOthers::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Heal Others";
}

SpellId SpellHealOthers::id() const
{
    return SpellId::heal_others;
}

SpellDomain SpellHealOthers::domain() const
{
    return SpellDomain::END;
}

SpellShock SpellHealOthers::shock_type() const
{
    return SpellShock::mild;
}

std::vector<std::string> SpellHealOthers::descr_specific(SpellSkill skill) const
{
    (void)skill;
    return {};
}

bool SpellHealOthers::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

int SpellHealOthers::mon_cooldown() const
{
    return 10;
}

std::vector<actor::Actor*> SpellHealOthers::find_possible_actors_to_heal(
    const actor::Actor* const caster) const
{
    const std::vector<actor::Actor*> allies = actor::other_allied_actors(caster);

    Array2<bool> blocks_los(map::dims());

    const R r = fov::fov_rect(caster->m_pos, blocks_los.dims());

    map_parsers::BlocksLos().run(blocks_los, r, MapParseMode::overwrite);

    std::vector<actor::Actor*> actors_to_heal;

    std::copy_if(
        std::begin(allies),
        std::end(allies),
        std::back_inserter(actors_to_heal),
        [caster, blocks_los](const actor::Actor* const actor) {
            if (!actor::is_alive(*actor)) {
                return false;
            }

            if (!can_mon_see_actor(*caster, *actor, blocks_los)) {
                return false;
            }

            const bool is_healing_needed = (actor->m_hp < ((actor::max_hp(*actor) * 3) / 4));

            if (!is_healing_needed) {
                return false;
            }

            return true;
        });

    return actors_to_heal;
}

actor::Actor* SpellHealOthers::find_random_actor_to_heal(
    const actor::Actor* caster) const
{
    const std::vector<actor::Actor*> actors = find_possible_actors_to_heal(caster);

    if (actors.empty()) {
        ASSERT(false);

        return nullptr;
    }
    else {
        return rnd::element(actors);
    }
}

void SpellHealOthers::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    const int hp_healed = 8 + (int)skill * 4;

    actor::Actor* const actor_to_heal = find_random_actor_to_heal(caster);

    if (!actor_to_heal) {
        ASSERT(false);

        return;
    }

    actor::restore_hp(*actor_to_heal, hp_healed);
}

bool SpellHealOthers::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)seen_targets;
    (void)skill;

    return !find_possible_actors_to_heal(&mon).empty();
}

// -----------------------------------------------------------------------------
// Enfeeble
// -----------------------------------------------------------------------------
Range SpellEnfeeble::duration_range(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {8, 12};
    case SpellSkill::expert:       return {10, 16};
    case SpellSkill::master:       return {12, 20};
    case SpellSkill::transcendent: return {30, 50};
    }

    ASSERT(false);

    return {1, 1};
}

SpellId SpellEnfeeble::id() const
{
    return SpellId::enfeeble;
}

SpellDomain SpellEnfeeble::domain() const
{
    return SpellDomain::corruption;
}

SpellShock SpellEnfeeble::shock_type() const
{
    return SpellShock::mild;
}

bool SpellEnfeeble::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

std::string SpellEnfeeble::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "an " : "";

    return a + "Enfeeble";
}

int SpellEnfeeble::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 5;
}

int SpellEnfeeble::mon_cooldown() const
{
    return 5;
}

void SpellEnfeeble::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    const int duration = duration_range(skill).roll();

    if (seen_targets.empty()) {
        if (actor::is_player(caster)) {
            msg_log::add("The bugs on the ground suddenly move very feebly.");
        }

        return;
    }

    // There are targets available

    const std::vector<actor::Actor*> targets =
        (skill == SpellSkill::basic)
        ? std::vector {rnd::element(seen_targets)}
        : seen_targets;

    if ((player_aware == PlayerAwareOfCast::yes) && is_player_caster_or_target(caster, targets)) {
        draw_blast_at_seen_actors(targets, colors::magenta());
    }

    for (actor::Actor* const target : targets) {
        if (target->m_properties.has(prop::Id::r_spell)) {
            on_resist(*target, caster);
            continue;
        }

        prop::Prop* const prop = prop::make(prop::Id::weakened);

        prop->set_duration(duration);

        target->m_properties.apply(prop);
    }
}

std::vector<std::string> SpellEnfeeble::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Physically enfeebles the spell's victims, causing them to "
        "only do half damage in melee combat.");

    descr.emplace_back(s_not_alerting_mon_descr);

    descr.emplace_back(
        skill == SpellSkill::basic
            ? "Affects one random visible hostile creature."
            : "Affects all visible hostile creatures.");

    descr.push_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

bool SpellEnfeeble::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

// -----------------------------------------------------------------------------
// Temporal Echo
// -----------------------------------------------------------------------------
int SpellTemporalEcho::pct_damage_dealt(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return 75;
    case SpellSkill::expert:       return 100;
    case SpellSkill::master:       return 125;
    case SpellSkill::transcendent: return 200;
    }

    ASSERT(false);

    return 100;
}

Range SpellTemporalEcho::duration_range() const
{
    return {6, 8};
}

SpellId SpellTemporalEcho::id() const
{
    return SpellId::temporal_echo;
}

SpellDomain SpellTemporalEcho::domain() const
{
    return SpellDomain::time;
}

SpellShock SpellTemporalEcho::shock_type() const
{
    return SpellShock::mild;
}

bool SpellTemporalEcho::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

std::string SpellTemporalEcho::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Temporal Echo";
}

int SpellTemporalEcho::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 4;
}

int SpellTemporalEcho::mon_cooldown() const
{
    return 10;
}

void SpellTemporalEcho::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    if (seen_targets.empty()) {
        if (actor::is_player(caster)) {
            msg_log::add("There is a faint stutter in time.");
        }

        return;
    }

    // There are targets available

    if ((player_aware == PlayerAwareOfCast::yes) &&
        is_player_caster_or_target(caster, seen_targets)) {
        draw_blast_at_seen_actors(seen_targets, colors::magenta());
    }

    const int duration = duration_range().roll();

    for (actor::Actor* const target : seen_targets) {
        if (target->m_properties.has(prop::Id::r_spell)) {
            on_resist(*target, caster);
            continue;
        }

        apply_temporal_echo_effect(*target, skill, duration);
    }
}

void SpellTemporalEcho::apply_temporal_echo_effect(
    actor::Actor& target,
    const SpellSkill skill,
    const int duration) const
{
    prop::Prop* const temporal_echo = prop::make(prop::Id::temporal_echo);

    temporal_echo->set_duration(duration);

    const int pct_dmg = pct_damage_dealt(skill);

    static_cast<prop::TemporalEcho*>(temporal_echo)->set_percent_damage_dealt(pct_dmg);

    target.m_properties.apply(temporal_echo);
}

std::vector<std::string> SpellTemporalEcho::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr = {
        "For all visible enemies, time is manipulated so that damage taken during a "
        "brief period will recur when the effect ends."};

    descr.push_back(
        "The effect lasts for " +
        duration_range().str() +
        " turns (their turns). " +
        std::to_string(pct_damage_dealt(skill)) +
        "% of the damage taken during the effect is dealt again.");

    return descr;
}

bool SpellTemporalEcho::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

// -----------------------------------------------------------------------------
// Slow
// -----------------------------------------------------------------------------
std::string SpellSlow::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Slow";
}

SpellId SpellSlow::id() const
{
    return SpellId::slow;
}

SpellDomain SpellSlow::domain() const
{
    return SpellDomain::time;
}

SpellShock SpellSlow::shock_type() const
{
    return SpellShock::mild;
}

bool SpellSlow::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

Range SpellSlow::duration_range(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {5, 10};
    case SpellSkill::expert:       return {7, 12};
    case SpellSkill::master:       return {9, 14};
    case SpellSkill::transcendent: return {30, 50};
    }

    ASSERT(false);

    return {1, 1};
}

int SpellSlow::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 5;
}

void SpellSlow::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    const int duration = duration_range(skill).roll();

    if (seen_targets.empty()) {
        if (actor::is_player(caster)) {
            msg_log::add("The bugs on the ground suddenly move very slowly.");
        }

        return;
    }

    // There are targets available

    const std::vector<actor::Actor*> targets =
        (skill == SpellSkill::basic)
        ? std::vector {rnd::element(seen_targets)}
        : seen_targets;

    if ((player_aware == PlayerAwareOfCast::yes) && is_player_caster_or_target(caster, targets)) {
        draw_blast_at_seen_actors(targets, colors::light_green());
    }

    for (actor::Actor* const target : targets) {
        if (target->m_properties.has(prop::Id::r_spell)) {
            on_resist(*target, caster);
            continue;
        }

        auto* const prop = prop::make(prop::Id::slowed);

        prop->set_duration(duration);

        target->m_properties.apply(prop);
    }
}

std::vector<std::string> SpellSlow::descr_specific(
    const SpellSkill skill) const
{
    (void)skill;

    std::vector<std::string> descr;

    descr.emplace_back("Causes the spell's victims to move more slowly.");

    descr.emplace_back(s_not_alerting_mon_descr);

    descr.emplace_back(
        skill == SpellSkill::basic
            ? "Affects one random visible hostile creature."
            : "Affects all visible hostile creatures.");

    descr.push_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

int SpellSlow::mon_cooldown() const
{
    return 20;
}

bool SpellSlow::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

// -----------------------------------------------------------------------------
// Terrify
// -----------------------------------------------------------------------------
std::string SpellTerrify::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Terrify";
}

SpellId SpellTerrify::id() const
{
    return SpellId::terrify;
}

SpellDomain SpellTerrify::domain() const
{
    return SpellDomain::illusion;
}

SpellShock SpellTerrify::shock_type() const
{
    return SpellShock::mild;
}

bool SpellTerrify::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

Range SpellTerrify::duration_range(SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {6, 12};
    case SpellSkill::expert:       return {12, 24};
    case SpellSkill::master:       return {18, 36};
    case SpellSkill::transcendent: return {24, 48};
    }

    ASSERT(false);

    return {1, 1};
}

int SpellTerrify::faint_pct_chance(const SpellSkill skill) const
{
    if (skill == SpellSkill::transcendent) {
        return 100;
    }
    else {
        return 30 + ((int)skill * 20);
    }
}

Range SpellTerrify::faint_duration_range() const
{
    return {2, 4};
}

int SpellTerrify::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 5;
}

int SpellTerrify::mon_cooldown() const
{
    return 5;
}

void SpellTerrify::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    if (seen_targets.empty()) {
        if (actor::is_player(caster)) {
            msg_log::add("The bugs on the ground suddenly scatter away.");
        }

        return;
    }

    // There are targets available

    const std::vector<actor::Actor*> targets =
        (skill == SpellSkill::basic)
        ? std::vector {rnd::element(seen_targets)}
        : seen_targets;

    if ((player_aware == PlayerAwareOfCast::yes) && is_player_caster_or_target(caster, targets)) {
        draw_blast_at_seen_actors(targets, colors::magenta());
    }

    for (actor::Actor* const target : targets) {
        if (target->m_properties.has(prop::Id::r_spell)) {
            on_resist(*target, caster);
            continue;
        }

        terrify_target(*target, skill);

        // NOTE: Since this fainting is supposed to be a side effect of the creature becoming
        // terrified by a spell, it would look weird if they "reisted" the sleep due to sleep
        // resistance. Therefore only try to apply the property if they are known to not have such
        // resistance. Any other sources of resisting sleep would probably be fine, but not
        // explicitly sleep resistance.
        if (target->m_properties.has(prop::Id::terrified) &&
            !target->m_properties.has(prop::Id::r_sleep) &&
            rnd::percent(faint_pct_chance(skill))) {
            faint_target(*target);
        }
    }
}

void SpellTerrify::terrify_target(actor::Actor& target, const SpellSkill skill) const
{
    prop::Prop* const terrified = prop::make(prop::Id::terrified);

    terrified->set_duration(duration_range(skill).roll());

    target.m_properties.apply(terrified);
}

void SpellTerrify::faint_target(actor::Actor& target) const
{
    prop::Prop* const fainted = prop::make(prop::Id::fainted);

    fainted->set_duration(faint_duration_range().roll());

    target.m_properties.apply(fainted);
}

std::vector<std::string> SpellTerrify::descr_specific(
    const SpellSkill skill) const
{
    (void)skill;

    std::vector<std::string> descr;

    descr.emplace_back("Inflicts a nightmare illusion that overwhelms its victims with dread.");

    descr.emplace_back(
        skill == SpellSkill::basic
            ? "Affects one random visible hostile creature."
            : "Affects all visible hostile creatures.");

    descr.push_back("The spell lasts " + duration_range(skill).str() + " turns.");

    if (skill == SpellSkill::transcendent) {
        descr.emplace_back("Affected creatures also faint.");
    }
    else {
        const std::string creature_str = (skill == SpellSkill::basic) ? "creature" : "creatures";

        descr.emplace_back(
            "Has a " +
            std::to_string(faint_pct_chance(skill)) +
            "% chance to also make affected " +
            creature_str +
            " faint.");
    }

    return descr;
}

bool SpellTerrify::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

// -----------------------------------------------------------------------------
// Threat Projection
// -----------------------------------------------------------------------------
std::string SpellThreatProjection::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Threat Projection";
}

SpellId SpellThreatProjection::id() const
{
    return SpellId::threat_projection;
}

SpellDomain SpellThreatProjection::domain() const
{
    return SpellDomain::illusion;
}

SpellShock SpellThreatProjection::shock_type() const
{
    return SpellShock::mild;
}

bool SpellThreatProjection::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

Range SpellThreatProjection::duration_range(SpellSkill skill) const
{
    switch (skill) {
    // NOTE: Same duration as Horn of Malice (but only on one seen monster).
    case SpellSkill::basic:        return {6, 12};
    case SpellSkill::expert:       return {12, 24};
    case SpellSkill::master:       return {18, 36};
    // NOTE: Transcendent level applies the effect on all seen monsters.
    case SpellSkill::transcendent: return {6, 12};
    }

    ASSERT(false);

    return {1, 1};
}

int SpellThreatProjection::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 5;
}

void SpellThreatProjection::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    if (seen_targets.empty()) {
        if (actor::is_player(caster)) {
            msg_log::add("The bugs on the ground all start to attack each other.");
        }

        return;
    }

    // There are targets available

    const std::vector<actor::Actor*> targets =
        (skill == SpellSkill::transcendent)
        ? seen_targets
        : std::vector {rnd::element(seen_targets)};

    if ((player_aware == PlayerAwareOfCast::yes) && is_player_caster_or_target(caster, targets)) {
        draw_blast_at_seen_actors(targets, colors::magenta());
    }

    for (actor::Actor* const target : targets) {
        if (target->m_properties.has(prop::Id::r_spell)) {
            on_resist(*target, caster);
            continue;
        }

        conflict_target(*target, skill);
    }
}

void SpellThreatProjection::conflict_target(actor::Actor& target, const SpellSkill skill) const
{
    prop::Prop* const conflicted = prop::make(prop::Id::conflict);

    conflicted->set_duration(duration_range(skill).roll());

    target.m_properties.apply(conflicted);
}

std::vector<std::string> SpellThreatProjection::descr_specific(
    const SpellSkill skill) const
{
    (void)skill;

    std::vector<std::string> descr;

    descr.emplace_back(
        "Distorts the perception of the spell's victims, causing "
        "all other creatures to be misidentified as enemies.");

    descr.emplace_back(
        (skill == SpellSkill::transcendent)
            ? "Affects all visible hostile creatures."
            : "Affects one random visible hostile creature.");

    descr.push_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

// -----------------------------------------------------------------------------
// Blind
// -----------------------------------------------------------------------------
std::string SpellBlind::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Blind";
}

SpellId SpellBlind::id() const
{
    return SpellId::blind;
}

SpellDomain SpellBlind::domain() const
{
    return SpellDomain::END;
}

SpellShock SpellBlind::shock_type() const
{
    return SpellShock::mild;
}

std::vector<std::string> SpellBlind::descr_specific(const SpellSkill skill) const
{
    (void)skill;

    return {};
}

int SpellBlind::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 7;
}

bool SpellBlind::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

int SpellBlind::mon_cooldown() const
{
    return 20;
}

void SpellBlind::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    const std::vector<actor::Actor*> seen_targets_not_blind_resistant =
        find_actors_not_blind_resistant(seen_targets);

    actor::Actor* target =
        map::random_closest_actor(
            caster->m_pos,
            seen_targets_not_blind_resistant);

    if (!target) {
        return;
    }

    if (target->m_properties.has(prop::Id::r_spell)) {
        on_resist(*target, caster);
        return;
    }

    if (actor::is_player(target)) {
        msg_log::add("Scales grow over my eyes!");
    }
    else if (actor::can_player_see_actor(*target)) {
        const std::string actor_name = actor::name_the(*target);

        msg_log::add("Scales grow over the eyes of " + actor_name + ".");
    }

    prop::Prop* prop = prop::make(prop::Id::blind);

    prop->set_duration(3 + (int)skill);

    target->m_properties.apply(prop);
}

bool SpellBlind::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !find_actors_not_blind_resistant(seen_targets).empty();
}

std::vector<actor::Actor*> SpellBlind::find_actors_not_blind_resistant(
    const std::vector<actor::Actor*>& actors) const
{
    std::vector<actor::Actor*> result;

    result.reserve(actors.size());

    std::copy_if(
        std::begin(actors),
        std::end(actors),
        std::back_inserter(result),
        [](const actor::Actor* const actor) {
            return !actor->m_properties.has(prop::Id::r_blind);
        });

    return result;
}

// -----------------------------------------------------------------------------
// Summon spells
// -----------------------------------------------------------------------------
int SpellSummon::mon_cooldown() const
{
    return 8;
}

std::string SpellSummon::name(const SpellNameType name_type) const
{
    (void)name_type;

    return "";
}

SpellId SpellSummon::id() const
{
    return m_impl->id();
}

SpellDomain SpellSummon::domain() const
{
    return SpellDomain::END;
}

SpellShock SpellSummon::shock_type() const
{
    return SpellShock::disturbing;
}

std::vector<std::string> SpellSummon::descr_specific(const SpellSkill skill) const
{
    (void)skill;

    return {};
}

int SpellSummon::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 6;
}

bool SpellSummon::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

void SpellSummon::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    Range mon_lvl_range = get_allowed_mon_lvl_range(skill);

    TRACE
        << "Allowed monster level range: "
        << "'" << mon_lvl_range.str() << "'"
        << "\n";

    std::vector<std::string> summon_bucket = make_summon_bucket(mon_lvl_range);

    if (summon_bucket.empty()) {
        TRACE
            << "No eligible monsters found, trying again with monsters allowed from depth 0."
            << "\n";

        mon_lvl_range.min = 0;

        TRACE
            << "Allowed monster dungeon level range: "
            << "'" << mon_lvl_range.str() << "'"
            << "\n";

        summon_bucket = make_summon_bucket(mon_lvl_range);
    }

    if (summon_bucket.empty()) {
        TRACE << "No elligible monsters found for spawning" << "\n";

        ASSERT(false);

        return;
    }

    const auto id = rnd::element(summon_bucket);

    summon(id, caster);
}

Range SpellSummon::get_allowed_mon_lvl_range(const SpellSkill skill) const
{
    Range dlvl_range;

    switch (skill) {
    case SpellSkill::basic:
        dlvl_range.min = 0;
        dlvl_range.max = g_dlvl_last_early_game;
        break;

    case SpellSkill::expert:
        dlvl_range.min = 0;
        dlvl_range.max = g_dlvl_last_mid_game;
        break;

    case SpellSkill::master:
    case SpellSkill::transcendent:
        dlvl_range.min = g_dlvl_first_mid_game;
        dlvl_range.max = g_dlvl_last;
        break;
    }

    // Cap min and max to current dungeon level + 2
    const int dlvl = map::g_dlvl + 2;

    dlvl_range.min = std::min(dlvl_range.min, dlvl);
    dlvl_range.max = std::min(dlvl_range.max, dlvl);

    return dlvl_range;
}

std::vector<std::string> SpellSummon::make_summon_bucket(const Range& lvl_range) const
{
    std::vector<std::string> summon_bucket;

    for (auto& it : actor::g_data) {
        const actor::ActorData& data = it.second;

        if (!data.can_be_summoned_by_mon) {
            continue;
        }

        // NOTE: The "min" dungeon level in the monster data is used here as a general
        // "strength" of the monster. The "max" dungeon level is not considered.
        const int mon_lvl = data.spawn_min_dlvl;

        if (!lvl_range.is_in_range(mon_lvl)) {
            continue;
        }

        summon_bucket.push_back(data.id);
    }

    TRACE
        << "Number of monsters allowed before specific filtering: "
        << "'" << summon_bucket.size() << "'"
        << "\n";

    summon_bucket = m_impl->filter_allowed_ids(summon_bucket);

    TRACE
        << "Number of monsters allowed after specific filtering: "
        << "'" << summon_bucket.size() << "'"
        << "\n";

    return summon_bucket;
}

void SpellSummon::summon(const std::string& id, actor::Actor* caster) const
{
    actor::Actor* const caster_leader = caster->m_leader;

    actor::Actor* const leader = caster_leader ? caster_leader : caster;

    const actor::MonSpawnResult summoned =
        actor::spawn(caster->m_pos, {id})
            .make_aware_of_player()
            .set_leader(leader);

    std::for_each(
        std::begin(summoned.monsters),
        std::end(summoned.monsters),
        [](auto* const mon) {
            mon->m_properties.apply(prop::make(prop::Id::summoned));

            prop::Prop* prop_waiting = prop::make(prop::Id::waiting);

            prop_waiting->set_duration(2);

            mon->m_properties.apply(prop_waiting);
        });

    if (summoned.monsters.empty()) {
        return;
    }

    actor::Actor* const mon = summoned.monsters[0];

    if (actor::can_player_see_actor(*mon)) {
        std::string appear_msg = m_impl->appear_msg_override();

        if (appear_msg.empty()) {
            const std::string mon_name_a =
                text_format::first_to_upper(
                    actor::name_a(*mon));

            appear_msg = mon_name_a + " appears!";
        }

        msg_log::add(appear_msg);

        actor::make_player_aware_mon(*mon);
    }
}

bool SpellSummon::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)skill;

    // Always allow casting with a visible target.
    if (!seen_targets.empty()) {
        return true;
    }

    // Sometimes allow casting if monster has an unseen target.
    if (mon.m_ai_state.target && rnd::one_in(30)) {
        return true;
    }

    return false;
}

SpellId SummonRandom::id() const
{
    return SpellId::summon_random;
}

int SummonImpl::mon_cooldown() const
{
    return 8;
}

std::string SummonImpl::appear_msg_override() const
{
    return "";
}

std::vector<std::string> SummonRandom::filter_allowed_ids(
    const std::vector<std::string>& summon_bucket) const
{
    // No specific filtering.
    return summon_bucket;
}

SpellId SummonWaterCreature::id() const
{
    return SpellId::summon_water_creature;
}

std::vector<std::string> SummonWaterCreature::filter_allowed_ids(
    const std::vector<std::string>& summon_bucket) const
{
    // Return all creatures with the "water creature" property.
    std::vector<std::string> result;

    std::copy_if(
        std::cbegin(summon_bucket),
        std::cend(summon_bucket),
        std::back_inserter(result),
        [](const std::string& id) {
            const actor::ActorData& data = actor::g_data.at(id);

            return data.natural_props[(size_t)prop::Id::water_creature];
        });

    return result;
}

SpellId SummonTentacles::id() const
{
    return SpellId::summon_tentacles;
}

int SummonTentacles::mon_cooldown() const
{
    return 3;
}

std::vector<std::string> SummonTentacles::filter_allowed_ids(
    const std::vector<std::string>& summon_bucket) const
{
    (void)summon_bucket;

    return {"MON_TENTACLE_CLUSTER"};
}

std::string SummonTentacles::appear_msg_override() const
{
    return "Monstrous tentacles rise up from the ground!";
}

// -----------------------------------------------------------------------------
// Heal
// -----------------------------------------------------------------------------
int SpellHeal::mon_cooldown() const
{
    return 6;
}

std::string SpellHeal::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Healing";
}

SpellId SpellHeal::id() const
{
    return SpellId::heal;
}

SpellDomain SpellHeal::domain() const
{
    return SpellDomain::warding;
}

SpellShock SpellHeal::shock_type() const
{
    return SpellShock::mild;
}

bool SpellHeal::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

int SpellHeal::nr_hp_restored(SpellSkill skill) const
{
    return 8 + (int)skill * 4;
}

Range SpellHeal::regen_duration() const
{
    return {50, 100};
}

int SpellHeal::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 6;
}

void SpellHeal::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    std::vector<prop::Id> props_can_heal;

    if ((int)skill >= (int)SpellSkill::expert) {
        props_can_heal.push_back(prop::Id::weakened);
        props_can_heal.push_back(prop::Id::poisoned);
    }

    if (skill >= SpellSkill::master) {
        props_can_heal.push_back(prop::Id::infected);
        props_can_heal.push_back(prop::Id::diseased);
        props_can_heal.push_back(prop::Id::blind);
        props_can_heal.push_back(prop::Id::deaf);
    }

    bool is_noticable = false;

    for (prop::Id prop_id : props_can_heal) {
        const bool did_end = caster->m_properties.end_prop(prop_id);

        is_noticable = is_noticable || did_end;
    }

    if (skill == SpellSkill::transcendent) {
        if (actor::is_player(caster)) {
            prop::Prop* const wound_prop = map::g_player->m_properties.prop(prop::Id::wound);

            if (wound_prop) {
                static_cast<prop::Wound*>(wound_prop)->heal_one_wound();
            }
        }

        auto* const prop = prop::make(prop::Id::regenerating);

        prop->set_duration(regen_duration().roll());

        caster->m_properties.apply(prop);

        is_noticable = true;
    }

    const bool did_restore_hp = actor::restore_hp(*caster, nr_hp_restored(skill));

    if (did_restore_hp) {
        is_noticable = true;
    }

    if (!is_noticable && actor::is_player(caster)) {
        msg_log::add(common_text::g_player_magic_heal_no_effect);
    }
}

bool SpellHeal::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)seen_targets;
    (void)skill;

    return mon.m_hp < actor::max_hp(mon);
}

std::vector<std::string> SpellHeal::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.push_back("Restores " + std::to_string(nr_hp_restored(skill)) + " hit points.");

    if (skill == SpellSkill::expert) {
        descr.emplace_back("Cures weakening and poisoning.");
    }
    else if (skill >= SpellSkill::master) {
        descr.emplace_back(
            "Cures weakening, poisoning, infections, disease, blindness and deafness.");
    }

    if (skill == SpellSkill::transcendent) {
        descr.emplace_back("Heals one wound.");

        descr.emplace_back(
            "+1 hit point regenerated per turn, for " +
            regen_duration().str() +
            " turns.");
    }

    return descr;
}

// -----------------------------------------------------------------------------
// Mi-Go hypnosis
// -----------------------------------------------------------------------------
int SpellMiGoHypno::mon_cooldown() const
{
    return 5;
}

std::string SpellMiGoHypno::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Hypnosis";
}

SpellId SpellMiGoHypno::id() const
{
    return SpellId::mi_go_hypno;
}

SpellDomain SpellMiGoHypno::domain() const
{
    return SpellDomain::END;
}

SpellShock SpellMiGoHypno::shock_type() const
{
    return SpellShock::mild;
}

std::vector<std::string> SpellMiGoHypno::descr_specific(const SpellSkill skill) const
{
    (void)skill;

    return {};
}

int SpellMiGoHypno::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 7;
}

bool SpellMiGoHypno::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

void SpellMiGoHypno::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)skill;
    (void)player_aware;

    actor::Actor* target = map::random_closest_actor(caster->m_pos, seen_targets);

    if (!target) {
        ASSERT(false);

        return;
    }

    if (target->m_properties.has(prop::Id::r_spell)) {
        on_resist(*target, caster);
        return;
    }

    if (actor::is_player(target)) {
        msg_log::add("There is a sharp droning in my head!");
    }

    if (rnd::coin_toss()) {
        spells::run_mi_go_hypno_effect(*target);
    }
    else {
        if (actor::is_player(target)) {
            msg_log::add("I feel dizzy.");
        }
    }
}

bool SpellMiGoHypno::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

// -----------------------------------------------------------------------------
// Immolation
// -----------------------------------------------------------------------------
int SpellBurn::mon_cooldown() const
{
    return 9;
}

std::string SpellBurn::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "an " : "";

    return a + "Immolation";
}

SpellId SpellBurn::id() const
{
    return SpellId::burn;
}

SpellDomain SpellBurn::domain() const
{
    return SpellDomain::END;
}

SpellShock SpellBurn::shock_type() const
{
    return SpellShock::disturbing;
}

std::vector<std::string> SpellBurn::descr_specific(const SpellSkill skill) const
{
    (void)skill;

    return {};
}

int SpellBurn::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 7;
}

bool SpellBurn::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

void SpellBurn::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    actor::Actor* target = map::random_closest_actor(caster->m_pos, seen_targets);

    if (!target) {
        ASSERT(false);

        return;
    }

    if (target->m_properties.has(prop::Id::r_spell)) {
        on_resist(*target, caster);
        return;
    }

    if (actor::can_player_see_actor(*target)) {
        const std::string actor_name =
            actor::is_player(target)
            ? "me"
            : actor::name_the(*target);

        msg_log::add("Flames are rising around " + actor_name + "!");
    }

    prop::Prop* prop = prop::make(prop::Id::burning);

    prop->set_duration(2 + (int)skill);

    target->m_properties.apply(prop);

    if (!actor::is_player(target)) {
        target->become_aware_player(actor::AwareSource::spell_victim);
    }
}

bool SpellBurn::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

// -----------------------------------------------------------------------------
// Deafening
// -----------------------------------------------------------------------------
int SpellDeafen::mon_cooldown() const
{
    return 5;
}

std::string SpellDeafen::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Deafening";
}

SpellId SpellDeafen::id() const
{
    return SpellId::deafen;
}

SpellDomain SpellDeafen::domain() const
{
    return SpellDomain::END;
}

SpellShock SpellDeafen::shock_type() const
{
    return SpellShock::mild;
}

std::vector<std::string> SpellDeafen::descr_specific(const SpellSkill skill) const
{
    (void)skill;

    return {};
}

int SpellDeafen::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 4;
}

bool SpellDeafen::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

void SpellDeafen::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)player_aware;

    actor::Actor* target = map::random_closest_actor(caster->m_pos, seen_targets);

    if (!target) {
        ASSERT(false);

        return;
    }

    if (target->m_properties.has(prop::Id::r_spell)) {
        on_resist(*target, caster);
        return;
    }

    prop::Prop* prop = prop::make(prop::Id::deaf);

    prop->set_duration(75 + (int)skill * 75);

    target->m_properties.apply(prop);
}

bool SpellDeafen::allow_mon_cast_now(
    const actor::Actor& mon,
    const std::vector<actor::Actor*>& seen_targets,
    SpellSkill skill) const
{
    (void)mon;
    (void)skill;

    return !seen_targets.empty();
}

// -----------------------------------------------------------------------------
// Transmutation
// -----------------------------------------------------------------------------
std::string SpellTransmut::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Transmutation";
}

SpellId SpellTransmut::id() const
{
    return SpellId::transmut;
}

SpellDomain SpellTransmut::domain() const
{
    return SpellDomain::mind;
}

bool SpellTransmut::is_tenebrous() const
{
    return true;
}

SpellShock SpellTransmut::shock_type() const
{
    return SpellShock::mild;
}

int SpellTransmut::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 4;
}

bool SpellTransmut::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

int SpellTransmut::skill_bon(const SpellSkill skill) const
{
    return 10 * (int)skill;
}

int SpellTransmut::chance_scroll(const SpellSkill skill) const
{
    return skill_bon(skill) + 40;
}

int SpellTransmut::chance_potion(const SpellSkill skill) const
{
    return skill_bon(skill) + 40;
}

int SpellTransmut::chance_weapon(
    const SpellSkill skill,
    int plus) const
{
    plus = std::min(5, plus);

    return skill_bon(skill) + (plus * 10);
}

void SpellTransmut::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)caster;
    (void)seen_targets;
    (void)player_aware;

    const auto& p = map::g_player->m_pos;

    auto* item_before = map::g_items.at(p);

    if (!item_before) {
        msg_log::add("There is a vague change in the air.");

        return;
    }

    // Player is standing on an item

    // Get information on the existing item(s)
    const bool is_stackable_before = item_before->data().is_stackable;

    const int nr_items_before =
        is_stackable_before
        ? item_before->m_nr_items
        : 1;

    const auto item_type_before = item_before->data().type;

    const int melee_plus = item_before->base_melee_dmg().plus();

    const auto id_before = item_before->id();

    std::string item_name_before = "The ";

    if (nr_items_before > 1) {
        item_name_before += item_before->name(ItemNameType::plural);
    }
    else {
        // Single item
        item_name_before += item_before->name(ItemNameType::plain);
    }

    // Remove the existing item(s)
    delete map::g_items.at(p);
    map::g_items.at(p) = nullptr;

    if (map::g_seen.at(p)) {
        std::string disappear_str =
            (nr_items_before == 1)
            ? "disappears"
            : "disappear";

        msg_log::add(
            item_name_before + " " + disappear_str + ".",
            colors::text(),
            MsgInterruptPlayer::no,
            MorePromptOnMsg::yes);
    }

    // Determine which item(s) to spawn, if any

    int pct_chance_per_item = 0;

    std::vector<item::Id> id_bucket;

    // Converting a potion?
    if (item_type_before == ItemType::potion) {
        pct_chance_per_item = chance_potion(skill);

        for (size_t item_id = 0;
             (item::Id)item_id != item::Id::END;
             ++item_id) {
            if ((item::Id)item_id == id_before) {
                continue;
            }

            const auto& d = item::g_data[item_id];

            if (d.type == ItemType::potion) {
                id_bucket.push_back((item::Id)item_id);
            }
        }
    }
    // Converting a scroll?
    else if (item_type_before == ItemType::scroll) {
        pct_chance_per_item = chance_scroll(skill);

        for (size_t item_id = 0;
             (item::Id)item_id != item::Id::END;
             ++item_id) {
            if ((item::Id)item_id == id_before) {
                continue;
            }

            const auto& d = item::g_data[item_id];

            if (d.type == ItemType::scroll) {
                id_bucket.push_back((item::Id)item_id);
            }
        }
    }
    // Converting a melee weapon (with at least one "plus")?
    else if ((item_type_before == ItemType::melee_wpn) && (melee_plus >= 1)) {
        pct_chance_per_item = chance_weapon(skill, melee_plus);

        for (size_t item_id = 0;
             (item::Id)item_id != item::Id::END;
             ++item_id) {
            const auto& d = item::g_data[item_id];

            if ((d.type == ItemType::potion) ||
                (d.type == ItemType::scroll)) {
                id_bucket.push_back((item::Id)item_id);
            }
        }
    }

    // Never spawn Transmute scrolls, this is just dumb
    for (auto it = std::begin(id_bucket); it != std::end(id_bucket);) {
        if (*it == item::Id::scroll_transmut) {
            it = id_bucket.erase(it);
        }
        else {
            // Not transmute
            ++it;
        }
    }

    auto id_new = item::Id::END;

    if (!id_bucket.empty()) {
        id_new = rnd::element(id_bucket);
    }

    int nr_items_new = 0;

    // How many items?
    for (int i = 0; i < nr_items_before; ++i) {
        if (rnd::percent(pct_chance_per_item)) {
            ++nr_items_new;
        }
    }

    if ((id_new == item::Id::END) || (nr_items_new < 1)) {
        msg_log::add("Nothing appears.");

        return;
    }

    // OK, items are good, and player succeeded the rolls etc

    auto* item_new = item::make(id_new, nr_items_new);

    item::randomize_item_properties(*item_new);

    if (item_new->data().is_stackable) {
        item_new->m_nr_items = nr_items_new;
    }

    const std::string item_name_new =
        text_format::first_to_upper(
            item_new->name(ItemNameType::plural));

    if (map::g_seen.at(p)) {
        std::string appear_str =
            (nr_items_new == 1)
            ? "appears"
            : "appear";

        msg_log::add(item_name_new + " " + appear_str + ".");
    }

    // NOTE: This will possibly make the player "discover" the item, so it
    // should occur last, after the "appear" message.
    item_drop::drop_item_on_map(map::g_player->m_pos, *item_new);
}

std::vector<std::string> SpellTransmut::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Attempts to convert items (stand over an item when casting). "
        "On failure, the item is destroyed.");

    descr.push_back(
        "Converts Potions with " +
        std::to_string(chance_potion(skill)) +
        "% chance.");

    descr.push_back(
        "Converts Manuscripts with " +
        std::to_string(chance_scroll(skill)) +
        "% chance.");

    descr.push_back(
        "Melee weapons with at least +1 damage (not counting any "
        "damage bonus from skills) are converted to a Potion or "
        "Manuscript, with " +
        std::to_string(chance_weapon(skill, 1)) +
        "% chance for a +1 weapon, " +
        std::to_string(chance_weapon(skill, 2)) +
        "% chance for a +2 weapon, " +
        std::to_string(chance_weapon(skill, 3)) +
        "% chance for a +3 weapon, etc.");

    return descr;
}

// -----------------------------------------------------------------------------
// Clairvoyance
// -----------------------------------------------------------------------------
std::string SpellClairvoyance::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Clairvoyance";
}

SpellId SpellClairvoyance::id() const
{
    return SpellId::clairvoyance;
}

SpellDomain SpellClairvoyance::domain() const
{
    return SpellDomain::mind;
}

bool SpellClairvoyance::is_tenebrous() const
{
    return true;
}

bool SpellClairvoyance::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

SpellShock SpellClairvoyance::shock_type() const
{
    return SpellShock::disturbing;
}

Range SpellClairvoyance::duration_range(const SpellSkill skill) const
{
    if (skill == SpellSkill::transcendent) {
        return {400, 800};
    }
    else {
        return {150, 300};
    }
}

int SpellClairvoyance::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 7;
}

void SpellClairvoyance::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    auto* clairvoyance = static_cast<prop::Clairvoyance*>(prop::make(prop::Id::clairvoyance));

    clairvoyance->set_duration(duration_range(skill).roll());

    if (skill >= SpellSkill::expert) {
        clairvoyance->set_allow_reveal_items();
    }

    if (skill >= SpellSkill::master) {
        clairvoyance->set_allow_reveal_creatures();
    }

    caster->m_properties.apply(clairvoyance);
}

std::vector<std::string> SpellClairvoyance::descr_specific(
    const SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Reveals the presence of doors, traps, stairs, and other "
        "locations of interest in the surrounding area.");

    if (skill == SpellSkill::expert) {
        descr.emplace_back("Also reveals items.");
    }
    else if (skill >= SpellSkill::master) {
        descr.emplace_back("Also reveals items and creatures.");
    }

    descr.push_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

// -----------------------------------------------------------------------------
// Blood Tempering
// -----------------------------------------------------------------------------
std::string SpellBloodTempering::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Blood Tempering";
}

SpellId SpellBloodTempering::id() const
{
    return SpellId::blood_tempering;
}

SpellDomain SpellBloodTempering::domain() const
{
    return SpellDomain::blood;
}

bool SpellBloodTempering::is_tenebrous() const
{
    return true;
}

SpellCostType SpellBloodTempering::cost_type() const
{
    return SpellCostType::hit_points;
}

SpellShock SpellBloodTempering::shock_type() const
{
    return SpellShock::disturbing;
}

bool SpellBloodTempering::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

Range SpellBloodTempering::duration_range(SpellSkill skill) const
{
    Range duration_range;

    duration_range.min = 4 + ((int)skill * 2);
    duration_range.max = duration_range.min + 4;

    return duration_range;
}

int SpellBloodTempering::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 8;
}

void SpellBloodTempering::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    int nr_turns = duration_range(skill).roll();

    prop::Prop* r_phys = prop::make(prop::Id::r_phys);

    r_phys->set_duration(nr_turns);

    caster->m_properties.apply(r_phys);
}

std::vector<std::string> SpellBloodTempering::descr_specific(
    SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Through ardous suffering, the caster tempers their body to "
        "resist physical force (cannot be harmed by normal attacks, "
        "however other forms of damage such as fire is still "
        "harmful).");

    descr.emplace_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

// -----------------------------------------------------------------------------
// Thorns
// -----------------------------------------------------------------------------
std::string SpellThorns::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Thorns";
}

SpellId SpellThorns::id() const
{
    return SpellId::thorns;
}

SpellDomain SpellThorns::domain() const
{
    return SpellDomain::blood;
}

SpellCostType SpellThorns::cost_type() const
{
    return SpellCostType::hit_points;
}

SpellShock SpellThorns::shock_type() const
{
    return SpellShock::mild;
}

int SpellThorns::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 4;
}

bool SpellThorns::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

Range SpellThorns::duration_range(const SpellSkill skill) const
{
    Range duration_range;

    duration_range.min = ((int)skill + 1) * 5;
    duration_range.max = duration_range.min * 2;

    return duration_range;
}

Range SpellThorns::dmg_range(const SpellSkill skill) const
{
    switch (skill) {
    case SpellSkill::basic:        return {2, 4};   // Avg 3.0
    case SpellSkill::expert:       return {3, 6};   // Avg 4.5
    case SpellSkill::master:       return {4, 8};   // Avg 6.0
    case SpellSkill::transcendent: return {5, 10};  // Avg 7.5
    }

    ASSERT(false);

    return {1, 1};
}

void SpellThorns::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    auto* const prop = static_cast<prop::Thorns*>(prop::make(prop::Id::thorns));

    prop->set_duration(duration_range(skill).roll());

    prop->set_dmg(dmg_range(skill).roll());

    caster->m_properties.apply(prop);
}

std::vector<std::string> SpellThorns::descr_specific(SpellSkill skill) const
{
    std::vector<std::string> descr;

    // Re-using the property description as spell description.
    descr.push_back(prop::g_data[(size_t)prop::Id::thorns].descr);

    descr.push_back(
        "The spell returns " +
        dmg_range(skill).str() +
        " damage to the attacker.");

    descr.emplace_back("The spell lasts " + duration_range(skill).str() + " turns.");

    return descr;
}

// -----------------------------------------------------------------------------
// Crimson Passage
// -----------------------------------------------------------------------------
std::string SpellCrimsonPassage::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Crimson Passage";
}

SpellId SpellCrimsonPassage::id() const
{
    return SpellId::crimson_passage;
}

SpellDomain SpellCrimsonPassage::domain() const
{
    return SpellDomain::blood;
}

SpellCostType SpellCrimsonPassage::cost_type() const
{
    return SpellCostType::hit_points;
}

bool SpellCrimsonPassage::is_noisy(SpellSkill skill) const
{
    return (skill == SpellSkill::basic);
}

SpellShock SpellCrimsonPassage::shock_type() const
{
    // If the effect is already active, the spell does not cause shock.
    //
    // HACK: Assuming only the player can cast this spell.
    return (
        map::g_player->m_properties.has(prop::Id::crimson_passage)
            ? SpellShock::none
            : SpellShock::disturbing);
}

int SpellCrimsonPassage::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    // If the effect is already active, the spell is free to cast.

    // HACK: Assuming only the player can cast this spell.
    return map::g_player->m_properties.has(prop::Id::crimson_passage) ? 0 : 3;
}

int SpellCrimsonPassage::nr_steps_allowed(const SpellSkill skill) const
{
    if (skill == SpellSkill::transcendent) {
        return -1;
    }
    else {
        return ((int)skill + 1) * 4;
    }
}

void SpellCrimsonPassage::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    if (caster->m_properties.has(prop::Id::crimson_passage)) {
        // Effect already active, cancel it instead.
        caster->m_properties.end_prop(prop::Id::crimson_passage);

        return;
    }

    auto* const prop =
        static_cast<prop::CrimsonPassage*>(
            prop::make(prop::Id::crimson_passage));

    prop->set_indefinite();

    prop->set_nr_steps_allowed(nr_steps_allowed(skill));

    caster->m_properties.apply(prop);
}

std::vector<std::string> SpellCrimsonPassage::descr_specific(
    SpellSkill skill) const
{
    std::vector<std::string> descr;

    // Re-using the property description as spell description.
    descr.push_back(prop::g_data[(size_t)prop::Id::crimson_passage].descr);

    const int nr_steps = nr_steps_allowed(skill);

    if (nr_steps == -1) {
        descr.emplace_back(
            "An infinite number of steps may be taken, the spell "
            "is only limited by the number of hit points.");
    }
    else {
        descr.emplace_back(
            std::to_string(nr_steps_allowed(skill)) +
            " steps may be taken before the effect ends.");
    }

    descr.emplace_back(
        "Casting the spell again while it is already active cancels "
        "the effect (this does not drain hit points or cause shock).");

    return descr;
}

// -----------------------------------------------------------------------------
// Sacrifice Life
// -----------------------------------------------------------------------------
std::string SpellSacrificeLife::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Sacrifice Life";
}

SpellId SpellSacrificeLife::id() const
{
    return SpellId::sacrifice_life;
}

SpellDomain SpellSacrificeLife::domain() const
{
    return SpellDomain::blood;
}

bool SpellSacrificeLife::is_tenebrous() const
{
    return true;
}

SpellShock SpellSacrificeLife::shock_type() const
{
    return SpellShock::disturbing;
}

int SpellSacrificeLife::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 0;
}

bool SpellSacrificeLife::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return true;
}

int SpellSacrificeLife::nr_sp_per_hp(const SpellSkill skill) const
{
    return 1 + (int)skill;
}

void SpellSacrificeLife::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;
    (void)player_aware;

    const int hp = caster->m_hp;

    if (hp <= 2) {
        // Not enough HP.
        msg_log::add("I feel like I have very little to offer.");

        return;
    }

    int hp_drained = ((hp - 1) / 2) * 2;

    hp_drained = std::min(8, hp_drained);

    actor::hit(*caster, hp_drained, DmgType::pure, nullptr, AllowWound::no);

    const int sp_gained = hp_drained * nr_sp_per_hp(skill);

    actor::restore_sp(*caster, sp_gained, actor::AllowRestoreAboveMax::yes);
}

std::vector<std::string> SpellSacrificeLife::descr_specific(
    SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Sacrifices the life force of the caster in order to restore "
        "the spirit. The amount restored is proportional to the life "
        "lost. A maximum of 8 hit points may be sacrificed.");

    const int k = nr_sp_per_hp(skill);

    if (k == 1) {
        descr.emplace_back(
            "For each hit point sacrificed, " +
            std::to_string(k) +
            " spirit point is gained.");
    }
    else {
        descr.emplace_back(
            "For each hit point sacrificed, " +
            std::to_string(k) +
            " spirit points are gained.");
    }

    return descr;
}

// -----------------------------------------------------------------------------
// Shed Impurity
// -----------------------------------------------------------------------------
std::string SpellShedImpurity::name(const SpellNameType name_type) const
{
    const std::string a = (name_type == SpellNameType::a) ? "a " : "";

    return a + "Shed Impurity";
}

SpellId SpellShedImpurity::id() const
{
    return SpellId::shed_impurity;
}

SpellDomain SpellShedImpurity::domain() const
{
    return SpellDomain::blood;
}

SpellShock SpellShedImpurity::shock_type() const
{
    return SpellShock::mild;
}

int SpellShedImpurity::base_max_cost(
    const SpellSkill skill,
    const actor::Actor* const caster) const
{
    (void)skill;
    (void)caster;

    return 0;
}

bool SpellShedImpurity::is_noisy(const SpellSkill skill) const
{
    (void)skill;

    return false;
}

int SpellShedImpurity::get_min_hp_removed_for_bonus_effects() const
{
    return 9;
}

int SpellShedImpurity::get_moribund_hp_limit() const
{
    return player_bon::has_trait(TraitId::memento_mori) ? 8 : 6;
}

int SpellShedImpurity::calc_nr_hp_removed(const actor::Actor* const caster) const
{
    return caster->m_hp - get_moribund_hp_limit();
}

void SpellShedImpurity::run_effect(
    actor::Actor* const caster,
    const SpellSkill skill,
    const std::vector<actor::Actor*>& seen_targets,
    PlayerAwareOfCast player_aware) const
{
    (void)seen_targets;

    const int hp_removed = calc_nr_hp_removed(caster);

    if (hp_removed <= 0) {
        // Not enough HP.
        msg_log::add("There is nothing more to shed.");

        return;
    }

    actor::hit(*caster, hp_removed, DmgType::pure, nullptr, AllowWound::no);

    if (hp_removed >= get_min_hp_removed_for_bonus_effects()) {
        caster->m_properties.end_prop(prop::Id::weakened);
        caster->m_properties.end_prop(prop::Id::poisoned);

        if ((int)skill >= (int)SpellSkill::expert) {
            caster->m_properties.end_prop(prop::Id::infected);
            caster->m_properties.end_prop(prop::Id::diseased);
        }

        if (skill >= SpellSkill::master) {
            caster->m_properties.end_prop(prop::Id::slowed);
        }

        if (skill == SpellSkill::transcendent) {
            std::unique_ptr<Spell> bless_spell(spells::make(SpellId::bless));

            bless_spell->run_effect(caster, SpellSkill::basic, {}, player_aware);
        }
    }
}

std::vector<std::string> SpellShedImpurity::descr_specific(
    SpellSkill skill) const
{
    std::vector<std::string> descr;

    descr.emplace_back(
        "Purifies the caster by carving away all that is extraneous, "
        "revealing the essential core of their being.");

    descr.emplace_back(
        "Hit points are lowered to the limit where the Moribund effect is activated "
        "(bonuses for having low hit points). "
        "This limit is at " +
        std::to_string(get_moribund_hp_limit()) +
        " hit points.");

    std::string bonus_effect_descr =
        "If at least " +
        std::to_string(get_min_hp_removed_for_bonus_effects()) +
        " hit points are lost, then ";

    switch (skill) {
    case SpellSkill::basic: {
        bonus_effect_descr +=
            "weakening and poisoning are cured.";
    } break;

    case SpellSkill::expert: {
        bonus_effect_descr +=
            "weakening, poisoning, infection and disease are cured.";
    } break;

    case SpellSkill::master: {
        bonus_effect_descr +=
            "weakening, poisoning, infection, disease and slowing are cured.";
    } break;

    case SpellSkill::transcendent: {
        std::unique_ptr<SpellBless> bless_spell(
            static_cast<SpellBless*>(spells::make(SpellId::bless)));

        bonus_effect_descr +=
            "weakening, poisoning, infection, disease and slowing are cured. "
            "The caster is also blessed for " +
            bless_spell->duration_range(SpellSkill::basic).str() +
            " turns.";
    } break;
    }

    bonus_effect_descr +=
        " Currently " +
        std::to_string(calc_nr_hp_removed(map::g_player)) +
        " hit points would be removed.";

    descr.push_back(bonus_effect_descr);

    return descr;
}

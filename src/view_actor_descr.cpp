// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#include "view_actor_descr.hpp"

#include "SDL_keycode.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "ability_values.hpp"
#include "actor.hpp"
#include "actor_data.hpp"
#include "actor_sneak.hpp"
#include "attack_data.hpp"
#include "colors.hpp"
#include "debug.hpp"
#include "global.hpp"
#include "inventory.hpp"
#include "io.hpp"
#include "item.hpp"
#include "item_weapon.hpp"
#include "map.hpp"
#include "panel.hpp"
#include "property.hpp"
#include "property_data.hpp"
#include "property_handler.hpp"
#include "text.hpp"
#include "text_format.hpp"

// -----------------------------------------------------------------------------
// private
// -----------------------------------------------------------------------------
enum class PropTextCategory
{
    cannot_be_harmed_by,
    unaffected_by,
    cannot_be,
    cannot,
    can,
    custom
};

struct PropTextData
{
    PropTextCategory category;

    // Property ID and sentence fragment (or a full sentence if "custom" category).
    std::initializer_list<std::pair<prop::Id, std::string>> entries;
};

// This is used for showing descriptions of a monster's "natural properties" (e.g. for a monster
// that is naturally fire resistant, as opposed to a temporary effect). Not all types of properties
// are included in the creature descriptions.
//
// The data is structured in categories that correspond to a beginning of a sentence, for example
// "They cannot be harmed by", and in each such cateogry there is a list of properties and what text
// should be written for that property (colors are supported).
//
// The result may be something like "They cannot be harmed by fire or poison".
//
// The structure of the data below also controls the order in which things will be listed (both the
// order of the categories and the order of the properties within each category).
//
static const PropTextData s_prop_text_data[] = {
    // --- Cannot be harmed by ... ---
    {
        PropTextCategory::cannot_be_harmed_by,
        {
            {prop::Id::r_phys, "{COLOR_GRAY}physical damage{reset_color}"},
            {prop::Id::r_fire, "{COLOR_LIGHT_RED}fire{reset_color}"},
            {prop::Id::r_elec, "{COLOR_YELLOW}electricity{reset_color}"},
            {prop::Id::r_poison, "{COLOR_LIGHT_GREEN}poison{reset_color}"},
            {prop::Id::r_disease, "{COLOR_GREEN}disease{reset_color}"},
            {prop::Id::r_spell, "{COLOR_MAGENTA}magic{reset_color}"},
        },
    },

    // --- Unaffected by ... ---
    {
        PropTextCategory::unaffected_by,
        {
            {prop::Id::r_fear, "fear"},
            {prop::Id::r_conf, "confusion"},
        },
    },

    // --- Cannot be ... ---
    {
        PropTextCategory::cannot_be,
        {
            {prop::Id::r_slow, "slowed"},
            {prop::Id::r_para, "paralyzed"},
        },
    },

    // --- Cannot ... ---
    {
        PropTextCategory::cannot,
        {
            {prop::Id::r_sleep, "faint"},
        },
    },

    // --- Can ... ---
    {
        PropTextCategory::can,
        {
            {prop::Id::darkvision, "see in darkness"},
        },
    },

    // --- Fully custom sentences. ---
    {
        PropTextCategory::custom,
        {
            {prop::Id::reduced_pierce_dmg,
             "Piercing attacks such as pistol shots or dagger strikes are very "
             "ineffective against them"},

            {prop::Id::radiant_self,
             "They emit light and can be seen in darkness"},

            {prop::Id::radiant_adjacent,
             "They emit light and can be seen in darkness"},

            {prop::Id::radiant_fov,
             "They emit light and can be seen in darkness"},

            {prop::Id::regenerating,
             "They regenerate health over time"},

            {prop::Id::explodes_on_death,
             "They explode on death"},

            {prop::Id::flammable,
             "They are very flammable, and will quickly ignite other nearby "
             "flammable creatures"},

            {prop::Id::undead,
             "{COLOR_MAGENTA}This creature is undead{reset_color}"},

            {prop::Id::outer_being,
             "{COLOR_VIOLET}They are an Outer Being, not anchored to "
             "this reality{reset_color}"},
        },
    },
};

static std::string prop_text_category_prefix_str(PropTextCategory category)
{
    switch (category) {
    case PropTextCategory::cannot_be_harmed_by:
        return "They cannot be harmed by";
    case PropTextCategory::unaffected_by:
        return "They are unaffected by";
    case PropTextCategory::cannot_be:
        return "They cannot be";
    case PropTextCategory::cannot:
        return "They cannot";
    case PropTextCategory::can:
        return "They can";
    case PropTextCategory::custom:
        return "";
    }
    return "";
}

struct MonShockStrings
{
    std::string color_fmt_str;
    std::string shock_str;
    std::string punct_str;
};

static std::string get_mon_memory_turns_descr(
    const actor::ActorData& actor_data,
    const actor::Actor& actor)
{
    const int nr_turns_aware = actor_data.nr_turns_aware;

    if (nr_turns_aware <= 0) {
        return "";
    }

    const std::string name_a = text_format::first_to_upper(actor::name_a(actor));

    if (nr_turns_aware < 50) {
        const std::string nr_turns_aware_str =
            std::to_string(nr_turns_aware);

        return name_a +
            " will remember hostile creatures for at least "
            "{COLOR_DARK_YELLOW}" +
            nr_turns_aware_str +
            "{_}turns{reset_color}.";
    }
    else {
        // Very high number of turns awareness
        return (
            name_a +
            " remembers hostile creatures for a "
            "{COLOR_DARK_YELLOW}very long time{reset_color}.");
    }
}

static std::string mon_speed_type_to_str(const actor::Speed speed)
{
    switch (speed) {
    case actor::Speed::slow:
        return "slowly";

    case actor::Speed::normal:
        return "";

    case actor::Speed::fast:
        return "fast";

    case actor::Speed::very_fast:
        return "very swiftly";
    }

    ASSERT(false);

    return "";
}

static std::string get_mon_speed_descr(
    const actor::ActorData& actor_data,
    const actor::Actor& actor)
{
    const std::string speed_type_str = mon_speed_type_to_str(actor_data.speed);

    if (speed_type_str.empty()) {
        return "";
    }

    if (actor_data.is_unique) {
        return (
            actor::name_the(actor) +
            " appears to move{_}" +
            speed_type_str +
            ".");
    }
    else {
        return (
            "They appear to move{_}" +
            speed_type_str +
            ".");
    }
}

static MonShockStrings mon_shock_lvl_to_strings(const MonShockLvl shock_lvl)
{
    MonShockStrings result;

    switch (shock_lvl) {
    case MonShockLvl::unsettling:
        result.color_fmt_str = "{COLOR_DARK_BROWN}";
        result.shock_str = "unsettling";
        result.punct_str = ".";
        break;

    case MonShockLvl::frightening:
        result.color_fmt_str = "{COLOR_GRAY}";
        result.shock_str = "frightening";
        result.punct_str = ".";
        break;

    case MonShockLvl::terrifying:
        result.color_fmt_str = "{COLOR_RED}";
        result.shock_str = "terrifying";
        result.punct_str = "!";
        break;

    case MonShockLvl::mind_shattering:
        result.color_fmt_str = "{COLOR_LIGHT_RED}";
        result.shock_str = "mind shattering";
        result.punct_str = "!";
        break;

    case MonShockLvl::none:
    case MonShockLvl::END:
        break;
    }

    return result;
}

static std::string get_mon_shock_descr(
    const actor::ActorData& actor_data,
    const actor::Actor& actor)
{
    const MonShockStrings shock_strings = mon_shock_lvl_to_strings(actor_data.mon_shock_lvl);

    if (shock_strings.shock_str.empty()) {
        return "";
    }

    const std::string prefix_str =
        actor_data.is_unique
        ? (actor::name_the(actor) + " is ")
        : ("They are ");

    return (
        shock_strings.color_fmt_str +
        prefix_str +
        shock_strings.shock_str +
        " to behold" +
        shock_strings.punct_str +
        "{reset_color}");
}

static std::string get_mon_wielded_wpn_str(
    const actor::ActorData& actor_data,
    const actor::Actor& actor)
{
    const item::Item* const wpn = actor.m_inv.item_in_slot(SlotId::wpn);

    if (!wpn) {
        return "";
    }

    const std::string pronoun_str =
        actor_data.is_unique
        ? actor::name_the(actor)
        : "It";

    const std::string wpn_name_a =
        wpn->name(
            ItemNameType::a,
            ItemNameInfo::none,
            ItemNameAttackInfo::none);

    return pronoun_str + " is wielding " + wpn_name_a + ".";
}

static std::string get_mon_current_health_descr(const actor::Actor& actor)
{
    int hp_pct = ((actor.m_hp * 100) / actor::max_hp(actor));

    hp_pct = std::max(1, hp_pct);

    std::string str =
        (hp_pct >= 100)
        ? "They are at full health."
        : ("They are at " + std::to_string(hp_pct) + "% health.");

    return str;
}

static std::string get_melee_hit_chance_descr(actor::Actor& actor)
{
    const item::Item* wielded_item = map::g_player->m_inv.item_in_slot(SlotId::wpn);

    const item::Wpn* const wpn =
        wielded_item
        ? static_cast<const item::Wpn*>(wielded_item)
        : &map::g_player->unarmed_wpn();

    if (!wpn) {
        ASSERT(false);

        return "";
    }

    const std::unique_ptr<item::Wpn> kick(map::g_player->make_kick_wpn(actor));

    const MeleeAttData wpn_att_data(map::g_player, actor, *wpn);

    const MeleeAttData kick_att_data(map::g_player, actor, *kick);

    const int wpn_hit_chance =
        ability_roll::success_chance_pct_actual(
            wpn_att_data.hit_chance_tot);

    const int kick_hit_chance =
        ability_roll::success_chance_pct_actual(
            kick_att_data.hit_chance_tot);

    std::string descr;

    if (wpn_hit_chance == kick_hit_chance) {
        descr =
            "The chance to hit " +
            actor::name_the(actor) +
            " with a melee attack or kicking is currently{_}{COLOR_LIGHT_GREEN}" +
            std::to_string(wpn_hit_chance) +
            "%{reset_color}";
    }
    else {
        descr =
            "The chance to hit " +
            actor::name_the(actor) +
            " with a melee attack is currently{_}{COLOR_LIGHT_GREEN}" +
            std::to_string(wpn_hit_chance) +
            "%{reset_color}" +
            ", and the chance to hit by kicking is{_}{COLOR_LIGHT_GREEN}" +
            std::to_string(kick_hit_chance) +
            "%{reset_color}";
    }

    if (wpn_att_data.is_backstab) {
        descr += " (because they are unaware)";
    }

    descr += ".";

    return descr;
}

static std::string get_sneak_chance_descr(actor::Actor& actor)
{
    actor::SneakParameters p;
    p.actor_searching = &actor;
    p.actor_sneaking = map::g_player;

    const int tot_value =
        ability_roll::success_chance_pct_actual(
            actor::calc_total_sneak_ability(p));

    std::string descr =
        "The chance to remain undetected by " +
        actor::name_the(actor) +
        " is currently{_}{COLOR_LIGHT_GREEN}" +
        std::to_string(tot_value) +
        "%{reset_color}.";

    return descr;
}

static bool has_natural_property(
    const actor::ActorData& actor_data,
    const prop::Id id)
{
    return actor_data.natural_props[(size_t)id];
}

// Adds strings "foo", "bar", "baz" to "base string" as "base string foo, bar, or baz". Used for
// building sentences like "[...] cannot be harmed by fire or poison".
static void append_list_joined_with_or(
    std::string& base_str,
    const std::vector<std::string>& strings)
{
    // This function should only ever be used to append to things with an actual base string
    // (e.g. "They cannot be harmed by"):
    ASSERT(!base_str.empty());

    const size_t nr_strings = strings.size();

    for (size_t i = 0; i < nr_strings; ++i) {
        if ((nr_strings > 2) && (i > 0)) {
            base_str += ",";
        }

        base_str += " ";

        if ((nr_strings >= 2) && (i == (nr_strings - 1))) {
            base_str += "or ";
        }

        base_str += strings[i];
    }
}

static std::string get_mon_natural_properties_descr(const actor::ActorData& actor_data)
{
    std::string descr;

    for (const PropTextData& prop_text_data : s_prop_text_data) {
        std::vector<std::string> applicable_strings;

        for (const auto& entry : prop_text_data.entries) {
            if (has_natural_property(actor_data, entry.first)) {
                applicable_strings.push_back(entry.second);
            }
        }

        if (!applicable_strings.empty()) {
            if (prop_text_data.category == PropTextCategory::custom) {
                for (const std::string& str : applicable_strings) {
                    text_format::append_with_space(descr, str);
                    descr += ".";
                }
            }
            else {
                const std::string prefix =
                    prop_text_category_prefix_str(
                        prop_text_data.category);

                text_format::append_with_space(descr, prefix);

                append_list_joined_with_or(descr, applicable_strings);

                descr += ".";
            }
        }
    }

    return descr;
}

static std::string auto_description_str(actor::Actor& actor)
{
    std::string str;

    const actor::ActorData* actor_data =
        actor.m_hallucination_mimic_data
        ? actor.m_hallucination_mimic_data
        : actor.m_data;

    if (!actor.is_actor_my_leader(map::g_player)) {
        text_format::append_with_space(str, get_melee_hit_chance_descr(actor));
    }

    const bool* ai = actor_data->ai;
    const bool looks = ai[(size_t)actor::AiId::looks];

    if (!actor::is_aware_of_player(actor) &&
        !actor.is_actor_my_leader(map::g_player) &&
        looks) {
        text_format::append_with_space(str, get_sneak_chance_descr(actor));
    }

    if (actor_data->allow_speed_descr) {
        // NOTE: Speed type is always shown with the real monster data and ignores
        // hallucination (the player character would see how fast they move despite seeing
        // the monster as something else).
        text_format::append_with_space(str, get_mon_speed_descr(*actor.m_data, actor));
    }

    if (!actor.is_actor_my_leader(map::g_player)) {
        text_format::append_with_space(
            str, get_mon_memory_turns_descr(*actor_data, actor));
    }

    if (!looks) {
        text_format::append_with_space(
            str,
            "They cannot visually detect other creatures");

        const bool pursues =
            ai[(size_t)actor::AiId::moves_to_target_when_los] ||
            ai[(size_t)actor::AiId::paths_to_target_when_aware];

        if (pursues) {
            str += " (but will pursue any threat once aware)";
        }

        str += ".";
    }

    if (!actor.is_actor_my_leader(map::g_player)) {
        text_format::append_with_space(str, get_mon_shock_descr(*actor_data, actor));
    }

    if (actor_data->allow_wielded_wpn_descr) {
        text_format::append_with_space(str, get_mon_wielded_wpn_str(*actor_data, actor));
    }

    text_format::append_with_space(
        str,
        get_mon_current_health_descr(actor));

    const std::string natural_properties_descr = get_mon_natural_properties_descr(*actor_data);

    if (!natural_properties_descr.empty()) {
        if (!str.empty()) {
            str += "\n\n";
        }

        str += natural_properties_descr;
    }

    return str;
}

static bool should_show_property(const actor::Actor& actor, const prop::Prop& property)
{
    // Show all temporary negative properties (burning, fear etc), and
    // Frenzy (special case, it is typically very important to know, and
    // would be reasonable for the player character to notice this).
    //
    // We do not want to show temporary positive properties, since those are
    // not marked on the creature symbol on the map (perhaps they should,
    // but currently they are not), and also because it should not be known
    // whether a monster has Spell Shield active (and perhaps other
    // properties as well).
    //
    return (
        actor.m_properties.is_temporary_negative_prop(property) ||
        property.id() == prop::Id::frenzied);
}

static std::vector<prop::PropListEntry> temporary_properties_to_show(const actor::Actor& actor)
{
    std::vector<prop::PropListEntry> prop_list = actor.m_properties.property_names_and_descr();

    for (auto it = std::begin(prop_list); it != std::end(prop_list);) {
        const prop::Prop* const prop = it->prop;

        if (should_show_property(actor, *prop)) {
            ++it;
        }
        else {
            it = prop_list.erase(it);
        }
    }

    return prop_list;
}

static std::string temporary_properties_str(actor::Actor& actor)
{
    std::string str;

    // Properties
    const std::vector<prop::PropListEntry> prop_list = temporary_properties_to_show(actor);

    for (const prop::PropListEntry& entry : prop_list) {
        if (!str.empty()) {
            str += "\n";
        }

        // HACK: This assumes the good/bad/text colors, which is defined in a data
        // file. However the text formatter does not recognize those IDs.
        if (entry.title.color == colors::msg_good()) {
            str += "{COLOR_LIGHT_GREEN}";
        }
        else if (entry.title.color == colors::msg_bad()) {
            str += "{COLOR_LIGHT_RED}";
        }
        else {
            str += "{COLOR_WHITE}";
        }

        str += entry.title.str;
        str += "{color_reset}: ";
        str += entry.descr;
    }

    return str;
}

// -----------------------------------------------------------------------------
// View actor description
// -----------------------------------------------------------------------------
StateId ViewActorDescr::id() const
{
    return StateId::view_actor;
}

void ViewActorDescr::draw()
{
    io::cover_panel(Panel::screen);

    draw_interface();

    int y = 0;

    // Fixed decription
    {
        Text text;

        text.set_w(panels::w(Panel::info_screen_content));

        text.set_str(actor::descr(m_actor));

        text.set_color(colors::text());

        text.draw(Panel::info_screen_content, {0, y});

        y += text.nr_lines();
    }

    // Auto description
    //
    // HACK: Skip auto description for player mirror images allied to the player (should not
    // print stuff like if they can visually detect other creatures).
    //
    // TODO: There should be a setting in the monster data instead to completely disable
    // auto-description (currently there's settings for the wielded weapon and speed parts).
    //
    if ((m_actor.m_data->id != "MON_MIRROR_IMAGE") || m_actor.m_hallucination_mimic_data) {
        const std::string auto_descr_str = auto_description_str(m_actor);

        if (!auto_descr_str.empty()) {
            ++y;

            Text text;

            text.set_w(panels::w(Panel::info_screen_content));
            text.set_str(auto_description_str(m_actor));
            text.set_color(colors::text());

            text.draw(Panel::info_screen_content, {0, y});

            y += text.nr_lines();
        }
    }

    // Current properties (not all are shown)
    {
        ++y;

        Text text;

        text.set_w(panels::w(Panel::info_screen_content));
        text.set_str(temporary_properties_str(m_actor));
        text.set_color(colors::text());

        text.draw(Panel::info_screen_content, {0, y});
    }
}

void ViewActorDescr::update()
{
    const io::InputData input = io::read_input();

    switch (input.key) {
    case SDLK_SPACE:
    case SDLK_ESCAPE: {
        // Exit screen
        states::pop();
    } break;

    default: {
    } break;
    }
}

std::string ViewActorDescr::title() const
{
    return text_format::first_to_upper(actor::name_a(m_actor));
}

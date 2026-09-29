// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#include "player_bon.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>

#include "actor.hpp"
#include "actor_data.hpp"
#include "actor_player_state.hpp"
#include "colors.hpp"
#include "create_character.hpp"
#include "debug.hpp"
#include "game.hpp"
#include "global.hpp"
#include "item_data.hpp"
#include "map.hpp"
#include "player_spells.hpp"
#include "property.hpp"
#include "property_data.hpp"
#include "property_factory.hpp"
#include "property_handler.hpp"
#include "random.hpp"
#include "saving.hpp"
#include "spells.hpp"
#include "state.hpp"
#include "text_format.hpp"

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------
struct TraitData
{
    TraitId id {TraitId::END};
    std::string title {};
    std::string descr {};
    std::string extra_descr_when_picking {};
    std::function<void()> on_picked {};
    std::function<void()> on_removed {};
    std::vector<TraitId> trait_prereqs {};
    std::optional<player_bon::SpecialReq> special_prereq {};
    Bg bg_prereq {Bg::END};
    int clvl_prereq {0};
    std::vector<Bg> blocked_for_bgs {};
};

static TraitData s_trait_data[(size_t)TraitId::END];

// NOTE: This is stored separately from the trait data since we sometimes need to update the trait
// data (e.g. to update trait descriptions containing information on the player's current spirit for
// spell traits). Bundling the picked state with the other trait data would be confusing and
// inconvenient.
static bool s_traits_picked[(size_t)TraitId::END];

static std::vector<player_bon::TraitLogEntry> s_trait_log;

static auto s_player_bg = Bg::END;
static auto s_player_occultist_domain = SpellDomain::END;

static const int s_occultist_spell_upgrade_lvl_1 = 4;
static const int s_occultist_spell_upgrade_lvl_2 = 8;

static std::vector<int> s_exorcist_extra_trait_lvls = {2, 6};

static const int s_flagellant_spell_upgrade_lvl_1 = 4;
static const int s_flagellant_spell_upgrade_lvl_2 = 8;

static std::string trait_descr_for_spells(
    const std::vector<SpellId>& spell_ids,
    const SpellSkill skill)
{
    std::string str = "Gain the ability to cast ";

    std::vector<std::string> spell_strings;

    // Assert that the player character has been initialized as it is used below - also, spell
    // costs might be affected by whether the caster is the player or not.
    ASSERT(actor::is_player(map::g_player));

    for (const SpellId id : spell_ids) {
        std::unique_ptr<Spell> spell(spells::make(id));

        const std::string name = spell->name();

        const auto cost_str = spell->cost_range(skill, map::g_player).str();

        spell_strings.emplace_back(name + " (" + cost_str + " spirit)");
    }

    str += text_format::make_comma_and_str(spell_strings);

    str += " at " + spells::skill_to_str(skill) + " level.";

    return str;
}

static std::string get_player_available_sp_str()
{
    const std::string sp_str = std::to_string(map::g_player->m_sp);
    const std::string max_sp_str = std::to_string(actor::max_sp(*map::g_player));

    std::string descr = "You currently have " + sp_str + "/" + max_sp_str + " spirit";

    if (player_bon::is_bg(Bg::exorcist)) {
        const std::string fp_str = std::to_string(actor::player_state::g_exorcist_fervor);
        const std::string max_fp_str = std::to_string(actor::player_exorcist_max_fervor());

        descr += " + " + fp_str + "/" + max_fp_str + " fervor";
    }

    descr += ".";

    return descr;
}

static std::string exorcist_extra_traits_descr()
{
    std::string str = "Gains a bonus trait at character ";

    str +=
        (s_exorcist_extra_trait_lvls.size() == 1)
        ? "level"
        : "levels";

    std::vector<std::string> number_strings;

    number_strings.reserve(s_exorcist_extra_trait_lvls.size());

    for (int lvl : s_exorcist_extra_trait_lvls) {
        number_strings.push_back(std::to_string(lvl));
    }

    str += " " + text_format::make_comma_and_str(number_strings) + ".";

    return str;
}

static void incr_spell_skills(const SpellDomain spell_domain)
{
    for (int i = 0; i < (int)SpellId::END; ++i) {
        const auto id = (SpellId)i;

        const std::unique_ptr<Spell> spell(spells::make(id));

        if (spell->domain() == spell_domain) {
            player_spells::incr_spell_skill(id, Verbose::yes);
        }
    }
}

static void decr_spell_skills(const SpellDomain spell_domain)
{
    for (int i = 0; i < (int)SpellId::END; ++i) {
        const auto id = (SpellId)i;

        const std::unique_ptr<Spell> spell(spells::make(id));

        if (spell->domain() == spell_domain) {
            player_spells::decr_spell_skill(id, Verbose::yes);
        }
    }
}

static TraitData& trait_data(const TraitId id)
{
    ASSERT(id != TraitId::END);

    return s_trait_data[(size_t)id];
}

static void set_trait_data(TraitData& d)
{
    ASSERT(d.id != TraitId::END);

    s_trait_data[(size_t)d.id] = d;

    d = {};
}

static bool knows_spell_with_domain(const SpellDomain domain)
{
    for (int i = 0; i < (int)SpellId::END; ++i) {
        const auto spell_id = (SpellId)i;

        if (player_spells::is_spell_learned(spell_id)) {
            const std::unique_ptr<Spell> tmp_spell(spells::make(spell_id));

            if (tmp_spell->domain() == domain) {
                return true;
            }
        }
    }

    return false;
}

static void update_trait_data()
{
    for (auto& d : s_trait_data) {
        d = {};
    }

    TraitData d;

    // --- Adept Melee Fighter ---
    d.id = TraitId::adept_melee;
    d.title = "Adept Melee Fighter";
    d.descr = "+10% hit chance and +1 damage with melee attacks";
    set_trait_data(d);

    // --- Expert Melee Fighter ---
    d = trait_data(TraitId::adept_melee);
    d.id = TraitId::expert_melee;
    d.title = "Expert Melee Fighter";
    d.trait_prereqs = {TraitId::adept_melee};
    d.blocked_for_bgs = {Bg::exorcist};
    set_trait_data(d);

    // --- Master Melee Fighter ---
    d = trait_data(TraitId::adept_melee);
    d.id = TraitId::master_melee;
    d.title = "Master Melee Fighter";
    d.trait_prereqs = {TraitId::expert_melee};
    d.blocked_for_bgs = {Bg::exorcist, Bg::occultist};
    set_trait_data(d);

    // --- Adept Marksman ---
    d.id = TraitId::adept_marksman;
    d.title = "Adept Marksman";
    d.descr = (
        "+10% hit chance and +1 minimum damage with firearms and thrown weapons "
        "(cannot raise maximum damage)");
    d.blocked_for_bgs = {Bg::ghoul};
    set_trait_data(d);

    // --- Expert Marksman ---
    d = trait_data(TraitId::adept_marksman);
    d.id = TraitId::expert_marksman;
    d.title = "Expert Marksman";
    d.trait_prereqs = {TraitId::adept_marksman};
    d.blocked_for_bgs = {Bg::ghoul, Bg::exorcist};
    set_trait_data(d);

    // --- Master Marksman ---
    d = trait_data(TraitId::adept_marksman);
    d.id = TraitId::master_marksman;
    d.title = "Master Marksman";
    d.trait_prereqs = {TraitId::expert_marksman};
    d.blocked_for_bgs = {Bg::ghoul, Bg::exorcist, Bg::occultist, Bg::flagellant};
    set_trait_data(d);

    // --- Cool-headed ---
    d.id = TraitId::cool_headed;
    d.title = "Cool-headed";
    d.descr = "+20% mental shock resistance";
    set_trait_data(d);

    // --- Courageous ---
    d = trait_data(TraitId::cool_headed);
    d.id = TraitId::courageous;
    d.title = "Courageous";
    d.trait_prereqs = {TraitId::cool_headed};
    set_trait_data(d);

    // --- Dexterous ---
    d.id = TraitId::dexterous;
    d.title = "Dexterous";
    d.descr = "+25% chance to evade attacks";
    set_trait_data(d);

    // --- Lithe ---
    d = trait_data(TraitId::dexterous);
    d.id = TraitId::lithe;
    d.title = "Lithe";
    d.trait_prereqs = {TraitId::dexterous};
    set_trait_data(d);

    // --- Crippling Strikes ---
    d.id = TraitId::crippling_strikes;
    d.title = "Crippling Strikes";
    d.descr =
        "Your melee attacks have 60% chance to weaken the target "
        "creature for 2-3 turns (reducing their melee damage by half)";
    d.trait_prereqs = {TraitId::dexterous, TraitId::adept_melee};
    d.bg_prereq = Bg::rogue;
    set_trait_data(d);

    // --- Fearless ---
    d.id = TraitId::fearless;
    d.title = "Fearless";
    d.descr = "You cannot become terrified, +10% mental shock resistance";
    d.on_picked = []() {
        prop::Prop* prop = prop::make(prop::Id::r_fear);

        prop->set_indefinite();

        map::g_player->m_properties.apply(prop, prop::PropSrc::intr, true, Verbose::no);
    };
    d.on_removed = []() {
        map::g_player->m_properties.end_prop(prop::Id::r_fear);
    };
    d.trait_prereqs = {TraitId::cool_headed};
    set_trait_data(d);

    // --- Stealthy ---
    d.id = TraitId::stealthy;
    d.title = "Stealthy";
    d.descr = "+45% chance to avoid detection by sight";
    set_trait_data(d);

    // --- Imperceptible ---
    d = trait_data(TraitId::stealthy);
    d.id = TraitId::imperceptible;
    d.title = "Imperceptible";
    d.trait_prereqs = {TraitId::stealthy};
    d.bg_prereq = Bg::rogue;
    set_trait_data(d);

    // --- Silent ---
    d.id = TraitId::silent;
    d.title = "Silent";
    d.descr =
        "All your melee attacks are silent (regardless of the weapon), "
        "and creatures are not alerted when you open or close doors, "
        "or wade through water";
    d.trait_prereqs = {TraitId::stealthy};
    set_trait_data(d);

    // --- Vigilant ---
    d.id = TraitId::vigilant;
    d.title = "Vigilant";
    d.descr = "You are always aware of nearby creatures";
    set_trait_data(d);

    // --- Treasure Hunter ---
    d.id = TraitId::treasure_hunter;
    d.title = "Treasure Hunter";
    d.descr = "You tend to find more items";
    d.blocked_for_bgs = {Bg::exorcist, Bg::ghoul, Bg::war_vet, Bg::flagellant};
    set_trait_data(d);

    // --- Self-aware ---
    d.id = TraitId::self_aware;
    d.title = "Self-aware";
    d.descr =
        "You cannot become confused, the number of remaining turns "
        "for status effects are displayed";
    d.on_picked = []() {
        prop::Prop* prop = prop::make(prop::Id::r_conf);

        prop->set_indefinite();

        map::g_player->m_properties.apply(
            prop,
            prop::PropSrc::intr,
            true,
            Verbose::no);
    };
    d.on_removed = []() {
        map::g_player->m_properties.end_prop(prop::Id::r_conf);
    };
    d.trait_prereqs = {TraitId::stout_spirit, TraitId::cool_headed};
    set_trait_data(d);

    // --- Healer ---
    d.id = TraitId::healer;
    d.title = "Healer";
    d.descr =
        "Using medical equipment requires only half the normal time "
        "and resources";
    d.blocked_for_bgs = {Bg::ghoul};
    set_trait_data(d);

    // --- Rapid Recoverer ---
    d.id = TraitId::rapid_recoverer;
    d.title = "Rapid Recoverer";
    d.descr = "You regenerate 1 hit point every third turn";
    d.trait_prereqs = {TraitId::tough, TraitId::healer};
    d.blocked_for_bgs = {Bg::ghoul};
    set_trait_data(d);

    // --- Survivalist ---
    d.id = TraitId::survivalist;
    d.title = "Survivalist";
    d.descr =
        "You cannot become diseased, "
        "only half your wounds count, "
        "rounded down "
        "(i.e. number of wounds are halved when calculating "
        "combat, hit point and regeneration penalties, "
        "slower walking speed happens at 6 wounds instead of 3, "
        "and you die from 10 wounds instead of 5)";
    d.on_picked = []() {
        prop::Prop* prop = prop::make(prop::Id::r_disease);

        prop->set_indefinite();

        map::g_player->m_properties.apply(
            prop,
            prop::PropSrc::intr,
            true,
            Verbose::no);
    };
    d.on_removed = []() {
        map::g_player->m_properties.end_prop(prop::Id::r_disease);
    };
    d.blocked_for_bgs = {Bg::ghoul, Bg::flagellant};
    set_trait_data(d);

    // --- Stout Spirit ---
    d.id = TraitId::stout_spirit;
    d.title = "Stout Spirit";
    d.descr =
        "+2 spirit points, increased spirit regeneration rate, you "
        "can defy harmful spells (it takes 125-150 turns to regain "
        "spell resistance after a spell is blocked)";
    d.on_picked = []() {
        prop::Prop* prop = prop::make(prop::Id::r_spell);

        prop->set_indefinite();

        map::g_player->m_properties.apply(prop, prop::PropSrc::intr, true, Verbose::no);

        const int spi_incr = 2;

        actor::change_max_sp(*map::g_player, spi_incr, Verbose::no);

        actor::restore_sp(*map::g_player, spi_incr, actor::AllowRestoreAboveMax::no, Verbose::no);
    };
    d.on_removed = []() {
        actor::change_max_sp(*map::g_player, -2, Verbose::no);
    };
    set_trait_data(d);

    // --- Strong Spirit ---
    d = trait_data(TraitId::stout_spirit);
    d.id = TraitId::strong_spirit;
    d.title = "Strong Spirit";
    d.descr =
        "+2 spirit points, increased spirit regeneration rate, it "
        "takes 75-100 turns to regain spell resistance after a spell "
        "is blocked";
    d.trait_prereqs = {TraitId::stout_spirit};
    set_trait_data(d);

    // --- Mighty Spirit ---
    d = trait_data(TraitId::stout_spirit);
    d.id = TraitId::mighty_spirit;
    d.title = "Mighty Spirit";
    d.descr =
        "+2 spirit points, increased spirit regeneration rate, it "
        "takes 25-50 turns to regain spell resistance after a spell "
        "is blocked";
    d.trait_prereqs = {TraitId::strong_spirit};
    set_trait_data(d);

    // --- Meditative ---
    d.id = TraitId::meditative;
    d.title = "Meditative";
    d.descr =
        "Applies a focused state which allows the next spell to be "
        "cast without spending a turn, and with the casting cost "
        "reduced by 1 point - it takes 125-150 turns to regain this "
        "state after a spell is cast";
    d.on_picked = []() {
        prop::Prop* prop = prop::make(prop::Id::meditative_focused);

        prop->set_indefinite();

        map::g_player->m_properties.apply(
            prop,
            prop::PropSrc::intr,
            true,
            Verbose::no);
    };
    d.trait_prereqs = {TraitId::strong_spirit, TraitId::cool_headed};
    d.blocked_for_bgs = {Bg::ghoul, Bg::war_vet, Bg::rogue};
    set_trait_data(d);

    // --- Sage ---
    d.id = TraitId::sage;
    d.title = "Sage";
    d.descr =
        "When focused, spells are cast without spending spirit points, "
        "and the duration to regain the focused state is reduced to "
        "75-100 turns";
    d.trait_prereqs = {TraitId::meditative};
    d.blocked_for_bgs = trait_data(TraitId::meditative).blocked_for_bgs;
    d.blocked_for_bgs.push_back(Bg::flagellant);
    set_trait_data(d);

    // --- Absorption ---
    d.id = TraitId::absorption;
    d.title = "Absorption";
    d.descr =
        "1-6 spirit points are restored each time Spell Shield is ended "
        "by a hostile spell "
        "(Spell Shield is granted by spirit traits or the Spell Shield spell)";
    d.trait_prereqs = {TraitId::strong_spirit};
    set_trait_data(d);

    // --- Tough ---
    d.id = TraitId::tough;
    d.title = "Tough";
    d.descr =
        "+6 hit points, "
        "+10% chance to resist burning, poisoning and paralysis, "
        "less likely to sprain when kicking, more likely to "
        "succeed with object interactions requiring strength (e.g. "
        "bashing things open)";
    d.on_picked = []() {
        const int hp_incr = 6;

        actor::change_max_hp(*map::g_player, hp_incr, Verbose::no);

        actor::restore_hp(
            *map::g_player,
            hp_incr,
            actor::AllowRestoreAboveMax::no,
            Verbose::no);
    };
    d.on_removed = []() {
        actor::change_max_hp(*map::g_player, -6, Verbose::no);
    };
    set_trait_data(d);

    // --- Rugged ---
    d = trait_data(TraitId::tough);
    d.id = TraitId::rugged;
    d.title = "Rugged";
    d.trait_prereqs = {TraitId::tough};
    set_trait_data(d);

    // --- Unbreakable ---
    d = trait_data(TraitId::rugged);
    d.id = TraitId::unbreakable;
    d.title = "Unbreakable";
    d.bg_prereq = Bg::flagellant;
    d.trait_prereqs = {TraitId::rugged};
    set_trait_data(d);

    // --- Thick Skinned ---
    d.id = TraitId::thick_skinned;
    d.title = "Thick Skinned";
    d.descr = "+1 armor point (physical damage reduced by 1 point)";
    d.trait_prereqs = {TraitId::tough};
    set_trait_data(d);

    // --- Callous ---
    d = trait_data(TraitId::thick_skinned);
    d.id = TraitId::callous;
    d.title = "Callous";
    d.bg_prereq = Bg::flagellant;
    d.trait_prereqs = {TraitId::thick_skinned};
    set_trait_data(d);

    // --- Resistant ---
    d.id = TraitId::resistant;
    d.title = "Resistant";
    d.descr =
        "+25% chance to resist burning, poisoning and paralysis - "
        "and the duration of those effects is halved";
    d.trait_prereqs = {TraitId::tough};
    set_trait_data(d);

    // --- Strong-backed ---
    d.id = TraitId::strong_backed;
    d.title = "Strong-backed";
    d.descr = "+50% carry weight limit";
    d.trait_prereqs = {TraitId::tough};
    set_trait_data(d);

    // --- Bane of the Undead ---
    d.id = TraitId::undead_bane;
    d.title = "Bane of the Undead";
    d.descr =
        "+2 melee and ranged attack damage against all undead "
        "monsters, +50% hit chance against ethereal undead monsters";
    d.trait_prereqs = {TraitId::tough, TraitId::fearless, TraitId::stout_spirit};
    set_trait_data(d);

    // --- Electrically Inclined ---
    d.id = TraitId::elec_incl;
    d.title = "Electrically Inclined";
    d.descr =
        "Rods recharge twice as fast, strange devices are less likely "
        "to malfunction or break, electric lanterns last twice as "
        "long, +1 damage with electricity weapons";
    d.blocked_for_bgs = {Bg::ghoul};
    set_trait_data(d);

    // -- Adept of Channeling ---
    d.id = TraitId::adept_of_channeling;
    d.title = "Adept of Channeling";
    d.descr =
        "Specialize in the channeling of violent energy. "
        "Channeling spells are cast at a higher skill level.";
    d.bg_prereq = Bg::occultist;
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_1;
    d.special_prereq = {
        []() { return knows_spell_with_domain(SpellDomain::channeling); },
        "Know any channeling spell"};
    d.on_picked = []() { incr_spell_skills(SpellDomain::channeling); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::channeling); };
    set_trait_data(d);

    // -- Master of Channeling ---
    d.id = TraitId::master_of_channeling;
    d.title = "Master of Channeling";
    d.descr =
        "Attain mastery over the channeling of violent energy. "
        "Channeling spells are cast at a higher skill level.";
    d.bg_prereq = Bg::occultist;
    d.trait_prereqs = {TraitId::adept_of_channeling};
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_2;
    d.on_picked = []() { incr_spell_skills(SpellDomain::channeling); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::channeling); };
    set_trait_data(d);

    // -- Adept of Corruption ---
    d.id = TraitId::adept_of_corruption;
    d.title = "Adept of Corruption";
    d.descr =
        "Specialize in corruption and withering. "
        "Corruption spells are cast at a higher skill level.";
    d.bg_prereq = Bg::occultist;
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_1;
    d.special_prereq = {
        []() { return knows_spell_with_domain(SpellDomain::corruption); },
        "Know any corruption spell"};
    d.on_picked = []() { incr_spell_skills(SpellDomain::corruption); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::corruption); };
    set_trait_data(d);

    // -- Master of Corruption ---
    d.id = TraitId::master_of_corruption;
    d.title = "Master of Corruption";
    d.descr =
        "Attain mastery over corruption and withering. "
        "Corruption spells are cast at a higher skill level.";
    d.bg_prereq = Bg::occultist;
    d.trait_prereqs = {TraitId::adept_of_corruption};
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_2;
    d.on_picked = []() { incr_spell_skills(SpellDomain::corruption); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::corruption); };
    set_trait_data(d);

    // -- Adept of Illusion ---
    d.id = TraitId::adept_of_illusion;
    d.title = "Adept of Illusion";
    d.descr =
        "Specialize in the casting of illusions. "
        "Illusion spells are cast at a higher skill level.";
    d.bg_prereq = Bg::occultist;
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_1;
    d.special_prereq = {
        []() { return knows_spell_with_domain(SpellDomain::illusion); },
        "Know any illusion spell"};
    d.on_picked = []() { incr_spell_skills(SpellDomain::illusion); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::illusion); };
    set_trait_data(d);

    // -- Master of Illusion ---
    d.id = TraitId::master_of_illusion;
    d.title = "Master of Illusion";
    d.descr =
        "Attain mastery over the casting of illusions. "
        "Illusion spells are cast at a higher skill level.";
    d.bg_prereq = Bg::occultist;
    d.trait_prereqs = {TraitId::adept_of_illusion};
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_2;
    d.on_picked = []() { incr_spell_skills(SpellDomain::illusion); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::illusion); };
    set_trait_data(d);

    // -- Adept of The_mind ---
    d.id = TraitId::adept_of_the_mind;
    d.title = "Adept of the Mind";
    d.descr =
        "Specialize in knowledge, foresight, and will. "
        "Mind spells are cast at a higher skill level.";
    d.bg_prereq = Bg::occultist;
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_1;
    d.special_prereq = {
        []() { return knows_spell_with_domain(SpellDomain::mind); },
        "Know any mind spell"};
    d.on_picked = []() { incr_spell_skills(SpellDomain::mind); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::mind); };
    set_trait_data(d);

    // -- Master of The_mind ---
    d.id = TraitId::master_of_the_mind;
    d.title = "Master of the Mind";
    d.descr =
        "Attain mastery over knowledge, foresight, and will. "
        "Mind spells are cast at a higher skill level, "
        "and you also sense items and creatures.";
    d.bg_prereq = Bg::occultist;
    d.trait_prereqs = {TraitId::adept_of_the_mind};
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_2;
    d.on_picked = []() { incr_spell_skills(SpellDomain::mind); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::mind); };
    set_trait_data(d);

    // -- Adept of Time ---
    d.id = TraitId::adept_of_time;
    d.title = "Adept of Time";
    d.descr =
        "Specialize in the manipulation of time and causality. "
        "Time spells are cast at a higher skill level.";
    d.bg_prereq = Bg::occultist;
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_1;
    d.special_prereq = {
        []() { return knows_spell_with_domain(SpellDomain::time); },
        "Know any time spell"};
    d.on_picked = []() { incr_spell_skills(SpellDomain::time); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::time); };
    set_trait_data(d);

    // -- Master of Time ---
    d.id = TraitId::master_of_time;
    d.title = "Master of Time";
    d.descr =
        "Attain mastery over the manipulation of time and causality. "
        "Time spells are cast at a higher skill level.";
    d.bg_prereq = Bg::occultist;
    d.trait_prereqs = {TraitId::adept_of_time};
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_2;
    d.on_picked = []() { incr_spell_skills(SpellDomain::time); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::time); };
    set_trait_data(d);

    // -- Adept of Warding ---
    d.id = TraitId::adept_of_warding;
    d.title = "Adept of Warding";
    d.descr =
        "Specialize in protective magic. "
        "Warding spells are cast at a higher skill level.";
    d.bg_prereq = Bg::occultist;
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_1;
    d.special_prereq = {
        []() { return knows_spell_with_domain(SpellDomain::warding); },
        "Know any warding spell"};
    d.on_picked = []() { incr_spell_skills(SpellDomain::warding); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::warding); };
    set_trait_data(d);

    // -- Master of Warding ---
    d.id = TraitId::master_of_warding;
    d.title = "Master of Warding";
    d.descr =
        "Attain mastery over protective magic. "
        "Warding spells are cast at a higher skill level.";
    d.bg_prereq = Bg::occultist;
    d.trait_prereqs = {TraitId::adept_of_warding};
    d.clvl_prereq = s_occultist_spell_upgrade_lvl_2;
    d.on_picked = []() { incr_spell_skills(SpellDomain::warding); };
    d.on_removed = []() { decr_spell_skills(SpellDomain::warding); };
    set_trait_data(d);

    // -- Benediction I --
    d.id = TraitId::benediction_i;
    d.title = "Benediction I";
    d.descr = trait_descr_for_spells(
        {
            SpellId::bless,
            SpellId::heal,
        },
        SpellSkill::basic);
    d.extra_descr_when_picking = get_player_available_sp_str();
    d.on_picked = []() {
        player_spells::learn_spell(SpellId::bless, Verbose::no);
        player_spells::learn_spell(SpellId::heal, Verbose::no);
    };
    d.on_removed = []() {
        player_spells::remove_learned_spell(SpellId::bless);
        player_spells::remove_learned_spell(SpellId::heal);
    };
    d.bg_prereq = Bg::exorcist;
    set_trait_data(d);

    // -- Benediction II --
    d.id = TraitId::benediction_ii;
    d.title = "Benediction II";
    d.descr = trait_descr_for_spells(
        {
            SpellId::bless,
            SpellId::heal,
        },
        SpellSkill::expert);
    d.extra_descr_when_picking = get_player_available_sp_str();
    d.on_picked = []() {
        player_spells::incr_spell_skill(SpellId::bless, Verbose::no);
        player_spells::incr_spell_skill(SpellId::heal, Verbose::no);
    };
    d.on_removed = []() {
        player_spells::set_spell_skill(SpellId::bless, SpellSkill::basic);
        player_spells::set_spell_skill(SpellId::heal, SpellSkill::basic);
    };
    d.trait_prereqs = {TraitId::benediction_i};
    d.bg_prereq = Bg::exorcist;
    set_trait_data(d);

    // -- Judgment I --
    d.id = TraitId::judgment_i;
    d.title = "Judgment I";
    d.descr = trait_descr_for_spells(
        {
            SpellId::cleansing_fire,
        },
        SpellSkill::basic);
    d.extra_descr_when_picking = get_player_available_sp_str();
    d.on_picked = []() {
        player_spells::learn_spell(SpellId::cleansing_fire, Verbose::no);
    };
    d.on_removed = []() {
        player_spells::remove_learned_spell(SpellId::cleansing_fire);
    };
    d.bg_prereq = Bg::exorcist;
    set_trait_data(d);

    // -- Judgment II --
    d.id = TraitId::judgment_ii;
    d.title = "Judgment II";
    d.descr = trait_descr_for_spells(
        {
            SpellId::cleansing_fire,
        },
        SpellSkill::expert);
    d.extra_descr_when_picking = get_player_available_sp_str();
    d.on_picked = []() {
        player_spells::incr_spell_skill(SpellId::cleansing_fire, Verbose::no);
    };
    d.on_removed = []() {
        player_spells::set_spell_skill(SpellId::cleansing_fire, SpellSkill::basic);
    };
    d.trait_prereqs = {TraitId::judgment_i};
    d.bg_prereq = Bg::exorcist;
    set_trait_data(d);

    // -- Light I --
    d.id = TraitId::light_i;
    d.title = "Light I";
    d.descr = trait_descr_for_spells(
        {
            SpellId::brilliance,
            SpellId::light,
            SpellId::see_invis,
        },
        SpellSkill::basic);
    d.extra_descr_when_picking = get_player_available_sp_str();
    d.on_picked = []() {
        player_spells::learn_spell(SpellId::brilliance, Verbose::no);
        player_spells::learn_spell(SpellId::light, Verbose::no);
        player_spells::learn_spell(SpellId::see_invis, Verbose::no);
    };
    d.on_removed = []() {
        player_spells::remove_learned_spell(SpellId::brilliance);
        player_spells::remove_learned_spell(SpellId::light);
        player_spells::remove_learned_spell(SpellId::see_invis);
    };
    d.bg_prereq = Bg::exorcist;
    set_trait_data(d);

    // -- Light II --
    d.id = TraitId::light_ii;
    d.title = "Light II";
    d.descr = trait_descr_for_spells(
        {
            SpellId::brilliance,
            SpellId::light,
            SpellId::see_invis,
        },
        SpellSkill::expert);
    d.extra_descr_when_picking = get_player_available_sp_str();
    d.on_picked = []() {
        player_spells::incr_spell_skill(SpellId::brilliance, Verbose::no);
        player_spells::incr_spell_skill(SpellId::light, Verbose::no);
        player_spells::incr_spell_skill(SpellId::see_invis, Verbose::no);
    };
    d.on_removed = []() {
        player_spells::set_spell_skill(SpellId::brilliance, SpellSkill::basic);
        player_spells::set_spell_skill(SpellId::light, SpellSkill::basic);
        player_spells::set_spell_skill(SpellId::see_invis, SpellSkill::basic);
    };
    d.trait_prereqs = {TraitId::light_i};
    d.bg_prereq = Bg::exorcist;
    set_trait_data(d);

    // -- Sanctity I --
    d.id = TraitId::sanctity_i;
    d.title = "Sanctity I";
    d.descr = trait_descr_for_spells(
        {
            SpellId::cancellation,
            SpellId::sanctuary,
        },
        SpellSkill::basic);
    d.extra_descr_when_picking = get_player_available_sp_str();
    d.on_picked = []() {
        player_spells::learn_spell(SpellId::cancellation, Verbose::no);
        player_spells::learn_spell(SpellId::sanctuary, Verbose::no);
    };
    d.on_removed = []() {
        player_spells::remove_learned_spell(SpellId::cancellation);
        player_spells::remove_learned_spell(SpellId::sanctuary);
    };
    d.bg_prereq = Bg::exorcist;
    set_trait_data(d);

    // -- Sanctity II --
    d.id = TraitId::sanctity_ii;
    d.title = "Sanctity II";
    d.descr = trait_descr_for_spells(
        {
            SpellId::cancellation,
            SpellId::sanctuary,
        },
        SpellSkill::expert);
    d.extra_descr_when_picking = get_player_available_sp_str();
    d.on_picked = []() {
        player_spells::incr_spell_skill(SpellId::cancellation, Verbose::no);
        player_spells::incr_spell_skill(SpellId::sanctuary, Verbose::no);
    };
    d.on_removed = []() {
        player_spells::set_spell_skill(SpellId::cancellation, SpellSkill::basic);
        player_spells::set_spell_skill(SpellId::sanctuary, SpellSkill::basic);
    };
    d.trait_prereqs = {TraitId::sanctity_i};
    d.bg_prereq = Bg::exorcist;
    set_trait_data(d);

    // --- Prolonged Life ---
    d.id = TraitId::prolonged_life;
    d.title = "Prolonged Life";
    d.descr = "Any fatal damage received is instead drained from your fervor points";
    d.bg_prereq = Bg::exorcist;
    set_trait_data(d);

    // --- Ravenous ---
    d.id = TraitId::ravenous;
    d.title = "Ravenous";
    d.descr = "You occasionally feed on living victims when attacking with claws";
    d.trait_prereqs = {TraitId::adept_melee};
    d.bg_prereq = Bg::ghoul;
    set_trait_data(d);

    // --- Foul ---
    d.id = TraitId::foul;
    d.title = "Foul";
    d.descr =
        "+1 claw damage, when attacking with claws, vicious worms "
        "occasionally burst out from the corpses of your victims to "
        "attack your enemies";
    d.bg_prereq = Bg::ghoul;
    set_trait_data(d);

    // --- Toxic ---
    d.id = TraitId::toxic;
    d.title = "Toxic";
    d.descr =
        "+1 claw damage, you are immune to poison, and attacks with "
        "your claws often poisons your victims";
    d.on_picked = []() {
        prop::Prop* prop = prop::make(prop::Id::r_poison);

        prop->set_indefinite();

        map::g_player->m_properties.apply(
            prop,
            prop::PropSrc::intr,
            true,
            Verbose::no);
    };
    d.on_removed = []() {
        map::g_player->m_properties.end_prop(prop::Id::r_poison);
    };
    d.trait_prereqs = {TraitId::foul};
    d.bg_prereq = Bg::ghoul;
    set_trait_data(d);

    // --- Indomitable Fury ---
    d.id = TraitId::indomitable_fury;
    d.title = "Indomitable Fury";
    d.descr = "While frenzied, you are immune to wounds, and your claw attacks cause fear";
    d.trait_prereqs = {TraitId::adept_melee, TraitId::tough};
    d.bg_prereq = Bg::ghoul;
    set_trait_data(d);

    // --- Elusive ---
    d.id = TraitId::elusive;
    d.title = "Elusive";
    d.descr = "Creatures only remember you for half the normal duration (rounded up).";
    d.bg_prereq = Bg::rogue;
    set_trait_data(d);

    // --- Vicious ---
    d.id = TraitId::vicious;
    d.title = "Vicious";
    d.descr = "+100% backstab damage (in addition to the normal +50%)";
    d.trait_prereqs = {TraitId::stealthy, TraitId::dexterous};
    d.bg_prereq = Bg::rogue;
    set_trait_data(d);

    // --- Ruthless ---
    d.id = TraitId::ruthless;
    d.title = "Ruthless";
    d.descr = "+100% backstab damage";
    d.trait_prereqs = {TraitId::vicious};
    d.bg_prereq = Bg::rogue;
    set_trait_data(d);

    // --- Steady Aimer ---
    d.id = TraitId::steady_aimer;
    d.title = "Steady Aimer";
    d.descr =
        "Standing still gives ranged attacks maximum damage and +10% "
        "hit chance on the following turn, unless damage is taken";
    d.bg_prereq = Bg::war_vet;
    set_trait_data(d);

    // --- Galvanization ---
    d.id = TraitId::galvanization;
    d.title = "Galvanization";
    d.descr =
        "Casting any spell from the Blood domain grants "
        "Regeneration for 4-6 turns "
        "(+1 extra hit point regenerated per turn), if "
        "hit points are lost from casting the spell";
    d.bg_prereq = Bg::flagellant;
    set_trait_data(d);

    d.id = TraitId::enthusiasm;
    d.title = "Enthusiasm";
    d.descr = "Doubles all bonuses for the moribund effect";
    d.bg_prereq = Bg::flagellant;
    set_trait_data(d);

    // --- Memento Mori ---
    d.id = TraitId::memento_mori;
    d.title = "Memento Mori";
    d.descr =
        "Raises the threshold of the moribund status to 8 hit points, "
        "and increases the duration of the effect by 50% (rounded down)";
    d.bg_prereq = Bg::flagellant;
    set_trait_data(d);
}

static bool is_trait_blocked_for_bg(const TraitId trait, const Bg bg)
{
    const auto d = trait_data(trait);

    const bool is_blocked_for_bg =
        std::find(
            std::begin(d.blocked_for_bgs),
            std::end(d.blocked_for_bgs),
            bg) != std::end(d.blocked_for_bgs);

    return is_blocked_for_bg;
}

static bool is_flagellant_spell_upgrade_clvl(const int clvl)
{
    return (
        (clvl == s_flagellant_spell_upgrade_lvl_1) ||
        (clvl == s_flagellant_spell_upgrade_lvl_2));
}

// -----------------------------------------------------------------------------
// player_bon
// -----------------------------------------------------------------------------
namespace player_bon
{
void init()
{
    s_player_bg = Bg::END;

    s_player_occultist_domain = SpellDomain::END;

    for (size_t i = 0; i < (size_t)TraitId::END; ++i) {
        s_traits_picked[i] = false;
    }

    update_trait_data();

    s_trait_log.clear();
}

void save()
{
    saving::put_int((int)s_player_bg);

    saving::put_int((int)s_player_occultist_domain);

    for (size_t i = 0; i < (size_t)TraitId::END; ++i) {
        saving::put_bool(s_traits_picked[i]);
    }

    saving::put_int((int)s_trait_log.size());

    for (const TraitLogEntry& e : s_trait_log) {
        saving::put_int(e.clvl);

        saving::put_int((int)e.trait_id);

        saving::put_bool(e.is_removal);
    }
}

void load()
{
    s_player_bg = (Bg)saving::get_int();

    s_player_occultist_domain = (SpellDomain)saving::get_int();

    for (size_t i = 0; i < (size_t)TraitId::END; ++i) {
        s_traits_picked[i] = saving::get_bool();
    }

    const int nr_trait_log_entries = saving::get_int();

    s_trait_log.resize(nr_trait_log_entries);

    for (player_bon::TraitLogEntry& e : s_trait_log) {
        e.clvl = saving::get_int();

        e.trait_id = (TraitId)saving::get_int();

        e.is_removal = saving::get_bool();
    }
}

std::string bg_title(const Bg id)
{
    switch (id) {
    case Bg::exorcist:
        return "Exorcist";

    case Bg::flagellant:
        return "Flagellant";

    case Bg::ghoul:
        return "Ghoul";

    case Bg::occultist:
        return "Occultist";

    case Bg::rogue:
        return "Rogue";

    case Bg::war_vet:
        return "War Veteran";

    case Bg::END:
        break;
    }

    ASSERT(false);

    return "";
}

std::string trait_title(const TraitId id)
{
    return trait_data(id).title;
}

std::vector<ColoredString> bg_descr(const Bg id)
{
    std::vector<ColoredString> descr;

    auto put = [&descr](const std::string& str) {
        descr.emplace_back(str, colors::text());
    };

    auto put_trait = [&descr](const TraitId trait_id) {
        const auto t = trait_title(trait_id);
        const auto d = trait_descr(trait_id);

        descr.emplace_back("{COLOR_WHITE}" + t + "{color_reset}: " + d, colors::gray());
    };

    switch (id) {
    case Bg::exorcist:
        put("Cannot use manuscripts, altars, monoliths, or gongs, "
            "but instead gains experience and fervor for destroying "
            "these (manuscripts are destroyed when picking them up). "
            "Fervor can be used for casting spells - these points "
            "are used automatically when there is not enough "
            "spirit points to cast from.");
        put("");
        put("Starts with a Holy Symbol, which can restore "
            "spirit points and grant resistance against "
            "mental shock and fear.");
        put("");
        put(exorcist_extra_traits_descr());
        put("");
        put_trait(TraitId::stout_spirit);
        put("");
        put_trait(TraitId::undead_bane);
        break;

    case Bg::flagellant:
        put("No mental shock received for taking damage.");
        put("");
        put("If health is reduced to 6 hit points or below when taking damage, "
            "the moribund status is applied for 5-7 turns "
            "(+3 melee damage, +30% melee hit chance, +3 armor points).");
        put("");
        put("Wears a torture collar which cannot be taken off; "
            "walking requires extra turns, and stealth and evasion "
            "are reduced by 20%. However, wearing the collar hardens "
            "the Flagellant against physical suffering, armor is "
            "increased by 3 points.");
        put("");
        put("Specializes in spells belonging to the Blood domain. "
            "At character levels " +
            std::to_string(s_flagellant_spell_upgrade_lvl_1) +
            " and " +
            std::to_string(s_flagellant_spell_upgrade_lvl_2) +
            ", all spells belonging to this domain are cast at "
            "a higher skill level.");
        put("");
        put("-25% mental shock taken from casting memorized spells "
            "from the Blood domain.");
        put("");
        put_trait(TraitId::self_aware);
        put("");
        put_trait(TraitId::tough);
        break;

    case Bg::ghoul:
        put("-50% mental shock taken from seeing monsters and "
            "standing in darkness - "
            "but also only gains halved shock reduction from light.");
        put("");
        put("Does not regenerate hit points and cannot use medical equipment - "
            "instead heals by feeding on corpses "
            "(feeding is done by waiting on a corpse).");
        put("");
        put("Can incite frenzy at will, and does not become weakened "
            "when frenzy ends.");
        put("");
        put("+8 hit points.");
        put("");
        put("Is immune to disease and infections.");
        put("");
        put("Does not get sprains.");
        put("");
        put("Can see in darkness.");
        put("");
        put("-15% hit chance with firearms and thrown weapons.");
        put("");
        put("All ghouls are allied.");
        break;

    case Bg::occultist:
        put("-50% mental shock taken from casting memorized spells "
            "and from using or identifying strange items such as "
            "potions or manuscripts "
            "(in addition to \"Cool-headed\").");
        put("");
        put("Can gain traits to increase skill level in various spell domains.");
        put("");
        put("Chooses background in a specific spell domain at character creation, "
            "which determines starting spells.");
        put("");
        put("+3 spirit points (in addition to \"Stout Spirit\").");
        put("");
        put("Starts with several Bone Charms, that can be used for "
            "gaining spell resistance or dispelling sigils "
            ""
            "(\"strange shape\" on the floor).");

        put("");
        put_trait(TraitId::stout_spirit);
        put("");
        put_trait(TraitId::cool_headed);
        break;

    case Bg::rogue:
        put("Mental shock received passively over time is reduced by 25%.");
        put("");
        put("+10% chance to spot hidden monsters, doors, and traps.");
        put("");
        put("Remains aware of the presence of other creatures longer.");
        put("");
        put("Can sense the presence of unique monsters or powerful "
            "artifacts.");
        put("");
        put("Has acquired an artifact which can cloud the minds of all "
            "enemies, causing them to forget the presence of the "
            "user.");
        put("");
        put_trait(TraitId::stealthy);
        break;

    case Bg::war_vet:
        put("Switches to prepared weapon instantly.");
        put("");
        put("Starts with a Flak Jacket.");
        put("");
        put("Maintains armor twice as long before it breaks.");
        put("");
        put_trait(TraitId::adept_marksman);
        put("");
        put_trait(TraitId::adept_melee);
        put("");
        put_trait(TraitId::tough);
        put("");
        put_trait(TraitId::healer);
        break;

    case Bg::END:
        ASSERT(false);
        break;
    }

    return descr;
}

std::string occultist_domain_descr(const SpellDomain domain)
{
    const std::vector<SpellId> spell_ids = occultist_domian_starting_spells(domain);

    std::vector<std::string> spell_names;

    for (const SpellId id : spell_ids) {
        const std::unique_ptr<Spell> tmp_spell(spells::make(id));

        spell_names.push_back(tmp_spell->name());
    }

    const std::string spell_list_str = text_format::make_comma_and_str(spell_names);

    switch (domain) {
    case SpellDomain::channeling:
        return (
            "You have previously dabbled in the channeling of violent energy, "
            "and have basic knowledge of " +
            spell_list_str + ".");

    case SpellDomain::corruption:
        // NOTE: The phrasing here match the Corruption domain starting spells Aura of Decay
        // and Curse:
        return (
            "You have previously dabbled in spells that wither and corrupt, "
            "and have basic knowledge of " +
            spell_list_str + ".");

    case SpellDomain::illusion:
        return (
            "You have previously dabbled in the casting of illusions, "
            "and have basic knowledge of " +
            spell_list_str + ".");

    case SpellDomain::mind:
        return (
            "You have previously dabbled in disciplines of revelation, foresight, "
            "and will, "
            "and have basic knowledge of " +
            spell_list_str + ".");

    case SpellDomain::time:
        return (
            "You have previously dabbled in the manipulation of time and causality, "
            "and have basic knowledge of " +
            spell_list_str + ".");

    case SpellDomain::warding:
        return (
            "You have previously dabbled in protective magic, "
            "and have basic knowledge of " +
            spell_list_str + ".");

    case SpellDomain::blood:
    case SpellDomain::END:
        ASSERT(false);
        break;
    }

    return "";
}

std::vector<SpellId> occultist_domian_starting_spells(const SpellDomain domain)
{
    switch (domain) {
    case SpellDomain::channeling:
        return {SpellId::darkbolt, SpellId::gnawing_torrent};

    case SpellDomain::corruption:
        return {SpellId::enfeeble, SpellId::aura_of_decay};

    case SpellDomain::illusion:
        return {SpellId::mirror_images, SpellId::terrify};

    case SpellDomain::mind:
        return {SpellId::premonition, SpellId::clairvoyance};

    case SpellDomain::time:
        return {SpellId::temporal_echo, SpellId::expulsion};

    case SpellDomain::warding:
        return {SpellId::heal, SpellId::inscribe_boundary_sigil};

    case SpellDomain::blood:
    case SpellDomain::END:
        ASSERT(false);
        break;
    }

    return {};
}

std::string trait_descr(const TraitId id)
{
    return trait_data(id).descr;
}

std::string trait_descr_extra_when_picking(const TraitId id)
{
    return trait_data(id).extra_descr_when_picking;
}

TraitPrereqData trait_prereqs(const TraitId trait, const Bg bg)
{
    const auto& d = trait_data(trait);

    TraitPrereqData result;

    result.clvl = d.clvl_prereq;
    result.traits = d.trait_prereqs;
    result.bg = d.bg_prereq;
    result.special = d.special_prereq;

    // Remove traits which are blocked for this background (prerequisites are considered
    // fulfilled).
    for (auto it = std::begin(result.traits);
         it != std::end(result.traits);) {
        if (is_trait_blocked_for_bg(*it, bg)) {
            it = result.traits.erase(it);
        }
        else {
            // Not blocked
            ++it;
        }
    }

    // Sort traits lexicographically.
    std::sort(
        std::begin(result.traits),
        std::end(result.traits),
        [](const TraitId& t1, const TraitId& t2) {
            const std::string str1 = trait_title(t1);
            const std::string str2 = trait_title(t2);
            return str1 < str2;
        });

    return result;
}

Bg bg()
{
    return s_player_bg;
}

SpellDomain occultist_starting_domain()
{
    return s_player_occultist_domain;
}

bool is_bg(Bg bg)
{
    ASSERT(bg != Bg::END);

    return bg == s_player_bg;
}

bool has_trait(const TraitId id)
{
    return s_traits_picked[(size_t)id];
}

std::vector<Bg> pickable_bgs()
{
    std::vector<Bg> result;

    result.reserve((int)Bg::END);

    for (int i = 0; i < (int)Bg::END; ++i) {
        result.push_back((Bg)i);
    }

    // Sort lexicographically.
    std::sort(
        std::begin(result),
        std::end(result),
        [](const Bg bg1, const Bg bg2) {
            const std::string str1 = bg_title(bg1);
            const std::string str2 = bg_title(bg2);
            return str1 < str2;
        });

    return result;
}

std::vector<SpellDomain> pickable_occultist_domains()
{
    std::vector<SpellDomain> result = {
        SpellDomain::channeling,
        SpellDomain::corruption,
        SpellDomain::illusion,
        SpellDomain::mind,
        SpellDomain::time,
        SpellDomain::warding,
    };

    // Sort lexicographically.
    std::sort(
        std::begin(result),
        std::end(result),
        [](const SpellDomain domain_1, const SpellDomain domain_2) {
            const std::string str1 = spells::spell_domain_title(domain_1);
            const std::string str2 = spells::spell_domain_title(domain_2);

            return str1 < str2;
        });

    return result;
}

UnpickedTraitsData unpicked_traits(const Bg bg)
{
    update_trait_data();

    UnpickedTraitsData result;

    for (const TraitData& d : s_trait_data) {
        if (s_traits_picked[(size_t)d.id]) {
            continue;
        }

        // Check if trait is explicitly blocked for this background.
        const bool is_blocked_for_bg = is_trait_blocked_for_bg(d.id, bg);

        if (is_blocked_for_bg) {
            continue;
        }

        // Check trait prerequisites (character level, traits and background).

        // NOTE: Traits blocked for the current background are not considered prerequisites.
        const auto prereq_data = trait_prereqs(d.id, bg);

        const bool is_bg_ok =
            (s_player_bg == prereq_data.bg) ||
            (prereq_data.bg == Bg::END);

        if (!is_bg_ok) {
            // Trait not available for this background - don't include it as an
            // "unpicked trait" (it will never become available).
            continue;
        }

        // OK the trait *could* be picked eventually.

        const bool is_clvl_ok = game::clvl() >= prereq_data.clvl;

        const bool is_trait_prereqs_ok = std::all_of(
            std::begin(prereq_data.traits),
            std::end(prereq_data.traits),
            [](TraitId prereq_id) { return s_traits_picked[(size_t)prereq_id]; });

        const bool is_special_req_ok =
            !prereq_data.special ||
            prereq_data.special.value().req();

        if (is_clvl_ok && is_trait_prereqs_ok && is_special_req_ok) {
            result.traits_can_be_picked.push_back(d.id);
        }
        else {
            result.traits_prereqs_not_met.push_back(d.id);
        }

    }  // Trait loop

    // Sort lexicographically
    std::sort(
        std::begin(result.traits_can_be_picked),
        std::end(result.traits_can_be_picked),
        [](const TraitId& t1, const TraitId& t2) {
            const std::string str1 = trait_title(t1);
            const std::string str2 = trait_title(t2);
            return str1 < str2;
        });

    std::sort(
        std::begin(result.traits_prereqs_not_met),
        std::end(result.traits_prereqs_not_met),
        [](const TraitId& t1, const TraitId& t2) {
            const std::string str1 = trait_title(t1);
            const std::string str2 = trait_title(t2);
            return str1 < str2;
        });

    return result;
}  // unpicked_traits

std::vector<TraitId> traits_can_be_removed()
{
    update_trait_data();

    std::vector<TraitId> result;

    for (const auto& d : s_trait_data) {
        if (!s_traits_picked[(size_t)d.id]) {
            continue;
        }

        bool is_prereq_for_other_trait = false;

        for (const auto& d_other : s_trait_data) {
            if (!s_traits_picked[(size_t)d_other.id]) {
                continue;
            }

            const auto match =
                std::find(
                    std::begin(d_other.trait_prereqs),
                    std::end(d_other.trait_prereqs),
                    d.id);

            if (match != std::end(d_other.trait_prereqs)) {
                is_prereq_for_other_trait = true;
                break;
            }
        }

        if (is_prereq_for_other_trait) {
            continue;
        }

        result.push_back(d.id);
    }

    return result;
}

void pick_bg(const Bg bg)
{
    TRACE_FUNC_BEGIN;

    ASSERT(bg != Bg::END);

    s_player_bg = bg;

    switch (s_player_bg) {
    case Bg::exorcist: {
        pick_trait(TraitId::stout_spirit);
        pick_trait(TraitId::undead_bane);

        // Mark all scrolls as found, so that they do not yield XP.
        for (auto& d : item::g_data) {
            if (d.type == ItemType::scroll) {
                d.is_found = true;
            }
        }
    } break;

    case Bg::flagellant: {
        pick_trait(TraitId::self_aware);
        pick_trait(TraitId::tough);

        prop::Prop* flagellant_prop = prop::make(prop::Id::flagellant);

        flagellant_prop->set_indefinite();

        map::g_player->m_properties.apply(
            flagellant_prop,
            prop::PropSrc::intr,
            true,
            Verbose::no);
    } break;

    case Bg::ghoul: {
        prop::Prop* r_disease = prop::make(prop::Id::r_disease);

        r_disease->set_indefinite();

        map::g_player->m_properties.apply(
            r_disease,
            prop::PropSrc::intr,
            true,
            Verbose::no);

        prop::Prop* darkvis = prop::make(prop::Id::darkvision);

        darkvis->set_indefinite();

        map::g_player->m_properties.apply(
            darkvis,
            prop::PropSrc::intr,
            true,
            Verbose::no);

        player_spells::learn_spell(SpellId::frenzy, Verbose::no);

        actor::change_max_hp(*map::g_player, 8, Verbose::no);
    } break;

    case Bg::occultist: {
        pick_trait(TraitId::stout_spirit);
        pick_trait(TraitId::cool_headed);

        actor::change_max_sp(*map::g_player, 3, Verbose::no);
    } break;

    case Bg::rogue: {
        pick_trait(TraitId::stealthy);
    } break;

    case Bg::war_vet: {
        pick_trait(TraitId::adept_marksman);
        pick_trait(TraitId::adept_melee);
        pick_trait(TraitId::tough);
        pick_trait(TraitId::healer);
    } break;

    case Bg::END:
        break;
    }

    TRACE_FUNC_END;
}

void pick_occultist_domain(const SpellDomain domain)
{
    ASSERT(domain != SpellDomain::blood);
    ASSERT(domain != SpellDomain::END);

    s_player_occultist_domain = domain;
}

void on_player_gained_lvl(const int new_lvl)
{
    TRACE_FUNC_BEGIN;

    switch (s_player_bg) {
    case Bg::exorcist: {
        const bool is_exorcist_extra_trait =
            std::find(
                std::begin(s_exorcist_extra_trait_lvls),
                std::end(s_exorcist_extra_trait_lvls),
                new_lvl) != std::end(s_exorcist_extra_trait_lvls);

        if (is_exorcist_extra_trait) {
            states::push(
                std::make_unique<PickTraitState>(
                    "You gain an extra trait!",
                    IsCharacterCreationTraitPick::no));
        }
    } break;

    case Bg::flagellant: {
        if (is_flagellant_spell_upgrade_clvl(new_lvl)) {
            incr_spell_skills(SpellDomain::blood);
        }
    } break;

    case Bg::ghoul:
    case Bg::occultist:
    case Bg::rogue:
    case Bg::war_vet:   {
    } break;

    case Bg::END: {
        ASSERT(false);
    } break;
    }

    TRACE_FUNC_END;
}

void set_all_traits_to_picked()
{
    for (size_t i = 0; i < (size_t)TraitId::END; ++i) {
        s_traits_picked[i] = true;
    }
}

void pick_trait(const TraitId id)
{
    TRACE_FUNC_BEGIN;

    ASSERT(id != TraitId::END);

    s_traits_picked[(size_t)id] = true;

    TraitLogEntry trait_log_entry;

    trait_log_entry.trait_id = id;
    trait_log_entry.clvl = game::clvl();
    trait_log_entry.is_removal = false;

    s_trait_log.push_back(trait_log_entry);

    const TraitData& d = trait_data(id);

    if (d.on_picked) {
        // Has trait pick function
        trait_data(id).on_picked();
    }

    TRACE_FUNC_END;
}

void remove_trait(const TraitId id)
{
    TRACE_FUNC_BEGIN;

    ASSERT(id != TraitId::END);

    s_traits_picked[(size_t)id] = false;

    TraitLogEntry trait_log_entry;

    trait_log_entry.trait_id = id;
    trait_log_entry.clvl = game::clvl();
    trait_log_entry.is_removal = true;

    s_trait_log.push_back(trait_log_entry);

    const TraitData& d = trait_data(id);

    // If the trait applies effects when picked, it must also revert those
    ASSERT(!(d.on_picked && !d.on_removed));

    if (d.on_removed) {
        // Has trait removal function
        trait_data(id).on_removed();
    }

    TRACE_FUNC_END;
}

std::vector<TraitLogEntry> trait_log()
{
    return s_trait_log;
}

}  // namespace player_bon

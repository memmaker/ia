// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#include "terrain_trap.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ostream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "actor.hpp"
#include "actor_data.hpp"
#include "actor_factory.hpp"
#include "actor_see.hpp"
#include "array2.hpp"
#include "attack.hpp"
#include "audio_data.hpp"
#include "common_text.hpp"
#include "debug.hpp"
#include "direction.hpp"
#include "explosion.hpp"
#include "game.hpp"
#include "game_time.hpp"
#include "inventory.hpp"
#include "io.hpp"
#include "item.hpp"
#include "item_data.hpp"
#include "item_factory.hpp"
#include "item_weapon.hpp"
#include "map.hpp"
#include "msg_log.hpp"
#include "player_bon.hpp"
#include "player_spells.hpp"
#include "property.hpp"
#include "property_data.hpp"
#include "property_factory.hpp"
#include "property_handler.hpp"
#include "query.hpp"
#include "random.hpp"
#include "sound.hpp"
#include "spells.hpp"
#include "state.hpp"
#include "teleport.hpp"
#include "terrain_data.hpp"
#include "terrain_factory.hpp"
#include "text_format.hpp"

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------
static bool is_player_seeing_trap_trigger(
    const actor::Actor& actor,
    const P& pos)
{
    if (actor::is_player(&actor)) {
        return true;
    }
    else {
        const bool can_player_see_trap = map::g_seen.at(pos);
        const bool can_player_see_actor = actor::can_player_see_actor(actor);

        return can_player_see_trap || can_player_see_actor;
    }
}

static void communicate_sigil_trigger(const terrain::Trap& trap, const actor::Actor& actor)
{
    map::update_vision();

    const bool can_player_see_trap = map::g_seen.at(trap.pos());

    if (actor::is_player(&actor)) {
        if (can_player_see_trap) {
            std::string msg = "A beam of light shoots out from ";

            if (trap.is_hidden()) {
                msg += "the floor";
            }
            else {
                const std::string name = trap.name(Article::the);

                msg += name;
            }

            msg += "!";

            msg_log::add(msg);
        }
        else {
            msg_log::add("I feel a peculiar energy around me!");
        }
    }
    else {
        // Is a monster

        const bool can_player_see_actor = actor::can_player_see_actor(actor);

        if (can_player_see_actor || can_player_see_trap) {
            const std::string actor_name =
                can_player_see_actor
                ? actor::name_the(actor)
                : "it";

            msg_log::add("A beam of light shoots out under " + actor_name + ".");
        }
    }

    Snd snd(
        "I hear an otherworldly blaze.",
        audio::SfxId::sigil_trigger,
        IgnoreMsgIfOriginSeen::yes,
        trap.pos(),
        nullptr,
        SndVol::low,
        AlertsMon::no);

    snd.run();

    if (can_player_see_trap) {
        const int flash_speed_pct = 15;

        io::flash_at(trap.pos(), colors::yellow(), flash_speed_pct);
    }

    if (actor::is_player(&actor)) {
        msg_log::more_prompt();
    }
}

static void communicate_sigil_strained(const terrain::Trap& trap)
{
    map::update_vision();

    if (map::g_seen.at(trap.pos()) && !trap.is_hidden()) {
        const std::string name = text_format::first_to_upper(trap.name(Article::the));

        msg_log::add(name + " wavers.");
    }
}

static std::string sigil_fade_msg(const terrain::Trap& trap)
{
    const std::string name = text_format::first_to_upper(trap.name(Article::the));

    return name + " fades out.";
}

static void communicate_sigil_destroyed(const terrain::Trap& trap)
{
    map::update_vision();

    if (map::g_seen.at(trap.pos()) && !trap.is_hidden()) {
        Snd snd(
            "",
            audio::SfxId::sigil_fade,
            IgnoreMsgIfOriginSeen::yes,
            trap.pos(),
            nullptr,
            SndVol::low,
            AlertsMon::no);

        snd.run();

        const int flash_speed_pct = 50;

        io::flash_at(trap.pos(), colors::gray(), flash_speed_pct);

        msg_log::add(sigil_fade_msg(trap));
    }
}

static void communicate_mechanical_trap_trigger(const actor::Actor& actor, const P& pos)
{
    std::string msg = "I hear a click.";

    auto alerts = AlertsMon::no;

    if (actor::is_player(&actor)) {
        alerts = AlertsMon::yes;

        // Use a amore foreboding message when the player is triggering.
        msg += "..";
    }

    Snd snd(
        msg,
        audio::SfxId::mechanical_trap_trigger,
        IgnoreMsgIfOriginSeen::no,
        pos,
        nullptr,
        SndVol::low,
        alerts);

    snd.run();

    if (actor::is_player(&actor)) {
        const bool is_deaf = map::g_player->m_properties.has(prop::Id::deaf);

        if (is_deaf) {
            msg_log::add("I feel the ground shifting slightly under my foot.");
        }

        msg_log::more_prompt();
    }
}

static terrain::TrapImpl* make_trap_impl_from_id(
    const terrain::TrapId trap_id,
    const P& pos,
    terrain::Trap* parent_trap)
{
    switch (trap_id) {
    case terrain::TrapId::dart:
        return new terrain::TrapDart(pos, parent_trap);
        break;

    case terrain::TrapId::spear:
        return new terrain::TrapSpear(pos, parent_trap);
        break;

    case terrain::TrapId::blinding:
        return new terrain::TrapBlindingFlash(pos, parent_trap);
        break;

    case terrain::TrapId::deafening:
        return new terrain::TrapDeafening(pos, parent_trap);
        break;

    case terrain::TrapId::teleport:
        return new terrain::TrapTeleport(pos, parent_trap);
        break;

    case terrain::TrapId::summon:
        return new terrain::TrapSummonMon(pos, parent_trap);
        break;

    case terrain::TrapId::smoke:
        return new terrain::TrapSmoke(pos, parent_trap);
        break;

    case terrain::TrapId::alarm:
        return new terrain::TrapAlarm(pos, parent_trap);
        break;

    case terrain::TrapId::web:
        return new terrain::TrapWeb(pos, parent_trap);
        break;

    case terrain::TrapId::slow:
        return new terrain::TrapSlow(pos, parent_trap);
        break;

    case terrain::TrapId::haste:
        return new terrain::TrapHaste(pos, parent_trap);
        break;

    case terrain::TrapId::alter_env:
        return new terrain::TrapAlterEnv(pos, parent_trap);
        break;

    case terrain::TrapId::curse:
        return new terrain::TrapCurse(pos, parent_trap);
        break;

    case terrain::TrapId::bless:
        return new terrain::TrapBless(pos, parent_trap);
        break;

    case terrain::TrapId::unlearn_spell:
        return new terrain::TrapUnlearnSpell(pos, parent_trap);
        break;

    case terrain::TrapId::boundary:
        return new terrain::TrapBoundary(pos, parent_trap);
        break;

    case terrain::TrapId::END_MECHANICAL:
    case terrain::TrapId::END_OF_AUTO_SPAWNABLE_TRAPS:
    case terrain::TrapId::END:
    case terrain::TrapId::any:
        break;
    }

    return nullptr;
}

static terrain::TrapImpl* try_make_impl(
    const terrain::TrapId trap_id,
    const P& pos,
    terrain::Trap* parent_trap)
{
    terrain::TrapImpl* impl = make_trap_impl_from_id(trap_id, pos, parent_trap);

    terrain::TrapPlacementValid valid = impl->on_place();

    if (valid == terrain::TrapPlacementValid::yes) {
        return impl;
    }
    else {
        // Placement not valid.
        delete impl;

        return nullptr;
    }
}

static terrain::TrapId get_random_id_to_spawn()
{
    Range id_range;

    // Spawn sigils most of the time.
    if (rnd::one_in(3)) {
        // "Mechanical" trap.
        id_range.min = 0;
        id_range.max = (int)terrain::TrapId::END_MECHANICAL - 1;
    }
    else {
        // sigil.
        id_range.min = (int)terrain::TrapId::END_MECHANICAL + 1;
        id_range.max = (int)terrain::TrapId::END_OF_AUTO_SPAWNABLE_TRAPS - 1;
    }

    return (terrain::TrapId)id_range.roll();
}

static bool can_actor_trigger_mechanical_trap(const actor::Actor& actor)
{
    const prop::PropHandler& props = actor.m_properties;

    return (
        !props.has(prop::Id::ethereal) &&
        !props.has(prop::Id::flying) &&
        !props.has(prop::Id::tiny_flying) &&
        !props.has(prop::Id::small_crawling) &&
        // NOTE: Spiders never trigger "mechanical" traps, including webs.
        !actor.m_data->is_spider);
}

// -----------------------------------------------------------------------------
// terrain
// -----------------------------------------------------------------------------
namespace terrain
{
Trap::~Trap()
{
    delete m_trap_impl;
    delete m_mimic_terrain;
}

bool Trap::try_init_type(const TrapId id)
{
    ASSERT(
        id != TrapId::END_MECHANICAL &&
        id != TrapId::END_OF_AUTO_SPAWNABLE_TRAPS &&
        id != TrapId::END);

    Terrain* const terrain_here = map::g_terrain.at(m_pos);

    if (!terrain_here->can_have_trap()) {
        TRACE
            << "Cannot place trap on terrain id: "
            << (int)terrain_here->id()
            << "\n"
            << "Trap id: " << (int)id
            << "\n";

        ASSERT(false);

        return false;
    }

    if (id == TrapId::any) {
        // Attempt to set a trap implementation until succeeding.
        while (true) {
            const TrapId random_id = get_random_id_to_spawn();

            TrapImpl* const impl = try_make_impl(random_id, m_pos, this);

            if (impl) {
                // Trap placement is good!
                m_trap_impl = impl;

                break;
            }
        }
    }
    else {
        // Make a specific trap type.

        // NOTE: This may fail, in which case we have no trap implementation. The trap
        // creator is responsible for handling this situation.
        m_trap_impl = try_make_impl(id, m_pos, this);
    }

    if (m_trap_impl) {
        m_is_hidden = true;

        return true;
    }
    else {
        return false;
    }
}

void Trap::set_mimic_terrain(terrain::Terrain* const terrain)
{
    m_mimic_terrain = terrain;
}

const terrain::Terrain* Trap::get_mimic_terrain() const
{
    ASSERT(m_mimic_terrain);

    return m_mimic_terrain;
}

void Trap::hit(
    DmgType dmg_type,
    actor::Actor* actor,
    const P& from_pos,
    int dmg)
{
    (void)dmg_type;
    (void)actor;
    (void)from_pos;
    (void)dmg;
}

TrapId Trap::type() const
{
    ASSERT(m_trap_impl);

    return m_trap_impl->type();
}

bool Trap::is_sigil() const
{
    ASSERT(m_trap_impl);

    return m_trap_impl->is_sigil();
}

void Trap::on_new_turn_hook()
{
    m_trap_impl->on_new_turn();
}

AllowAction Trap::pre_bump(actor::Actor& actor_bumping)
{
    // Ask the player if they really want to step into the trap.

    // This does not apply to monster creatures or to a confused player:
    if (!actor::is_player(&actor_bumping) ||
        actor_bumping.m_properties.has(prop::Id::confused)) {
        return AllowAction::yes;
    }

    bool can_player_trigger_trap = false;

    if (is_sigil()) {
        // Boundary Sigils are always placed by the player and only ever affects monsters.
        can_player_trigger_trap = type() != TrapId::boundary;
    }
    else {
        can_player_trigger_trap = can_actor_trigger_mechanical_trap(actor_bumping);
    }

    const bool should_query_player =
        can_player_trigger_trap &&
        map::g_seen.at(m_pos) &&
        !m_is_hidden;

    if (should_query_player) {
        // The trap is known, and would be triggered by the player.

        const std::string name_the = name(Article::the);

        const std::string msg =
            "Step into " +
            name_the +
            "? " +
            common_text::g_yes_or_no_hint;

        msg_log::add(
            msg,
            colors::light_white(),
            MsgInterruptPlayer::no,
            MorePromptOnMsg::no,
            CopyToMsgHistory::no);

        const BinaryAnswer query_result = query::yes_or_no();

        msg_log::clear();

        return (
            (query_result == BinaryAnswer::no)
                ? AllowAction::no
                : AllowAction::yes);
    }
    else {
        // The trap is hidden, or would not be triggered by the player - delegate the
        // question to the mimicked terrain.

        const AllowAction result = m_mimic_terrain->pre_bump(actor_bumping);

        return result;
    }
}

void Trap::bump(actor::Actor& actor_bumping)
{
    ASSERT(m_trap_impl);

    TRACE
        << "Bumping trap of type "
        << "'" << m_trap_impl->name(Article::a) << "', "
        << "with trap implementation id "
        << "'" << (int)m_trap_impl->type() << "'"
        << "\n";

    actor::update_player_fov();

    states::draw();

    m_trap_impl->on_bumped(actor_bumping);
}

bool Trap::disarm()
{
    const std::string disarm_msg = m_trap_impl->disarm_msg();

    if (!disarm_msg.empty()) {
        msg_log::add(disarm_msg);
    }

    destroy();

    return true;
}

void Trap::destroy()
{
    ASSERT(m_mimic_terrain);

    if (is_sigil() || type() == TrapId::web) {
        // Sigil or web - change the terrain here to the mimic terrain.

        Terrain* const mimic_terrain = m_mimic_terrain;

        m_mimic_terrain = nullptr;

        // NOTE: This call destroys this object!
        map::update_terrain(mimic_terrain);
    }
    else {
        // "Mechanical" trap (and not a web), just put rubble.
        map::update_terrain(terrain::make(terrain::Id::rubble_low, m_pos));
    }
}

void Trap::reveal(const PrintRevealMsg print_reveal_msg)
{
    TRACE_FUNC_BEGIN;

    const bool is_hidden_before = m_is_hidden;

    m_is_hidden = false;

    clear_gore();

    const bool allow_print =
        ((print_reveal_msg == PrintRevealMsg::if_seen) &&
         map::g_seen.at(m_pos)) ||
        (print_reveal_msg == PrintRevealMsg::yes);

    if (is_hidden_before && allow_print) {
        states::draw();

        std::string msg;

        const std::string trap_name_a = m_trap_impl->name(Article::a);

        if (m_pos == map::g_player->m_pos) {
            msg = "There is " + trap_name_a + " here!";
        }
        else {
            // Trap is not at player position
            msg = "I spot " + trap_name_a + ".";
        }

        msg_log::add(msg);
    }

    TRACE_FUNC_END;
}

void Trap::on_revealed_from_searching()
{
    if (type() != TrapId::web) {
        game::incr_player_xp(g_xp_on_reveal_trap);
    }
}

std::string Trap::name(const Article article) const
{
    if (m_is_hidden) {
        return m_mimic_terrain->name(article);
    }
    else {
        return m_trap_impl->name(article);
    }
}

Color Trap::color_default() const
{
    if (m_is_hidden) {
        return m_mimic_terrain->color();
    }
    else {
        return m_trap_impl->color();
    }
}

char Trap::character() const
{
    if (m_is_hidden) {
        return m_mimic_terrain->character();
    }
    else {
        return m_trap_impl->character();
    }
}

gfx::TileId Trap::tile() const
{
    if (m_is_hidden) {
        return m_mimic_terrain->tile();
    }
    else {
        return m_trap_impl->tile();
    }
}

Material Trap::material() const
{
    if (m_is_hidden) {
        return m_mimic_terrain->material();
    }
    else {
        return m_data->material_type;
    }
}

void Trap::strain()
{
    m_trap_impl->strain();
}

// -----------------------------------------------------------------------------
// Trap Implementation base class
// -----------------------------------------------------------------------------
char TrapImpl::character() const
{
    return '^';
}

// -----------------------------------------------------------------------------
// Mechanical trap base class
// -----------------------------------------------------------------------------
MechTrapImpl::MechTrapImpl(P pos, TrapId type, Trap* const base_trap) :
    TrapImpl(pos, type, base_trap) {}

gfx::TileId MechTrapImpl::tile() const
{
    return gfx::TileId::trap_general;
}

void MechTrapImpl::on_bumped(actor::Actor& actor_bumping)
{
    if (!can_actor_trigger_mechanical_trap(actor_bumping)) {
        return;
    }

    if (!actor::is_player(&actor_bumping)) {
        // TODO: This seems to prevent the trap from triggering when the player kicks a
        // monster into the trap in some cases? Perhaps when the monster has just stepped
        // into sight of the player? The problem can happen both when the monster is aware
        // or unaware?

        // Put some extra restrictions on monsters triggering traps. All of the following
        // must also be fulfilled for a monster to trigger the trap:
        //
        // * They can see their target
        // * They are aware of the player
        // * Trap is not hidden
        //
        const bool allow_monster_trigger =
            actor_bumping.m_ai_state.is_target_seen &&
            actor::is_aware_of_player(actor_bumping) &&
            !m_base_trap->is_hidden();

        if (!allow_monster_trigger) {
            return;
        }
    }

    trigger(&actor_bumping);
}

void MechTrapImpl::trigger(actor::Actor* actor)
{
    TRACE_FUNC_BEGIN;

    const WasKnownBeforeTrigger was_known_before =
        m_base_trap->is_hidden()
        ? WasKnownBeforeTrigger::no
        : WasKnownBeforeTrigger::yes;

    if (actor::is_player(actor)) {
        m_base_trap->reveal(PrintRevealMsg::no);
    }

    if (type() != TrapId::web) {
        communicate_mechanical_trap_trigger(*actor, m_pos);
    }

    run_trigger_effect(was_known_before);

    // NOTE: This deletes this terrain object!
    m_base_trap->destroy();

    TRACE_FUNC_END;
}

std::string MechTrapImpl::disarm_msg() const
{
    return "I disarm a trap.";
}

// -----------------------------------------------------------------------------
// Sigil base class
// -----------------------------------------------------------------------------
SigilImpl::SigilImpl(P pos, TrapId type, Trap* const base_trap) :
    TrapImpl(pos, type, base_trap) {}

std::string SigilImpl::name(const Article article) const
{
    std::string name = (article == Article::a) ? "a" : "the";

    name += " Sigil";

    return name;
}

gfx::TileId SigilImpl::tile() const
{
    return gfx::TileId::elder_sign;
}

char SigilImpl::character() const
{
    return '*';
}

Color SigilImpl::color() const
{
    return colors::light_red();
}

void SigilImpl::strain()
{
    if (rnd::percent(fade_chance_pct())) {
        communicate_sigil_destroyed(*m_base_trap);

        m_base_trap->destroy();

        map::update_vision();
    }
    else {
        communicate_sigil_strained(*m_base_trap);
    }
}

std::string SigilImpl::disarm_msg() const
{
    return sigil_fade_msg(*m_base_trap);
}

// -----------------------------------------------------------------------------
// Specific trap implementations
// -----------------------------------------------------------------------------
TrapDart::TrapDart(P pos, Trap* const base_trap) :
    MechTrapImpl(pos, TrapId::dart, base_trap),
    m_is_poisoned((map::g_dlvl >= g_dlvl_harder_traps) && rnd::one_in(3))
{}

std::string TrapDart::name(const Article article) const
{
    std::string name = (article == Article::a) ? "a" : "the";

    name += " dart trap";

    return name;
}

Color TrapDart::color() const
{
    return colors::white();
}

TrapPlacementValid TrapDart::on_place()
{
    std::vector<P> offsets = dir_utils::g_cardinal_list;

    rnd::shuffle(offsets);

    const int nr_steps_min = 2;
    const int nr_steps_max = g_fov_radi_int;

    TrapPlacementValid trap_plament_valid = TrapPlacementValid::no;

    for (const P& d : offsets) {
        P p = m_pos;

        for (int i = 0; i <= nr_steps_max; ++i) {
            p += d;

            const Terrain* const terrain = map::g_terrain.at(p);

            const bool is_wall = terrain->id() == terrain::Id::wall;

            const bool is_passable =
                terrain->is_projectile_passable();

            if (!is_passable &&
                ((i < nr_steps_min) || !is_wall)) {
                // We are blocked too early - OR - blocked by a terrain other than a
                // wall. Give up on this direction.
                break;
            }

            if ((i >= nr_steps_min) && is_wall) {
                // This is a good origin!
                m_dart_origin = p;
                trap_plament_valid = TrapPlacementValid::yes;
                break;
            }
        }

        if (trap_plament_valid == TrapPlacementValid::yes) {
            // A valid origin has been found

            if (rnd::fraction(2, 3)) {
                terrain::make_gore(m_pos);
                terrain::make_blood(m_pos);
            }

            break;
        }
    }

    return trap_plament_valid;
}

void TrapDart::run_trigger_effect(const WasKnownBeforeTrigger was_known_before)
{
    (void)was_known_before;

    TRACE_FUNC_BEGIN;

    ASSERT((m_dart_origin.x == m_pos.x) || (m_dart_origin.y == m_pos.y));
    ASSERT(m_dart_origin != m_pos);

    if (map::g_terrain.at(m_dart_origin)->id() != terrain::Id::wall) {
        // NOTE: This is permanently set from now on
        m_is_dart_origin_destroyed = true;
    }

    if (m_is_dart_origin_destroyed) {
        return;
    }

    // Aim target is the wall on the other side of the map
    P aim_pos = m_dart_origin;

    if (m_dart_origin.x == m_pos.x) {
        aim_pos.y =
            (m_dart_origin.y > m_pos.y)
            ? 0
            : (map::h() - 1);
    }
    else {
        // Dart origin is on same vertial line as the trap
        aim_pos.x =
            (m_dart_origin.x > m_pos.x)
            ? 0
            : (map::w() - 1);
    }

    if (map::g_seen.at(m_dart_origin)) {
        const std::string name =
            map::g_terrain.at(m_dart_origin)
                ->name(Article::the);

        msg_log::add("A dart is launched from " + name + "!");
    }

    // Make a temporary dart weapon
    item::Wpn* wpn = nullptr;

    if (m_is_poisoned) {
        wpn =
            static_cast<item::Wpn*>(
                item::make(item::Id::trap_dart_poison));
    }
    else {
        // Not poisoned
        wpn =
            static_cast<item::Wpn*>(
                item::make(item::Id::trap_dart));
    }

    // Fire!
    attack::ranged(
        nullptr,
        m_dart_origin,
        aim_pos,
        *wpn);

    delete wpn;

    TRACE_FUNC_END;
}

TrapSpear::TrapSpear(P pos, Trap* const base_trap) :
    MechTrapImpl(pos, TrapId::spear, base_trap),
    m_is_poisoned((map::g_dlvl >= g_dlvl_harder_traps) && rnd::one_in(4))
{}

std::string TrapSpear::name(const Article article) const
{
    std::string name = (article == Article::a) ? "a" : "the";

    name += " spear trap";

    return name;
}

Color TrapSpear::color() const
{
    return colors::light_white();
}

TrapPlacementValid TrapSpear::on_place()
{
    std::vector<P> offsets = dir_utils::g_cardinal_list;

    rnd::shuffle(offsets);

    TrapPlacementValid trap_plament_valid = TrapPlacementValid::no;

    for (const P& d : offsets) {
        const P p = m_pos + d;

        const Terrain* const terrain = map::g_terrain.at(p);

        const bool is_wall = terrain->id() == terrain::Id::wall;

        const bool is_passable = terrain->is_projectile_passable();

        if (is_wall && !is_passable) {
            // This is a good origin!
            m_spear_origin = p;
            trap_plament_valid = TrapPlacementValid::yes;

            if (rnd::fraction(2, 3)) {
                terrain::make_gore(m_pos);
                terrain::make_blood(m_pos);
            }

            break;
        }
    }

    return trap_plament_valid;
}

void TrapSpear::run_trigger_effect(const WasKnownBeforeTrigger was_known_before)
{
    (void)was_known_before;

    TRACE_FUNC_BEGIN;

    ASSERT(m_spear_origin.x == m_pos.x || m_spear_origin.y == m_pos.y);
    ASSERT(m_spear_origin != m_pos);

    if (map::g_terrain.at(m_spear_origin)->id() != terrain::Id::wall) {
        // NOTE: This is permanently set from now on
        m_is_spear_origin_destroyed = true;
    }

    if (m_is_spear_origin_destroyed) {
        return;
    }

    if (map::g_seen.at(m_spear_origin)) {
        const std::string name =
            map::g_terrain.at(m_spear_origin)
                ->name(Article::the);

        msg_log::add("A spear shoots out from " + name + "!");
    }

    // Is anyone standing on the trap now?
    actor::Actor* const actor_on_trap = map::living_actor_at(m_pos);

    if (actor_on_trap) {
        // Make a temporary spear weapon
        item::Wpn* wpn = nullptr;

        if (m_is_poisoned) {
            wpn = static_cast<item::Wpn*>(
                item::make(item::Id::trap_spear_poison));
        }
        else {
            // Not poisoned
            wpn =
                static_cast<item::Wpn*>(
                    item::make(item::Id::trap_spear));
        }

        // Attack!
        attack::melee(nullptr, m_spear_origin, m_pos, *wpn);

        delete wpn;
    }

    TRACE_FUNC_BEGIN;
}

TrapBlindingFlash::TrapBlindingFlash(P pos, Trap* const base_trap) :
    MechTrapImpl(pos, TrapId::blinding, base_trap) {}

std::string TrapBlindingFlash::name(const Article article) const
{
    std::string name = (article == Article::a) ? "a" : "the";

    name += " blinding trap";

    return name;
}

Color TrapBlindingFlash::color() const
{
    return colors::yellow();
}

void TrapBlindingFlash::run_trigger_effect(const WasKnownBeforeTrigger was_known_before)
{
    (void)was_known_before;

    TRACE_FUNC_BEGIN;

    if (map::g_seen.at(m_pos)) {
        msg_log::add("There is an intense flash of light!");
    }

    explosion::run(
        m_pos,
        ExplType::apply_prop,
        EmitExplSnd::no,
        -1,
        ExplExclCenter::no,
        {prop::make(prop::Id::blind)},
        colors::yellow());

    TRACE_FUNC_END;
}

TrapDeafening::TrapDeafening(P pos, Trap* const base_trap) :
    MechTrapImpl(pos, TrapId::deafening, base_trap) {}

std::string TrapDeafening::name(const Article article) const
{
    std::string name = (article == Article::a) ? "a" : "the";

    name += " deafening trap";

    return name;
}

Color TrapDeafening::color() const
{
    return colors::violet();
}

void TrapDeafening::run_trigger_effect(const WasKnownBeforeTrigger was_known_before)
{
    (void)was_known_before;

    TRACE_FUNC_BEGIN;

    if (map::g_seen.at(m_pos)) {
        msg_log::add(
            "There is suddenly a crushing pressure in the air!");
    }

    explosion::run(
        m_pos,
        ExplType::apply_prop,
        EmitExplSnd::no,
        -1,
        ExplExclCenter::no,
        {prop::make(prop::Id::deaf)},
        colors::light_white());

    TRACE_FUNC_END;
}

TrapSmoke::TrapSmoke(P pos, Trap* const base_trap) :
    MechTrapImpl(pos, TrapId::smoke, base_trap) {}

std::string TrapSmoke::name(const Article article) const
{
    std::string name = (article == Article::a) ? "a" : "the";

    name += " smoke trap";

    return name;
}

Color TrapSmoke::color() const
{
    return colors::gray();
}

void TrapSmoke::run_trigger_effect(const WasKnownBeforeTrigger was_known_before)
{
    (void)was_known_before;

    TRACE_FUNC_BEGIN;

    if (map::g_seen.at(m_pos)) {
        msg_log::add(
            "A burst of smoke is released from a vent in the "
            "floor!");
    }

    Snd snd(
        "I hear a burst of gas.",
        audio::SfxId::gas,
        IgnoreMsgIfOriginSeen::yes,
        m_pos,
        nullptr,
        SndVol::low,
        AlertsMon::yes);

    snd.run();

    explosion::run_smoke_explosion_at(m_pos);

    TRACE_FUNC_END;
}

TrapAlarm::TrapAlarm(P pos, Trap* const base_trap) :
    MechTrapImpl(pos, TrapId::alarm, base_trap) {}

std::string TrapAlarm::name(const Article article) const
{
    std::string name = (article == Article::a) ? "an" : "the";

    name += " alarm trap";

    return name;
}

Color TrapAlarm::color() const
{
    return colors::orange();
}

void TrapAlarm::run_trigger_effect(const WasKnownBeforeTrigger was_known_before)
{
    (void)was_known_before;

    TRACE_FUNC_BEGIN;

    Snd snd(
        "An alarm sounds!",
        audio::SfxId::END,
        IgnoreMsgIfOriginSeen::no,
        m_pos,
        nullptr,
        SndVol::global,
        AlertsMon::yes);

    snd.run();

    TRACE_FUNC_END;
}

TrapWeb::TrapWeb(P pos, Trap* const base_trap) :
    MechTrapImpl(pos, TrapId::web, base_trap) {}

std::string TrapWeb::name(const Article article) const
{
    std::string name = (article == Article::a) ? "a" : "the";

    name += " spider web";

    return name;
}

Color TrapWeb::color() const
{
    return colors::light_white();
}

gfx::TileId TrapWeb::tile() const
{
    return gfx::TileId::web;
}

char TrapWeb::character() const
{
    return '*';
}

void TrapWeb::run_trigger_effect(const WasKnownBeforeTrigger was_known_before)
{
    TRACE_FUNC_BEGIN;

    actor::Actor* const actor_here = map::living_actor_at(m_pos);

    ASSERT(actor_here);

    if (!actor_here) {
        return;
    }

    // Machetes cut down spider webs - if the player has a machete and the trap was already
    // known, do not apply entanglement.
    if (actor::is_player(actor_here) && (was_known_before == WasKnownBeforeTrigger::yes)) {
        item::Item* item = actor_here->m_inv.item_in_slot(SlotId::wpn);

        if (item && (item->id() == item::Id::machete)) {
            msg_log::add(
                "I cut myself free with my Machete.",
                colors::text(),
                MsgInterruptPlayer::no,
                MorePromptOnMsg::no);

            return;
        }
    }

    if (actor::is_player(actor_here)) {
        std::string msg;
        if (actor_here->m_properties.allow_see()) {
            msg = "I am entangled in a spider web!";
        }
        else {
            // Cannot see
            msg = "I am entangled in a sticky mass of threads!";
        }

        msg_log::add(msg);
    }
    else {
        // Is a monster

        if (actor::can_player_see_actor(*actor_here)) {
            const std::string actor_name =
                text_format::first_to_upper(
                    actor::name_the(*actor_here));

            msg_log::add(actor_name + " is entangled in a huge spider web!");
        }
    }

    prop::Prop* const entangled = prop::make(prop::Id::entangled);

    entangled->set_indefinite();

    actor_here->m_properties.apply(entangled, prop::PropSrc::intr, false, Verbose::no);

    // Players getting stuck in spider webs alerts all spiders.
    if (actor::is_player(actor_here)) {
        for (actor::Actor* const actor : game_time::g_actors) {
            if (!actor::is_player(actor) &&
                actor->m_data->is_spider &&
                !map::g_player->is_leader_of(actor)) {
                // Monster is a hostile spider.

                // Double duration.
                const int factor = 2;

                actor->become_aware_player(actor::AwareSource::other, factor);
            }
        }
    }

    TRACE_FUNC_END;
}

std::string TrapWeb::disarm_msg() const
{
    return "I tear down a spider web.";
}

void TrapTeleport::on_bumped(actor::Actor& actor_bumping)
{
    TRACE_FUNC_BEGIN;

    if (actor::is_player(&actor_bumping)) {
        m_base_trap->reveal(PrintRevealMsg::no);
    }

    communicate_sigil_trigger(*m_base_trap, actor_bumping);

    // NOTE: For the teleport trap, we strain it before teleportin the player, so that they are
    // aware that it's removed.
    //
    // NOTE: This deletes this terrain object!
    //
    strain();

    teleport(actor_bumping);

    TRACE_FUNC_END;
}

int TrapTeleport::fade_chance_pct() const
{
    return 100;
}

void TrapSummonMon::on_bumped(actor::Actor& actor_bumping)
{
    TRACE_FUNC_BEGIN;

    if (actor::is_player(&actor_bumping)) {
        m_base_trap->reveal(PrintRevealMsg::no);
    }

    communicate_sigil_trigger(*m_base_trap, actor_bumping);

    TRACE << "Finding summon candidates" << "\n";
    std::vector<std::string> summon_bucket;

    for (const auto& it : actor::g_data) {
        const actor::ActorData& data = it.second;

        if (data.can_be_summoned_by_mon &&
            data.spawn_min_dlvl <= (map::g_dlvl + 2)) {
            summon_bucket.push_back(data.id);
        }
    }

    if (summon_bucket.empty()) {
        TRACE << "No eligible candidates found" << "\n";
    }
    else {
        // Eligible monsters found
        const std::string id_to_summon = rnd::element(summon_bucket);

        TRACE << "Actor id: " << id_to_summon << "\n";

        const actor::MonSpawnResult summoned =
            actor::spawn(m_pos, {id_to_summon})
                .make_aware_of_player();

        std::for_each(
            std::begin(summoned.monsters),
            std::end(summoned.monsters),
            [](actor::Actor* const mon) {
                prop::Prop* prop_summoned = prop::make(prop::Id::summoned);

                prop_summoned->set_indefinite();

                mon->m_properties.apply(prop_summoned);

                prop::Prop* prop_waiting = prop::make(prop::Id::waiting);

                prop_waiting->set_duration(2);

                mon->m_properties.apply(prop_waiting);

                if (actor::can_player_see_actor(*mon)) {
                    states::draw();

                    const std::string name_a =
                        text_format::first_to_upper(
                            actor::name_a(*mon));

                    msg_log::add(name_a + " appears!");
                }
            });
    }

    strain();

    TRACE_FUNC_END;
}

int TrapSummonMon::fade_chance_pct() const
{
    return 100;
}

void TrapSlow::on_bumped(actor::Actor& actor_bumping)
{
    TRACE_FUNC_BEGIN;

    if (actor::is_player(&actor_bumping)) {
        m_base_trap->reveal(PrintRevealMsg::no);
    }

    communicate_sigil_trigger(*m_base_trap, actor_bumping);

    actor_bumping.m_properties.apply(prop::make(prop::Id::slowed));

    strain();

    TRACE_FUNC_END;
}

int TrapSlow::fade_chance_pct() const
{
    return 40;
}

void TrapHaste::on_bumped(actor::Actor& actor_bumping)
{
    TRACE_FUNC_BEGIN;

    if (actor::is_player(&actor_bumping)) {
        m_base_trap->reveal(PrintRevealMsg::no);
    }

    communicate_sigil_trigger(*m_base_trap, actor_bumping);

    actor_bumping.m_properties.apply(prop::make(prop::Id::hasted));

    strain();

    TRACE_FUNC_END;
}

int TrapHaste::fade_chance_pct() const
{
    return 40;
}

void TrapAlterEnv::on_bumped(actor::Actor& actor_bumping)
{
    TRACE_FUNC_BEGIN;

    if (actor::is_player(&actor_bumping)) {
        m_base_trap->reveal(PrintRevealMsg::no);
    }

    communicate_sigil_trigger(*m_base_trap, actor_bumping);

    if (is_player_seeing_trap_trigger(actor_bumping, m_pos)) {
        msg_log::add("The surroundings change!");
    }

    const int change_one_in_n = 2;

    prop::run_alter_env_effect(m_pos, change_one_in_n);

    strain();

    TRACE_FUNC_END;
}

int TrapAlterEnv::fade_chance_pct() const
{
    return 40;
}

void TrapCurse::on_bumped(actor::Actor& actor_bumping)
{
    TRACE_FUNC_BEGIN;

    if (actor::is_player(&actor_bumping)) {
        m_base_trap->reveal(PrintRevealMsg::no);
    }

    communicate_sigil_trigger(*m_base_trap, actor_bumping);

    actor_bumping.m_properties.apply(prop::make(prop::Id::cursed));

    strain();

    TRACE_FUNC_END;
}

int TrapCurse::fade_chance_pct() const
{
    return 40;
}

void TrapBless::on_bumped(actor::Actor& actor_bumping)
{
    TRACE_FUNC_BEGIN;

    if (actor::is_player(&actor_bumping)) {
        m_base_trap->reveal(PrintRevealMsg::no);
    }

    communicate_sigil_trigger(*m_base_trap, actor_bumping);

    actor_bumping.m_properties.apply(prop::make(prop::Id::blessed));

    strain();

    TRACE_FUNC_END;
}

int TrapBless::fade_chance_pct() const
{
    return 40;
}

void TrapUnlearnSpell::on_bumped(actor::Actor& actor_bumping)
{
    TRACE_FUNC_BEGIN;

    if (actor::is_player(&actor_bumping)) {
        m_base_trap->reveal(PrintRevealMsg::no);
    }

    communicate_sigil_trigger(*m_base_trap, actor_bumping);

    if (actor::is_player(&actor_bumping)) {
        try_unlearn_for_player();
    }
    else {
        try_unlearn_for_monster(actor_bumping);
    }

    strain();

    TRACE_FUNC_END;
}

void TrapUnlearnSpell::try_unlearn_for_player() const
{
    std::vector<SpellId> id_bucket;

    // Do not unlearn spells for the Exorcist.
    if (!player_bon::is_bg(Bg::exorcist)) {
        id_bucket.reserve((size_t)SpellId::END);

        for (int i = 0; i < (int)SpellId::END; ++i) {
            const auto id = (SpellId)i;

            bool has_scroll = false;

            for (const item::ItemData& d : item::g_data) {
                if (d.spell_cast_from_scroll == id) {
                    has_scroll = true;
                    break;
                }
            }

            if (!has_scroll) {
                continue;
            }

            if (!player_spells::is_spell_learned(id)) {
                continue;
            }

            if (player_spells::is_spell_forgotten(id)) {
                continue;
            }

            id_bucket.push_back(id);
        }
    }

    if (id_bucket.empty()) {
        msg_log::add("There is no apparent effect.");

        return;
    }

    msg_log::add("I am surrounded by a misty haze.");

    const SpellId id = rnd::element(id_bucket);

    player_spells::forget_spell(id);
}

void TrapUnlearnSpell::try_unlearn_for_monster(actor::Actor& actor) const
{
    std::vector<actor::MonSpell>& spells = actor.m_mon_spells;

    const bool player_sees_actor = actor::can_player_see_actor(actor);

    if (spells.empty()) {
        if (player_sees_actor) {
            msg_log::add("There is no apparent effect.");
        }

        return;
    }

    if (player_sees_actor) {
        const std::string actor_name =
            text_format::first_to_upper(
                actor::name_the(actor));

        msg_log::add("A misty haze surrounds " + actor_name + ".");
    }

    const int idx = rnd::idx(spells);

    delete spells[idx].spell;

    spells.erase(std::begin(spells) + idx);
}

int TrapUnlearnSpell::fade_chance_pct() const
{
    return 60;
}

std::string TrapBoundary::name(const Article article) const
{
    std::string name = (article == Article::a) ? "a" : "the";

    name += " Boundary Sigil";

    return name;
}

Color TrapBoundary::color() const
{
    return colors::light_green();
}

int TrapBoundary::fade_chance_pct() const
{
    // Should never be called, as strain() is overridden.
    ASSERT(false);

    return 100;
}

void TrapBoundary::set_nr_actions_to_prevent(const int nr)
{
    ASSERT(nr > 0);

    m_nr_actions_countdown = nr;
}

void TrapBoundary::strain()
{
    --m_nr_actions_countdown;

    if (m_nr_actions_countdown <= 0) {
        destroy();
    }
    else {
        communicate_sigil_strained(*m_base_trap);
    }
}

void TrapBoundary::on_new_turn()
{
    const int destroy_one_in_n = 200;

    if (rnd::one_in(destroy_one_in_n)) {
        destroy();
    }
}

void TrapBoundary::destroy()
{
    communicate_sigil_destroyed(*m_base_trap);

    m_base_trap->destroy();

    map::update_vision();
}

}  // namespace terrain

// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#ifndef TERRAIN_TRAP_HPP
#define TERRAIN_TRAP_HPP

#include <string>

#include "colors.hpp"
#include "gfx.hpp"
#include "global.hpp"
#include "pos.hpp"
#include "random.hpp"
#include "terrain.hpp"

namespace terrain
{
struct TerrainData;
}  // namespace terrain

namespace actor
{
class Actor;
}  // namespace actor

namespace terrain
{
class TrapImpl;

enum class TrapId
{
    //
    // --- MECHANICAL TRAPS
    //

    alarm,
    blinding,
    dart,
    deafening,
    smoke,
    spear,
    web,

    END_MECHANICAL,

    //
    // --- SIGILS
    //

    // Negative
    curse,
    slow,
    summon,
    teleport,
    unlearn_spell,

    // Positive
    bless,
    haste,

    // Neutral
    alter_env,

    END_OF_AUTO_SPAWNABLE_TRAPS,

    boundary,

    END,

    any
};

enum class TrapPlacementValid
{
    no,
    yes
};

enum class WasKnownBeforeTrigger
{
    no,
    yes,
};

class Trap : public Terrain
{
public:
    Trap(const P& pos, const TerrainData* const data) :
        Terrain(pos, data) {}

    Trap() = delete;

    ~Trap();

    bool try_init_type(TrapId id);

    void set_mimic_terrain(terrain::Terrain* terrain);

    const terrain::Terrain* get_mimic_terrain() const;

    AllowAction pre_bump(actor::Actor& actor_bumping) override;

    void bump(actor::Actor& actor_bumping) override;

    gfx::TileId tile() const override;

    char character() const override;

    std::string name(Article article) const override;

    void hit(
        DmgType dmg_type,
        actor::Actor* actor,
        const P& from_pos,
        int dmg) override;

    bool disarm();

    // Quietly destroys the trap, and either places rubble, or replaces it with the mimic
    // terrain (depending on trap type).
    void destroy();

    void on_new_turn_hook() override;

    bool can_have_blood() const override
    {
        return m_is_hidden;
    }

    bool can_have_gore() const override
    {
        return m_is_hidden;
    }

    bool is_sigil() const;

    void reveal(PrintRevealMsg print_reveal_msg) override;

    void on_revealed_from_searching() override;

    Material material() const override;

    TrapId type() const;

    TrapImpl* trap_impl() const
    {
        return m_trap_impl;
    }

    void player_try_spot_hidden();

    void strain();

private:
    Color color_default() const override;

    Terrain* m_mimic_terrain {nullptr};

    // TODO: Should be a unique pointer
    TrapImpl* m_trap_impl {nullptr};
};

class TrapImpl
{
public:
    TrapImpl(P p, TrapId type, Trap* const base_trap) :
        m_pos(p),
        m_type(type),
        m_base_trap(base_trap) {}

    virtual ~TrapImpl() = default;

    TrapId type() const
    {
        return m_type;
    }

    virtual bool is_sigil() const = 0;

    // Called by the trap terrain after picking a random trap implementation. This allows the
    // specific implementation to initialize itself and possibly modify the map. The
    // implementation may report that the placement is not applicable for this specific trap
    // type, in which case another implementation may be picked.
    virtual TrapPlacementValid on_place()
    {
        return TrapPlacementValid::yes;
    }

    virtual void on_bumped(actor::Actor& actor_bumping) = 0;

    virtual void on_new_turn() {};

    virtual std::string name(Article article) const = 0;

    virtual Color color() const = 0;

    virtual gfx::TileId tile() const = 0;

    virtual char character() const;

    virtual std::string disarm_msg() const = 0;

    virtual void strain() {};

protected:
    P m_pos;
    TrapId m_type;
    Trap* const m_base_trap;
};

class MechTrapImpl : public TrapImpl
{
public:
    MechTrapImpl(P pos, TrapId type, Trap* base_trap);

    virtual ~MechTrapImpl() = default;

    bool is_sigil() const override
    {
        return false;
    }

    void trigger(actor::Actor* actor);

    void on_bumped(actor::Actor& actor_bumping) final;

    gfx::TileId tile() const override;

    virtual void run_trigger_effect(WasKnownBeforeTrigger was_known_before) = 0;

    std::string disarm_msg() const override;
};

class SigilImpl : public TrapImpl
{
public:
    SigilImpl(P pos, TrapId type, Trap* base_trap);

    virtual ~SigilImpl() = default;

    bool is_sigil() const override
    {
        return true;
    }

    void on_bumped(actor::Actor& actor_bumping) override
    {
        // NOTE: For sigils, bumping does nothing per default - it is up to each type to
        // define a behavior (not all sigils necessarily "trigger" on being bumped, they may
        // have completely different interactions).
        (void)actor_bumping;
    }

    std::string name(Article article) const override;
    gfx::TileId tile() const final;
    char character() const final;
    Color color() const override;
    std::string disarm_msg() const override;

    // Roll for destruction of the sigil. A message is printed regardless of fail or success (if
    // the terrain is seen).
    //
    // This is possible to override if the specific Sigil shall use a different system than a random
    // chance for destruction (e.g. count down number of turns active, or something else).
    //
    void strain() override;

    // Percent chance to fade when strained.
    virtual int fade_chance_pct() const = 0;
};

class TrapDart : public MechTrapImpl
{
public:
    TrapDart(P pos, Trap* base_trap);

    std::string name(Article article) const override;
    Color color() const override;
    void run_trigger_effect(WasKnownBeforeTrigger was_known_before) override;
    TrapPlacementValid on_place() override;

private:
    bool m_is_poisoned;
    P m_dart_origin {};
    bool m_is_dart_origin_destroyed {false};
};

class TrapSpear : public MechTrapImpl
{
public:
    TrapSpear(P pos, Trap* base_trap);

    std::string name(Article article) const override;
    Color color() const override;
    void run_trigger_effect(WasKnownBeforeTrigger was_known_before) override;
    TrapPlacementValid on_place() override;

private:
    bool m_is_poisoned;
    P m_spear_origin {};
    bool m_is_spear_origin_destroyed {false};
};

class TrapBlindingFlash : public MechTrapImpl
{
public:
    TrapBlindingFlash(P pos, Trap* base_trap);

    std::string name(Article article) const override;
    Color color() const override;
    void run_trigger_effect(WasKnownBeforeTrigger was_known_before) override;
};

class TrapDeafening : public MechTrapImpl
{
public:
    TrapDeafening(P pos, Trap* base_trap);

    std::string name(Article article) const override;
    Color color() const override;
    void run_trigger_effect(WasKnownBeforeTrigger was_known_before) override;
};

class TrapSmoke : public MechTrapImpl
{
public:
    TrapSmoke(P pos, Trap* base_trap);

    std::string name(Article article) const override;
    Color color() const override;
    void run_trigger_effect(WasKnownBeforeTrigger was_known_before) override;
};

class TrapAlarm : public MechTrapImpl
{
public:
    TrapAlarm(P pos, Trap* base_trap);

    std::string name(Article article) const override;
    Color color() const override;
    void run_trigger_effect(WasKnownBeforeTrigger was_known_before) override;
};

class TrapWeb : public MechTrapImpl
{
public:
    TrapWeb(P pos, Trap* base_trap);

    std::string name(Article article) const override;
    Color color() const override;
    gfx::TileId tile() const override;
    char character() const override;
    void run_trigger_effect(WasKnownBeforeTrigger was_known_before) override;

    std::string disarm_msg() const override;
};

class TrapTeleport : public SigilImpl
{
public:
    TrapTeleport(P pos, Trap* const base_trap) :
        SigilImpl(pos, TrapId::teleport, base_trap) {}

    void on_bumped(actor::Actor& actor_bumping) override;

    int fade_chance_pct() const override;
};

class TrapSummonMon : public SigilImpl
{
public:
    TrapSummonMon(P pos, Trap* const base_trap) :
        SigilImpl(pos, TrapId::summon, base_trap) {}

    void on_bumped(actor::Actor& actor_bumping) override;

    int fade_chance_pct() const override;
};

class TrapSlow : public SigilImpl
{
public:
    TrapSlow(P pos, Trap* const base_trap) :
        SigilImpl(pos, TrapId::slow, base_trap) {}

    void on_bumped(actor::Actor& actor_bumping) override;

    int fade_chance_pct() const override;
};

class TrapHaste : public SigilImpl
{
public:
    TrapHaste(P pos, Trap* const base_trap) :
        SigilImpl(pos, TrapId::slow, base_trap) {}

    void on_bumped(actor::Actor& actor_bumping) override;

    int fade_chance_pct() const override;
};

class TrapAlterEnv : public SigilImpl
{
public:
    TrapAlterEnv(P pos, Trap* const base_trap) :
        SigilImpl(pos, TrapId::slow, base_trap) {}

    void on_bumped(actor::Actor& actor_bumping) override;

    int fade_chance_pct() const override;
};

class TrapCurse : public SigilImpl
{
public:
    TrapCurse(P pos, Trap* const base_trap) :
        SigilImpl(pos, TrapId::curse, base_trap) {}

    void on_bumped(actor::Actor& actor_bumping) override;

    int fade_chance_pct() const override;
};

class TrapBless : public SigilImpl
{
public:
    TrapBless(P pos, Trap* const base_trap) :
        SigilImpl(pos, TrapId::bless, base_trap) {}

    void on_bumped(actor::Actor& actor_bumping) override;

    int fade_chance_pct() const override;
};

class TrapUnlearnSpell : public SigilImpl
{
public:
    TrapUnlearnSpell(P pos, Trap* const base_trap) :
        SigilImpl(pos, TrapId::unlearn_spell, base_trap) {}

    void on_bumped(actor::Actor& actor_bumping) override;

    int fade_chance_pct() const override;

private:
    void try_unlearn_for_player() const;
    void try_unlearn_for_monster(actor::Actor& actor) const;
};

class TrapBoundary : public SigilImpl
{
public:
    TrapBoundary(P pos, Trap* const base_trap) :
        SigilImpl(pos, TrapId::boundary, base_trap) {}

    std::string name(Article article) const override;
    Color color() const override;

    void strain() override;

    void on_new_turn() override;

    int fade_chance_pct() const override;

    void set_nr_actions_to_prevent(int nr);

private:
    void destroy();

    // Number of actions left that it can prevent, until it fades.
    int m_nr_actions_countdown {1};
};

}  // namespace terrain

#endif  // TERRAIN_TRAP_HPP

// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#ifndef SPELLS_HPP
#define SPELLS_HPP

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "global.hpp"
#include "random.hpp"

class Spell;
struct P;

namespace terrain
{
class Terrain;
}  // namespace terrain

namespace prop
{
enum class Id;
};  // namespace prop

namespace actor
{
class Actor;
}  // namespace actor

namespace audio
{
enum class SfxId;
};  // namespace audio

namespace terrain
{
enum class DidOpen;
enum class DidClose;
}  // namespace terrain

namespace item
{
class Item;
}  // namespace item

enum class SpellId
{
    //
    // --- AVAILABLE FOR THE PLAYER (and possibly monsters)
    //

    // Domain: Channeling
    aza_gaze,
    cataclysm,
    darkbolt,
    gnawing_torrent,

    // Domain: Corruption
    aura_of_decay,
    curse,
    enfeeble,
    pestilence,
    poison,

    // Domain: Illusion
    invis,
    // NOTE: The Mirror Images spell is NOT supported for monsters, because the player could just
    // view the monster descriptions and see which ones are unusually hard to hit (the mirror images
    // have extremely high dodge), which would just be annoying and ruins the whole aspect of making
    // the images look like the caster.
    //
    // (A similar thing happens with hallucination, but in that case it's a different situation and
    // more OK.)
    //
    mirror_images,
    terrify,
    threat_projection,

    // Domain: Mind
    clairvoyance,
    control_object,
    erudition,
    identify,
    premonition,
    projected_strike,
    see_invis,
    transmut,

    // Domain: Time
    expulsion,
    haste,
    slow,
    teleport,
    temporal_echo,

    // Domain: Warding
    bless,
    cancellation,
    heal,
    inscribe_boundary_sigil,
    light,
    spell_shield,

    // Domain: Blood
    blood_tempering,
    crimson_passage,
    sacrifice_life,
    thorns,

    //
    // --- EXORCIST BACKGROUND ONLY ---
    //

    // (No domain)
    brilliance,
    cleansing_fire,
    purge,
    sanctuary,

    //
    // --- GHOUL BACKGROUND ONLY ---
    //

    // (No domain)
    frenzy,

    //
    // --- FLAGELLANT BACKGROUND ONLY ---
    //

    // Domain: Blood
    shed_impurity,

    //
    // --- MONSTERS ONLY ---
    //

    // (Domain doesn't matter)
    blind,
    burn,
    deafen,
    force_bolt,
    heal_others,
    knockback,
    mi_go_hypno,
    summon_random,
    summon_tentacles,
    summon_water_creature,

    END
};

enum class SpellDomain
{
    channeling,
    corruption,
    illusion,
    mind,
    time,
    warding,

    blood,

    END
};

enum class SpellSkill
{
    basic,
    expert,
    master,
    transcendent
};

enum class SpellSrc
{
    learned,
    manuscript,
    item
};

enum class SpellShock
{
    none,
    mild,
    disturbing,
    severe
};

// Player saw or heard the spell being cast.
enum class PlayerAwareOfCast
{
    no,
    yes,
};

// Does the spell cost spirit or hit points to cast?
enum class SpellCostType
{
    spirit,
    hit_points
};

enum class SpellNameType
{
    plain,
    a,
};

namespace spells
{
Spell* make(SpellId spell_id);

SpellId str_to_spell_id(const std::string& str);

std::string spell_domain_title(SpellDomain domain);

SpellSkill str_to_spell_skill_id(const std::string& str);

std::string skill_to_str(SpellSkill skill);

ShockSrc spell_domain_to_shock_type(SpellDomain domain);

terrain::DidOpen run_opening_spell_effect_at(const P& pos, SpellSkill skill);

terrain::DidClose run_close_spell_effect_at(const P& pos, SpellSkill skill);

void run_mi_go_hypno_effect(actor::Actor& target);

bool allow_properties_prevent_player_intr_spell(SpellId id);

}  // namespace spells

class Spell
{
public:
    Spell() = default;

    virtual ~Spell() = default;

    void cast(
        actor::Actor* caster,
        SpellSkill skill,
        SpellSrc spell_src,
        const std::vector<actor::Actor*>& seen_targets) const;

    virtual void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const = 0;

    virtual bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        const SpellSkill skill) const
    {
        (void)mon;
        (void)seen_targets;
        (void)skill;

        return false;
    }

    virtual int mon_cooldown() const
    {
        return 3;
    }

    virtual std::string name(SpellNameType name_type = SpellNameType::plain) const = 0;

    virtual SpellId id() const = 0;

    virtual SpellDomain domain() const = 0;

    // Casting a memorized tenebrous spell disables it (i.e. single use, until it it re-enabled).
    virtual bool is_tenebrous() const
    {
        return false;
    }

    virtual bool can_be_improved_with_skill() const
    {
        return true;
    }

    std::vector<std::string> descr(SpellSkill skill, SpellSrc spell_src) const;

    std::string domain_descr() const;

    virtual std::vector<std::string> descr_specific(SpellSkill skill) const = 0;

    Range cost_range(SpellSkill skill, const actor::Actor* caster = nullptr) const;

    virtual SpellCostType cost_type() const
    {
        return SpellCostType::spirit;
    }

    int shock_value() const;

    virtual SpellShock shock_type() const = 0;

protected:
    virtual int base_max_cost(SpellSkill skill, const actor::Actor* caster) const = 0;

    virtual bool is_noisy(SpellSkill skill) const = 0;

    void on_resist(actor::Actor& target, const actor::Actor* caster) const;

    bool m_is_disabled {false};
};

class SpellCurse : public Spell
{
public:
    SpellCurse() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range duration_range(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    int pct_chance_doom(SpellSkill skill) const;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellPoison : public Spell
{
public:
    SpellPoison() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range duration_range(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellEnfeeble : public Spell
{
public:
    SpellEnfeeble() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range duration_range(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellTemporalEcho : public Spell
{
public:
    SpellTemporalEcho() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range duration_range() const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    int pct_damage_dealt(SpellSkill skill) const;

    void apply_temporal_echo_effect(
        actor::Actor& target,
        SpellSkill skill,
        int duration) const;
};

class SpellSlow : public Spell
{
public:
    SpellSlow() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range duration_range(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellTerrify : public Spell
{
public:
    SpellTerrify() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    Range duration_range(SpellSkill skill) const;

    int faint_pct_chance(SpellSkill skill) const;
    Range faint_duration_range() const;

    void terrify_target(actor::Actor& target, SpellSkill skill) const;
    void faint_target(actor::Actor& target) const;
};

class SpellThreatProjection : public Spell
{
public:
    SpellThreatProjection() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    Range duration_range(SpellSkill skill) const;

    void conflict_target(actor::Actor& target, SpellSkill skill) const;
};

class SpellAuraOfDecay : public Spell
{
public:
    SpellAuraOfDecay() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    Range dmg_range(SpellSkill skill) const;

    Range duration_range(SpellSkill skill) const;
};

class BoltImpl
{
public:
    virtual ~BoltImpl() = default;

    virtual SpellShock shock_type() const;

    virtual Range damage(SpellSkill skill) const = 0;

    virtual void on_hit(
        actor::Actor& actor_hit,
        actor::Actor& caster,
        SpellSkill skill) const = 0;

    virtual std::string hit_msg_ending() const = 0;

    virtual audio::SfxId impact_sfx() const;

    virtual int mon_cooldown() const = 0;

    virtual std::string name(SpellNameType name_type) const = 0;

    virtual SpellId id() const = 0;

    virtual std::vector<std::string> descr_specific(SpellSkill skill) const = 0;

    virtual int base_max_cost(SpellSkill skill, const actor::Actor* caster) const = 0;

    virtual int nr_projectiles(SpellSkill skill) const
    {
        (void)skill;

        return 1;
    }
};

class ForceBolt : public BoltImpl
{
public:
    ForceBolt() = default;

    Range damage(SpellSkill skill) const override;

    void on_hit(
        actor::Actor& actor_hit,
        actor::Actor& caster,
        SpellSkill skill) const override;

    std::string hit_msg_ending() const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;
};

class Darkbolt : public BoltImpl
{
public:
    Darkbolt() = default;

    Range damage(SpellSkill skill) const override;

    void on_hit(
        actor::Actor& actor_hit,
        actor::Actor& caster,
        SpellSkill skill) const override;

    std::string hit_msg_ending() const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;
};

class GnawingTorrent : public BoltImpl
{
public:
    GnawingTorrent() = default;

    Range damage(SpellSkill skill) const override;

    void on_hit(
        actor::Actor& actor_hit,
        actor::Actor& caster,
        SpellSkill skill) const override;

    std::string hit_msg_ending() const override;

    audio::SfxId impact_sfx() const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    int nr_projectiles(SpellSkill skill) const override;
};

class SpellBolt : public Spell
{
public:
    SpellBolt(BoltImpl* impl) :
        m_impl(impl) {}

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    void draw_projectile_travel(
        const actor::Actor& caster,
        const actor::Actor& target,
        SpellSkill skill) const;

    void run_bolt_on_target(
        actor::Actor& caster,
        actor::Actor& target,
        SpellSkill skill,
        PlayerAwareOfCast player_aware) const;

    std::unique_ptr<BoltImpl> m_impl;
};

class SpellAzaGaze : public Spell
{
public:
    SpellAzaGaze() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    Range dmg_range(SpellSkill skill) const;

    Range faint_duration_range(SpellSkill skill) const;

    Range conflict_duration_range(SpellSkill skill) const;

    void run_effect_on_target(
        actor::Actor* caster,
        actor::Actor& target,
        SpellSkill skill) const;

    void do_damage_on_target(
        actor::Actor& target,
        SpellSkill skill,
        actor::Actor* caster) const;

    void apply_properties_on_target(
        actor::Actor& target,
        SpellSkill skill) const;
};

class SpellCataclysm : public Spell
{
public:
    SpellCataclysm() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int destruction_radi(SpellSkill skill) const;

    int nr_explosions(SpellSkill skill) const;

    int nr_destruction_sweeps(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellPestilence : public Spell
{
public:
    SpellPestilence() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int nr_rats_summoned(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    Range duration_range(SpellSkill skill) const;

    void on_rat_summoned(actor::Actor* mon, SpellSkill skill) const;
};

class SpellMirrorImages : public Spell
{
public:
    SpellMirrorImages() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int nr_mirror_images_summoned(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    Range duration_range(SpellSkill skill) const;

    void on_mirror_image_summoned(actor::Actor* mon, SpellSkill skill) const;
};

class SpellProjectedStrike : public Spell
{
public:
    SpellProjectedStrike() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    std::vector<const item::Item*> get_weapons(SpellSkill skill) const;

    int max_nr_weapons(SpellSkill skill) const;

    int hit_chance_bonus(SpellSkill skill) const;
};

class SpellControlObject : public Spell
{
public:
    SpellControlObject() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    int max_dist(SpellSkill skill) const;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellBrilliance : public Spell
{
public:
    SpellBrilliance() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    void run_effect_on_creature(actor::Actor* actor, SpellSkill skill) const;

    Range light_dmg_range(SpellSkill skill) const;

    Range light_sensitive_duration_range(SpellSkill skill) const;
};

class SpellCleansingFire : public Spell
{
public:
    SpellCleansingFire() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
    Range burn_duration_range() const;
};

class SpellSanctuary : public Spell
{
public:
    SpellSanctuary() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    Range duration(SpellSkill skill) const;
};

class SpellPurge : public Spell
{
public:
    SpellPurge() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    bool can_be_improved_with_skill() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    Range dmg_range() const;

    Range fear_duration_range() const;
};

class SpellFrenzy : public Spell
{
public:
    SpellFrenzy() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    bool can_be_improved_with_skill() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellBless : public Spell
{
public:
    SpellBless() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

    Range duration_range(SpellSkill skill) const;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

enum class CancelledPropIncludeInDescr
{
    no,
    yes,
};

enum class CancelledPropAllowPermanent
{
    no,
    yes,
};

struct CancelledPropData
{
    prop::Id id;

    // Include it in the lits of cancelled effects?
    CancelledPropIncludeInDescr include_in_descr {CancelledPropIncludeInDescr::yes};

    // Allow cancel permanent effects?
    CancelledPropAllowPermanent allow_cancel_permanent_effect {CancelledPropAllowPermanent::no};
};

class SpellCancellation : public Spell
{
public:
    SpellCancellation() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int mon_cooldown() const override;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int max_dist(SpellSkill skill) const;

    std::vector<CancelledPropData> negative_effect_types_cancelled() const;
    std::vector<CancelledPropData> positive_effect_types_cancelled() const;

    void run_effect_on_actor(actor::Actor& actor, actor::Actor& caster) const;
    void cancel_negative_effects(actor::Actor& actor) const;
    void cancel_positive_effects(actor::Actor& actor) const;
    Range damage_for_vulnerable_creatures() const;
    void do_damage_vulnerable_creature(actor::Actor& actor, actor::Actor& caster) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellInscribeBoundarySigil : public Spell
{
public:
    SpellInscribeBoundarySigil() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range nr_actions_prevented(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    terrain::Terrain* make_trap_mimic_terrain(const P& from_pos) const;
};

class SpellTransmut : public Spell
{
public:
    SpellTransmut() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    bool is_tenebrous() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int skill_bon(SpellSkill skill) const;

    int chance_scroll(SpellSkill skill) const;

    int chance_potion(SpellSkill skill) const;

    int chance_weapon(SpellSkill skill, int plus) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellClairvoyance : public Spell
{
public:
    SpellClairvoyance() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    bool is_tenebrous() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range duration_range(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellLight : public Spell
{
public:
    SpellLight() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range light_duration_range(SpellSkill skill) const;

    Range blind_duration_range(SpellSkill skill) const;

    Range burning_duration_range() const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellKnockBack : public Spell
{
public:
    SpellKnockBack() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellTeleport : public Spell
{
public:
    SpellTeleport() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int invis_duration(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    int max_dist(SpellSkill skill) const;
};

class SpellExpulsion : public Spell
{
public:
    SpellExpulsion() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    int max_dist(SpellSkill skill) const;
};

class SpellInvis : public Spell
{
public:
    SpellInvis() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    bool is_tenebrous() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range duration_range(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellSeeInvis : public Spell
{
public:
    SpellSeeInvis() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range duration_range(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellSpellShield : public Spell
{
public:
    SpellSpellShield() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellHaste : public Spell
{
public:
    SpellHaste() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range duration_range(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellPremonition : public Spell
{
public:
    SpellPremonition() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

private:
    Range duration_range(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellErudition : public Spell
{
public:
    SpellErudition() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    Range get_duration_range(SpellSkill skill) const;
};

class SpellIdentify : public Spell
{
public:
    SpellIdentify() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    bool is_tenebrous() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellBloodTempering : public Spell
{
public:
    SpellBloodTempering() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    bool is_tenebrous() const override;

    SpellCostType cost_type() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range duration_range(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellThorns : public Spell
{
public:
    SpellThorns() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellCostType cost_type() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    Range duration_range(SpellSkill skill) const;
    Range dmg_range(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellCrimsonPassage : public Spell
{
public:
    SpellCrimsonPassage() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellCostType cost_type() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int nr_steps_allowed(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellSacrificeLife : public Spell
{
public:
    SpellSacrificeLife() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    bool is_tenebrous() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int nr_sp_per_hp(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellShedImpurity : public Spell
{
public:
    SpellShedImpurity() = default;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int get_min_hp_removed_for_bonus_effects() const;

    int get_moribund_hp_limit() const;

    int calc_nr_hp_removed(const actor::Actor* caster) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellBlind : public Spell
{
public:
    SpellBlind() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    std::vector<actor::Actor*> find_actors_not_blind_resistant(
        const std::vector<actor::Actor*>& actors) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SummonImpl
{
public:
    virtual ~SummonImpl() = default;

    virtual SpellId id() const = 0;

    // NOTE: The input to this function is any summonable monster within a certain level range
    // depending on the monsters skill level.
    virtual std::vector<std::string> filter_allowed_ids(
        const std::vector<std::string>& summon_bucket) const = 0;

    virtual int mon_cooldown() const;

    virtual std::string appear_msg_override() const;
};

class SummonRandom : public SummonImpl
{
public:
    SummonRandom() = default;

    SpellId id() const override;

    std::vector<std::string> filter_allowed_ids(
        const std::vector<std::string>& summon_bucket) const override;
};

class SummonWaterCreature : public SummonImpl
{
public:
    SummonWaterCreature() = default;

    SpellId id() const override;

    std::vector<std::string> filter_allowed_ids(
        const std::vector<std::string>& summon_bucket) const override;
};

class SummonTentacles : public SummonImpl
{
public:
    SummonTentacles() = default;

    SpellId id() const override;

    std::vector<std::string> filter_allowed_ids(
        const std::vector<std::string>& summon_bucket) const override;

    int mon_cooldown() const override;

    std::string appear_msg_override() const override;
};

class SpellSummon : public Spell
{
public:
    SpellSummon(SummonImpl* impl) :
        m_impl(impl) {}

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    // NOTE: There is no way for the summon implementation classes to control the allowed
    // dungeon level range of the monsters. For spells that should summon a specific monster,
    // make sure that the monster is in range for the summoners spell skill!
    Range get_allowed_mon_lvl_range(SpellSkill skill) const;

    std::vector<std::string> make_summon_bucket(const Range& mon_lvl_range) const;

    void summon(const std::string& id, actor::Actor* caster) const;

    std::unique_ptr<SummonImpl> m_impl;
};

class SpellHeal : public Spell
{
public:
    SpellHeal() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int nr_hp_restored(SpellSkill skill) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;

    Range regen_duration() const;
};

class SpellMiGoHypno : public Spell
{
public:
    SpellMiGoHypno() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellBurn : public Spell
{
public:
    SpellBurn() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellDeafen : public Spell
{
public:
    SpellDeafen() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

class SpellHealOthers : public Spell
{
public:
    SpellHealOthers() = default;

    bool allow_mon_cast_now(
        const actor::Actor& mon,
        const std::vector<actor::Actor*>& seen_targets,
        SpellSkill skill) const override;

    int mon_cooldown() const override;

    std::string name(SpellNameType name_type) const override;

    SpellId id() const override;

    SpellDomain domain() const override;

    SpellShock shock_type() const override;

    std::vector<std::string> descr_specific(SpellSkill skill) const override;

    void run_effect(
        actor::Actor* caster,
        SpellSkill skill,
        const std::vector<actor::Actor*>& seen_targets,
        PlayerAwareOfCast player_aware) const override;

private:
    std::vector<actor::Actor*> find_possible_actors_to_heal(const actor::Actor* caster) const;

    actor::Actor* find_random_actor_to_heal(const actor::Actor* caster) const;

    int base_max_cost(SpellSkill skill, const actor::Actor* caster) const override;

    bool is_noisy(SpellSkill skill) const override;
};

#endif  // SPELLS_HPP

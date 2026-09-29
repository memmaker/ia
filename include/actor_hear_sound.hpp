// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#ifndef ACTOR_HEAR_SOUND_HPP
#define ACTOR_HEAR_SOUND_HPP

#include "direction.hpp"

class Snd;

namespace actor
{
class Actor;

void hear_sound_player(
    Snd& snd,
    bool is_origin_seen_by_player,
    Dir dir_to_origin,
    int percent_audible_distance);

void hear_sound_mon(Actor& actor, Snd& snd);

}  // namespace actor

#endif  // ACTOR_HEAR_SOUND_HPP

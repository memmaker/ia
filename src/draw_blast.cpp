// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#include "draw_blast.hpp"

#include <algorithm>

#include "actor.hpp"
#include "array2.hpp"
#include "config.hpp"
#include "debug.hpp"
#include "gfx.hpp"
#include "io.hpp"
#include "map.hpp"
#include "pos.hpp"
#include "state.hpp"
#include "viewport.hpp"

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Public
// -----------------------------------------------------------------------------
void draw_blast_at_cells(
    const std::vector<P>& positions,
    const Color& color,
    const int delay_div)
{
    TRACE_FUNC_BEGIN;

    states::draw();

    for (const P& pos : positions) {
        if (!viewport::is_in_view(pos)) {
            continue;
        }

        io::MapDrawObj draw_obj;
        draw_obj.tile = gfx::TileId::blast1;
        draw_obj.character = '*';
        draw_obj.pos = viewport::to_view_pos(pos);
        draw_obj.color = color;

        draw_obj.draw();
    }

    io::update_screen();

    const int delay_factor = 5;

    io::sleep((config::base_delay() * delay_factor) / delay_div);

    states::draw();

    for (const P& pos : positions) {
        if (!viewport::is_in_view(pos)) {
            continue;
        }

        io::MapDrawObj draw_obj;
        draw_obj.tile = gfx::TileId::blast2;
        draw_obj.character = '*';
        draw_obj.pos = viewport::to_view_pos(pos);
        draw_obj.color = color;

        draw_obj.draw();
    }

    io::update_screen();

    io::sleep((config::base_delay() * delay_factor) / delay_div);

    TRACE_FUNC_END;
}

void draw_blast_at_seen_cells(
    const std::vector<P>& positions,
    const Color& color,
    const int delay_div)
{
    std::vector<P> positions_with_vision;

    for (const P& p : positions) {
        if (map::g_seen.at(p)) {
            positions_with_vision.push_back(p);
        }
    }

    if (!positions_with_vision.empty()) {
        draw_blast_at_cells(positions_with_vision, color, delay_div);
    }
}

void draw_blast_at_seen_actors(
    const std::vector<actor::Actor*>& actors,
    const Color& color,
    const int delay_div)
{
    // if (!panels::is_valid())
    // {
    //         return;
    // }

    std::vector<P> positions;

    positions.reserve(actors.size());

    for (auto* const actor : actors) {
        positions.push_back(actor->m_pos);
    }

    draw_blast_at_seen_cells(positions, color, delay_div);
}

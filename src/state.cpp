// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#include "state.hpp"
#include "web.hpp"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

#include "debug.hpp"
#include "io.hpp"

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------
static std::vector<std::unique_ptr<State>> s_current_states;

static void run_state_iteration()
{
    const State* const current_state = states::current_state();

    states::start();

    if ((states::current_state()) != current_state) {
        // The state that was the current state before running "start"
        // has been removed, or another state has been pushed on it. In
        // either case rerun the iteration from start (otherwise "draw"
        // and "update" will run for a different state than the one that
        // was started).
        return;
    }

    states::draw();
    io::update_screen();

    states::update();
}

//-----------------------------------------------------------------------------
// states
//-----------------------------------------------------------------------------
namespace states
{
void init()
{
    TRACE_FUNC_BEGIN;

    cleanup();

    TRACE_FUNC_END;
}

void cleanup()
{
    TRACE_FUNC_BEGIN;

    s_current_states.resize(0);

    TRACE_FUNC_END;
}

void run()
{
    TRACE_FUNC_BEGIN;

    while (!is_empty()) {
        run_state_iteration();
    }

    TRACE_FUNC_END;
}

void run_until_state_done(std::unique_ptr<State> state)
{
    TRACE_FUNC_BEGIN;

    State* state_ptr = state.get();

    push(std::move(state));

    while (contains_state(state_ptr)) {
        run_state_iteration();
    }

    TRACE_FUNC_END;
}

void start()
{
    while (!is_empty() && !s_current_states.back()->has_started()) {
        auto& state = s_current_states.back();

        state->set_started();

        // NOTE: This may cause states to be pushed/popped - do not use
        // the "state" pointer beyond this call!
        state->on_start();
    }
}

void cycle_graphics(const io::GraphicsCycle cycle)
{
    if (is_empty()) {
        return;
    }

    // Find the first state to draw from.
    auto cycle_from = std::end(s_current_states);

    while (cycle_from != std::begin(s_current_states)) {
        --cycle_from;

        const auto& state_ptr = *cycle_from;

        // If not drawn overlayed, cycle graphics from this state as
        // bottom layer (but only if the state has been started and
        // drawing is not paused for the state).
        if (!state_ptr->draw_overlayed() &&
            state_ptr->has_started() &&
            !state_ptr->is_drawing_disabled()) {
            break;
        }
    }

    // Cycle graphics in every state from this state onward.
    for (; cycle_from != std::end(s_current_states); ++cycle_from) {
        const auto& state_ptr = *cycle_from;

        // Do NOT cycle graphics in states which are not yet started
        // (they may need to set up menus etc in their start function,
        // and expect the chance to do so before cycling is called).
        // Also do not cycle graphics if drawing is disabled.

        if (state_ptr->has_started() &&
            !state_ptr->is_drawing_disabled()) {
            state_ptr->cycle_graphics(cycle);
        }
    }
}

void draw()
{
    if (is_empty()) {
        return;
    }

    io::clear_screen();

    // Find the first state to draw from.
    auto draw_from = std::end(s_current_states);

    while (draw_from != std::begin(s_current_states)) {
        --draw_from;

        const auto& state_ptr = *draw_from;

        // If not drawn overlayed, cycle graphics from this state as
        // bottom layer (but only if the state has been started and
        // drawing is not paused for the state).
        if (!state_ptr->draw_overlayed() &&
            state_ptr->has_started() &&
            !state_ptr->is_drawing_disabled()) {
            break;
        }
    }

#ifdef __EMSCRIPTEN__
    // Web: in a game the map stays on the canvas; text states over it
    // (menus, pop-ups, inventory, character sheet...) become an HTML pop-up.
    web::popup_clear();

    auto game_it = std::end(s_current_states);

    for (auto it = std::begin(s_current_states); it != std::end(s_current_states); ++it) {
        if ((*it)->id() == StateId::game) {
            game_it = it;
        }
    }

    if (game_it != std::end(s_current_states) &&
        (*game_it)->has_started() &&
        !(*game_it)->is_drawing_disabled()) {
        draw_from = game_it;
    }
#endif  // __EMSCRIPTEN__

    // Draw every state from this state onward.
    for (; draw_from != std::end(s_current_states); ++draw_from) {
        const auto& state_ptr = *draw_from;

        // Do NOT draw states which are not yet started (they may need
        // to set up menus etc in their start function, and expect the
        // chance to do so before drawing is called). Also do not draw
        // if drawing is disabled.

        if (state_ptr->has_started() &&
            !state_ptr->is_drawing_disabled()) {
#ifdef __EMSCRIPTEN__
            const StateId id = state_ptr->id();

            // Screens that are text also outside a game (death summary,
            // high scores, manual, options from the main menu).
            const bool is_text_screen =
                (id == StateId::game_over_summary) ||
                (id == StateId::highscore) ||
                (id == StateId::browse_highscore_entry) ||
                (id == StateId::manual) ||
                (id == StateId::manual_page) ||
                (id == StateId::options) ||
                (id == StateId::options_submenu);

            const bool is_text =
                is_text_screen ||
                ((game_it != std::end(s_current_states)) &&
                 (id != StateId::game) &&
                 (id != StateId::marker) &&
                 (id != StateId::view_minimap));

            if (is_text) {
                web::popup_begin();
            }

            state_ptr->draw();

            web::popup_end();
#else
            state_ptr->draw();
#endif  // __EMSCRIPTEN__
        }
    }
}

void on_window_resized()
{
    for (auto& state : s_current_states) {
        state->on_window_resized();
    }
}

void update()
{
    if (is_empty()) {
        return;
    }

    s_current_states.back()->update();
}

void push(std::unique_ptr<State> state)
{
    TRACE_FUNC_BEGIN;

    // Pause the current state
    if (!is_empty()) {
        s_current_states.back()->on_pause();
    }

    s_current_states.push_back(std::move(state));

    s_current_states.back()->on_pushed();

    io::clear_input();

    TRACE_FUNC_END;
}

void pop()
{
    TRACE_FUNC_BEGIN;

    if (is_empty()) {
        TRACE_FUNC_END;

        return;
    }

    s_current_states.back()->on_popped();

    s_current_states.pop_back();

    if (!is_empty()) {
        s_current_states.back()->on_resume();
    }

    io::clear_input();

    TRACE_FUNC_END;
}

void pop_all()
{
    TRACE_FUNC_BEGIN;

    while (!is_empty()) {
        s_current_states.back()->on_popped();

        s_current_states.pop_back();
    }

    TRACE_FUNC_END;
}

bool contains_state(const StateId id)
{
    return (
        std::any_of(
            std::cbegin(s_current_states),
            std::cend(s_current_states),
            [id](const auto& state) {
                return state->id() == id;
            }));
}

bool contains_state(const State* const state)
{
    return (
        std::any_of(
            std::cbegin(s_current_states),
            std::cend(s_current_states),
            [state](const auto& state_found) {
                return state_found.get() == state;
            }));
}

void pop_until(const StateId id)
{
    TRACE_FUNC_BEGIN;

    if (is_empty() || !contains_state(id)) {
        ASSERT(false);

        return;
    }

    while (s_current_states.back().get()->id() != id) {
        pop();
    }

    TRACE_FUNC_END;
}

bool is_current_state(const State* const state)
{
    if (is_empty()) {
        return false;
    }

    return state == s_current_states.back().get();
}

bool is_current_state(const StateId id)
{
    if (is_empty()) {
        return false;
    }

    return s_current_states.back().get()->id() == id;
}

State* current_state()
{
    if (is_empty()) {
        return nullptr;
    }

    return s_current_states.back().get();
}

bool is_empty()
{
    return s_current_states.empty();
}

}  // namespace states

// RVIP: auto-explore (X) and stair walks (>, <).
//
// BFS over the cells the player has seen (own per-level map: the game does
// not memorize dark floor), one step per player turn. Hooked in
// actor_act.cpp next to the game's auto move.

#include "explore.hpp"

#include <SDL.h>
#include <climits>
#include <deque>
#include <string>
#include <vector>

#include "actor.hpp"
#include "actor_player_state.hpp"
#include "actor_see.hpp"
#include "array2.hpp"
#include "colors.hpp"
#include "io.hpp"
#include "map.hpp"
#include "msg_log.hpp"
#include "state.hpp"
#include "terrain.hpp"
#include "terrain_data.hpp"
#include "terrain_door.hpp"
#include "terrain_trap.hpp"

namespace explore
{
static Mode s_mode = Mode::off;
static bool s_new_msg = false;
static bool s_ignore_msgs = false;
static size_t s_nr_foes_at_start = 0;
static int s_level_dlvl = -1;
static Array2<bool> s_known(0, 0);
static Array2<bool> s_visited(0, 0);
static Array2<bool> s_skip(0, 0);  // locked / stuck doors
static P s_last_target(-1, -1);
static P s_last_step(0, 0);

static void say(const std::string& str)
{
    s_ignore_msgs = true;

    msg_log::add(
        str,
        colors::text(),
        MsgInterruptPlayer::no,
        MorePromptOnMsg::no,
        CopyToMsgHistory::no);

    s_ignore_msgs = false;
}

static void update_level()
{
    if ((s_level_dlvl != map::g_dlvl) || (s_known.dims() != map::dims())) {
        s_level_dlvl = map::g_dlvl;
        s_known.resize(map::dims(), false);
        s_visited.resize(map::dims(), false);
        s_skip.resize(map::dims(), false);
    }

    for (const P& p : map::positions()) {
        if (map::g_seen.at(p) ||
            map::g_terrain_memory.at(p).appearance.is_defined()) {
            s_known.at(p) = true;
        }
    }

    s_visited.at(map::g_player->m_pos) = true;
}

static bool is_known_stairs(const P& p)
{
    return s_known.at(p) &&
        (map::g_terrain.at(p)->id() == terrain::Id::stairs) &&
        !map::g_terrain.at(p)->is_hidden();
}

static bool is_closed_door(const P& p)
{
    const auto* t = map::g_terrain.at(p);

    return (t->id() == terrain::Id::door) &&
        !t->is_hidden() &&
        !static_cast<const terrain::Door*>(t)->is_open();
}

// allow_blocked: also cross known traps and skipped doors (to tell the
// player why exploring stopped).
static bool is_passable(const P& p, const bool allow_blocked)
{
    if (!s_known.at(p)) {
        return false;
    }

    const auto* t = map::g_terrain.at(p);
    const auto id = t->id();

    if (id == terrain::Id::stairs) {
        return false;
    }

    if (is_closed_door(p)) {
        return allow_blocked || !s_skip.at(p);
    }

    if (!t->can_move(*map::g_player)) {
        return false;
    }

    if (allow_blocked) {
        return true;
    }

    if ((id == terrain::Id::trap) &&
        !static_cast<const terrain::Trap*>(t)->is_hidden()) {
        return false;
    }

    return (id != terrain::Id::chains) &&
        (id != terrain::Id::liquid) &&
        (id != terrain::Id::vines) &&
        !t->is_burning();
}

static bool is_adj_to_known_stairs(const P& p)
{
    for (const P& d : dir_utils::g_dir_list) {
        const P n = p + d;

        if (map::is_pos_inside_map(n) && is_known_stairs(n)) {
            return true;
        }
    }

    return false;
}

static bool is_target(const P& p)
{
    if (s_mode == Mode::stairs) {
        return is_adj_to_known_stairs(p);
    }

    if (is_closed_door(p)) {
        return !s_skip.at(p);
    }

    if (s_visited.at(p)) {
        return false;
    }

    if (map::g_item_memory.at(p).appearance.is_defined()) {
        return true;
    }

    for (const P& d : dir_utils::g_dir_list) {
        const P n = p + d;

        if (map::is_pos_inside_map(n) && !s_known.at(n)) {
            return true;
        }
    }

    return false;
}

// Returns the first step towards the nearest target, or Dir::END
static Dir bfs(const bool allow_blocked)
{
    const P start = map::g_player->m_pos;
    Array2<P> from(map::dims());
    Array2<bool> done(0, 0);
    done.resize(map::dims(), false);

    std::deque<P> queue {start};
    done.at(start) = true;

    while (!queue.empty()) {
        const P p = queue.front();
        queue.pop_front();

        if ((p != start) && is_target(p)) {
            s_last_target = p;

            P step = p;

            while (from.at(step) != start) {
                step = from.at(step);
            }

            return dir_utils::dir(step - start);
        }

        // Closed doors are entered (opened) but not walked through in the
        // same search, the next step re-plans.
        if ((p != start) && is_closed_door(p)) {
            continue;
        }

        for (const P& d : dir_utils::g_dir_list) {
            const P n = p + d;

            if (!map::is_pos_inside_map(n) || done.at(n)) {
                continue;
            }

            if (!is_passable(n, allow_blocked)) {
                continue;
            }

            done.at(n) = true;
            from.at(n) = p;
            queue.push_back(n);
        }
    }

    return Dir::END;
}

static bool key_pressed()
{
    SDL_PumpEvents();

    if (SDL_HasEvent(SDL_KEYDOWN)) {
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

        return true;
    }

    return false;
}

static void start(const Mode mode)
{
    s_mode = mode;
    s_new_msg = false;
    s_nr_foes_at_start = actor::seen_foes(*map::g_player).size();
}

void stop()
{
    s_mode = Mode::off;
}

bool is_active()
{
    return s_mode != Mode::off;
}

void on_msg()
{
    if (!s_ignore_msgs) {
        s_new_msg = true;
    }
}

void cmd_explore()
{
    start(Mode::explore);
}

void cmd_descend()
{
    update_level();

    const P& pos = map::g_player->m_pos;

    for (const P& d : dir_utils::g_dir_list) {
        const P n = pos + d;

        if (map::is_pos_inside_map(n) && is_known_stairs(n)) {
            // Next to the stairs: take them (the game has the player step
            // onto the stairs and descend in one go).
            static_cast<terrain::Stairs*>(map::g_terrain.at(n))->descend();

            return;
        }
    }

    start(Mode::stairs);
}

void cmd_ascend()
{
    say("There is no way back up, only down.");
}

Dir next_dir()
{
    if (s_mode == Mode::off) {
        return Dir::END;
    }

    // Paint the last step and give the player a chance to interrupt.
    states::draw();
    io::update_screen();
    SDL_Delay(40);

    if (key_pressed()) {
        stop();
        return Dir::END;
    }

    if (s_new_msg) {
        stop();
        return Dir::END;
    }

    update_level();

    const auto foes = actor::seen_foes(*map::g_player);

    if (!foes.empty() &&
        ((s_mode == Mode::explore) || (foes.size() > s_nr_foes_at_start))) {
        stop();
        say("In view: " + actor::name_a(*foes[0]) + ".");
        return Dir::END;
    }

    if (s_mode == Mode::stairs) {
        s_nr_foes_at_start = foes.size();

        if (is_adj_to_known_stairs(map::g_player->m_pos)) {
            stop();
            say("The stairs are here: press > again to descend.");
            return Dir::END;
        }
    }

    const Dir dir = bfs(false);

    if (dir != Dir::END) {
        s_last_step = map::g_player->m_pos + dir_utils::offset(dir);
        return dir;
    }

    const Mode mode = s_mode;
    stop();

    if (mode == Mode::stairs) {
        say("I know of no way down yet.");
    }
    else if (bfs(true) != Dir::END) {
        say("Known traps or blocked doors bar the rest of the way.");
    }
    else {
        say("Nothing left to explore (maybe search for hidden doors).");
    }

    return Dir::END;
}

void after_step(const P& pos_before)
{
    const P& pos = map::g_player->m_pos;

    if (pos != pos_before) {
        return;
    }

    // NOTE: A stuck door's message may already have stopped us (the
    // game interrupts on it), mark it anyway so explore never kicks it.
    if (is_closed_door(s_last_step)) {
        s_skip.at(s_last_step) = true;
    }

    if (s_mode == Mode::off) {
        return;
    }

    // Did not move: either a door was bumped, or something blocked us.
    if (is_closed_door(s_last_step)) {
        // Still closed (locked, stuck, warded): skip it from now on.
        s_skip.at(s_last_step) = true;
        s_new_msg = false;
        return;
    }

    if (map::g_terrain.at(s_last_step)->id() == terrain::Id::door) {
        // Opened it: its own message must not stop us.
        s_new_msg = false;
        return;
    }

    stop();
}

}  // namespace explore

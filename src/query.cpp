// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#include "query.hpp"

#include <memory>
#include <string>

#include "SDL_keycode.h"
#include "config.hpp"
#include "game_commands.hpp"
#include "io.hpp"
#include "popup.hpp"
#include "state.hpp"

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------
static bool s_is_inited = false;

static bool s_is_waiting_for_yes_no = false;

// -----------------------------------------------------------------------------
// query
// -----------------------------------------------------------------------------
namespace query
{
void init()
{
    s_is_inited = true;
}

void cleanup()
{
    s_is_inited = false;
}

void wait_for_key_press()
{
    if (s_is_inited && !config::is_bot_playing()) {
        states::draw();
        io::update_screen();

        io::read_input();
    }
}

BinaryAnswer yes_or_no(
    std::optional<char> key_for_special_event,
    const AllowSpaceCancel allow_space_cancel)
{
    if (!s_is_inited || config::is_bot_playing()) {
        return BinaryAnswer::yes;
    }

    s_is_waiting_for_yes_no = true;

    states::draw();
    io::update_screen();

    io::InputData input;

    BinaryAnswer result = BinaryAnswer::no;

    while (true) {
        input = io::read_input();

        const bool is_canceled_with_space =
            (input.key == SDLK_SPACE) &&
            (allow_space_cancel == AllowSpaceCancel::yes);

        if (is_canceled_with_space ||
            (input.key == 'n') ||
            (input.key == SDLK_ESCAPE)) {
            result = BinaryAnswer::no;
            break;
        }

        if ((input.key == 'y') ||
            (input.key == SDLK_RETURN)) {
            result = BinaryAnswer::yes;
            break;
        }

        const bool is_special_key_pressed =
            key_for_special_event.has_value() &&
            (input.key == key_for_special_event.value());

        if (is_special_key_pressed) {
            result = BinaryAnswer::special;
            break;
        }
    }

    s_is_waiting_for_yes_no = false;

    return result;
}

bool is_waiting_for_yes_no()
{
    return s_is_waiting_for_yes_no;
}

io::InputData letter(const bool accept_enter)
{
    io::InputData input;

    if (!s_is_inited || config::is_bot_playing()) {
        input.key = 'a';

        return input;
    }

    states::draw();
    io::update_screen();

    while (true) {
        input = io::read_input();

        if ((accept_enter && (input.key == SDLK_RETURN)) ||
            (input.key == SDLK_ESCAPE) ||
            (input.key == SDLK_SPACE) ||
            ((input.key >= 'a') && (input.key <= 'z')) ||
            ((input.key >= 'A') && (input.key <= 'Z'))) {
            return input;
        }
    }

    // Unreachable
    return input;
}

int number(
    const QueryNumberConfig& config,
    const std::string& title,
    const std::string& msg)
{
    if (!s_is_inited || config::is_bot_playing()) {
        return 0;
    }

    int result = 0;

    auto popup = std::make_unique<popup::Popup>(popup::AddToMsgHistory::no);

    popup->set_title(title);
    popup->set_msg(msg);

    popup->setup_number_query_mode(config, &result);

    popup->run();

    return result;
}

void wait_for_msg_more()
{
    if (!s_is_inited || config::is_bot_playing()) {
        return;
    }

    states::draw();
    io::update_screen();

    // Determine criteria for confirming more prompt (decided by config)
    if (config::is_any_key_confirm_more()) {
        wait_for_key_press();
    }
    else {
        // Only some keys confirm more prompts
        while (true) {
            const auto input = io::read_input();

            if ((input.key == SDLK_SPACE) ||
                (input.key == SDLK_ESCAPE) ||
                (input.key == SDLK_RETURN) ||
                (input.key == SDLK_TAB)
#ifndef NDEBUG
                ||
                // Cheat key for descending
                (input.key == SDLK_F2) ||
                // Cheat key for teleporting
                (input.key == SDLK_F7)
#endif  // NDEBUG
            ) {
                break;
            }
        }
    }
}

void wait_for_confirm()
{
    if (!s_is_inited || config::is_bot_playing()) {
        return;
    }

    states::draw();
    io::update_screen();

    while (true) {
        const auto input = io::read_input();

        if ((input.key == SDLK_SPACE) ||
            (input.key == SDLK_ESCAPE) ||
            (input.key == SDLK_RETURN)) {
            break;
        }
    }
}

Dir dir(const AllowCenter allow_center)
{
    if (!s_is_inited || config::is_bot_playing()) {
        return Dir::END;
    }

    states::draw();
    io::update_screen();

    while (true) {
        const auto input = io::read_input();

        const auto game_cmd = game_commands::to_cmd(input);

        switch (game_cmd) {
        case GameCmd::right:
            return Dir::right;

        case GameCmd::down:
            return Dir::down;

        case GameCmd::left:
            return Dir::left;

        case GameCmd::up:
            return Dir::up;

        case GameCmd::down_right:
            return Dir::down_right;

        case GameCmd::up_right:
            return Dir::up_right;

        case GameCmd::down_left:
            return Dir::down_left;

        case GameCmd::up_left:
            return Dir::up_left;

        case GameCmd::wait:
            if (allow_center == AllowCenter::yes) {
                return Dir::center;
            }
            break;

        default:
            break;
        }

        if ((input.key == SDLK_SPACE) || (input.key == SDLK_ESCAPE)) {
            return Dir::END;
        }
    }

    // Unreachable
    return Dir::END;
}

}  // namespace query

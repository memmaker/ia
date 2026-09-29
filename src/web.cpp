// =============================================================================
// RVIP web page glue, see web.hpp. Everything the page shows as text comes
// from here: the status panel (captured draw_text calls), the live message
// rows (prompt line), the message history, the inventory and what is in view.
// =============================================================================

#include "web.hpp"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "actor.hpp"
#include "actor_see.hpp"
#include "config.hpp"
#include "gfx.hpp"
#include "inventory.hpp"
#include "io.hpp"
#include "io_internal.hpp"
#include "item.hpp"
#include "map.hpp"
#include "msg_log.hpp"
#include "state.hpp"

// clang-format off
EM_JS(void, web_js_send, (int kind, const char* s), {
    if (Module.onGame) Module.onGame(kind, UTF8ToString(s));
});
EM_JS(void, web_js_sync, (), {
    if (Module.onSync) Module.onSync();
});
// clang-format on

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------
namespace
{
enum Kind
{
    k_status = 0,
    k_prompt = 1,
    k_msgs = 2,
    k_inv = 3,
    k_vis = 4,
    k_at_cmd = 5,
    k_tile_names = 6,
    k_tiles_mode = 7,
    k_canvas = 8,
};

// Minimum GUI size: the game's full-screen menus are 78 cells wide.
const int s_min_gui_w = 80;
const int s_min_gui_h = 25;

struct Cell
{
    char ch {' '};
    Color color {};
};

using Grid = std::vector<std::vector<Cell>>;

Grid s_status;
Grid s_log;

// "\x01": nothing sent yet (an empty list must be sent too)
std::string s_sent[9] = {"\x01", "\x01", "\x01", "\x01", "\x01", "\x01", "\x01", "\x01", "\x01"};

bool s_at_cmd = false;
bool s_at_cmd_sent = false;
bool s_tile_names_sent = false;

int s_req_w = 0;
int s_req_h = 0;
bool s_pending_size = false;
int s_pending_scale = 0;
bool s_pending_tiles = false;

Grid* grid_for(const Panel panel)
{
    switch (panel) {
    case Panel::map_gui_stats:
    case Panel::map_gui_stats_border:
        return &s_status;

    case Panel::log:
        return &s_log;

    default:
        return nullptr;
    }
}

void send(const Kind kind, const std::string& s)
{
    if (s_sent[kind] == s) {
        return;
    }

    s_sent[kind] = s;

    web_js_send((int)kind, s.c_str());
}

std::string css(const Color& c)
{
    char buf[8];

    snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r(), c.g(), c.b());

    return buf;
}

void html_escape(std::string& out, const char c)
{
    switch (c) {
    case '<':
        out += "&lt;";
        break;

    case '>':
        out += "&gt;";
        break;

    case '&':
        out += "&amp;";
        break;

    default:
        out += c;
        break;
    }
}

// No tabs/newlines inside a field of a line-based list.
std::string field(std::string s)
{
    std::replace(std::begin(s), std::end(s), '\t', ' ');
    std::replace(std::begin(s), std::end(s), '\n', ' ');

    return s;
}

// A grid as HTML: coloured spans, trailing spaces and empty bottom rows trimmed.
std::string grid_html(const Grid& grid)
{
    std::vector<std::string> rows;

    for (const auto& row : grid) {
        int end = (int)row.size();

        while (end > 0 && row[end - 1].ch == ' ') {
            --end;
        }

        std::string line;
        bool open = false;
        Color cur;

        for (int x = 0; x < end; ++x) {
            const Cell& cell = row[x];

            if (cell.ch != ' ' && (!open || cell.color != cur)) {
                if (open) {
                    line += "</span>";
                }

                line += "<span style=\"color:" + css(cell.color) + "\">";
                open = true;
                cur = cell.color;
            }

            html_escape(line, cell.ch);
        }

        if (open) {
            line += "</span>";
        }

        rows.push_back(line);
    }

    while (!rows.empty() && rows.back().empty()) {
        rows.pop_back();
    }

    std::string out;

    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) {
            out += '\n';
        }

        out += rows[i];
    }

    return out;
}

// The message rows as plain text (prompt line).
std::string grid_text(const Grid& grid)
{
    std::string out;

    for (const auto& row : grid) {
        std::string line;

        for (const Cell& c : row) {
            line += c.ch;
        }

        while (!line.empty() && line.back() == ' ') {
            line.pop_back();
        }

        if (!line.empty()) {
            if (!out.empty()) {
                out += '\n';
            }

            out += line;
        }
    }

    return out;
}

bool is_in_game()
{
    return map::g_player && states::contains_state(StateId::game);
}

std::string item_row(const item::Item& item, const std::string& prefix)
{
    const bool tiles = config::is_tiles_mode();

    std::string name = item.name(ItemNameType::plural, ItemNameInfo::yes, ItemNameAttackInfo::main_attack_mode);

    if (!name.empty()) {
        name[0] = (char)toupper(name[0]);
    }

    return css(item.color()) + "\t" +
        field(std::string(1, item.character())) + "\t" +
        (tiles ? std::to_string((int)item.tile()) : std::string()) + "\t" +
        field(prefix + name);
}

std::string inventory_list()
{
    if (!is_in_game()) {
        return "";
    }

    const auto& inv = map::g_player->m_inv;

    std::string out = "=Equipment\n";

    for (const InvSlot& slot : inv.m_slots) {
        if (slot.item) {
            out += item_row(*slot.item, slot.name + ": ") + "\n";
        }
        else {
            out += css(colors::gray()) + "\t\t\t" + field(slot.name + ": -") + "\n";
        }
    }

    out += "=Backpack\n";

    for (const item::Item* const item : inv.m_backpack) {
        out += item_row(*item, "") + "\n";
    }

    return out;
}

std::string visible_list()
{
    if (!is_in_game()) {
        return "";
    }

    const bool tiles = config::is_tiles_mode();

    std::string out;

    for (const actor::Actor* const mon : actor::seen_actors(*map::g_player)) {
        out += "M" + field(std::string(1, actor::character(*mon))) +
            field(actor::name_a(*mon)) + "\t" +
            css(actor::color(*mon)) +
            (tiles ? "\t" + std::to_string((int)actor::tile(*mon)) : std::string()) + "\n";
    }

    const int w = map::g_items.w();
    const int h = map::g_items.h();

    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            const item::Item* const item = map::g_items.at(x, y);

            if (!item || !map::g_seen.at(x, y)) {
                continue;
            }

            out += "I" + field(std::string(1, item->character())) +
                field(item->name(ItemNameType::a, ItemNameInfo::yes)) + "\t" +
                css(item->color()) +
                (tiles ? "\t" + std::to_string((int)item->tile()) : std::string()) + "\n";
        }
    }

    return out;
}

// History plus the rows still in the log: "css\ttext" per line.
std::string messages()
{
    std::string out;

    for (const Msg& msg : msg_log::history()) {
        out += css(msg.color()) + "\t" + field(msg.text_with_repeats()) + "\n";
    }

    for (const Msg& msg : msg_log::web_current()) {
        out += css(msg.color()) + "\t" + field(msg.text_with_repeats()) + "\n";
    }

    return out;
}

void send_tile_names()
{
    if (s_tile_names_sent) {
        return;
    }

    s_tile_names_sent = true;

    std::string out;

    for (int i = 0; i < (int)gfx::TileId::END; ++i) {
        if (i) {
            out += '\t';
        }

        try {
            out += gfx::tile_id_to_filename((gfx::TileId)i);
        }
        catch (...) {
        }
    }

    web_js_send((int)k_tile_names, out.c_str());
}

P min_window_px()
{
    const int f = config::video_scale_factor();

    return {
        s_min_gui_w * config::gui_cell_px_w() * f,
        s_min_gui_h * config::gui_cell_px_h() * f};
}

}  // namespace

// -----------------------------------------------------------------------------
// Exports for the page
// -----------------------------------------------------------------------------
extern "C" {

// The Map window's body size in CSS px.
EMSCRIPTEN_KEEPALIVE void web_resize(const int w, const int h)
{
    s_req_w = w;
    s_req_h = h;
    s_pending_size = true;
}

EMSCRIPTEN_KEEPALIVE void web_set_scale(const int scale)
{
    s_pending_scale = std::clamp(scale, 1, 4);
}

EMSCRIPTEN_KEEPALIVE void web_toggle_tiles()
{
    s_pending_tiles = true;
}

EMSCRIPTEN_KEEPALIVE int web_scale()
{
    return config::video_scale_factor();
}

}  // extern "C"

// -----------------------------------------------------------------------------
// web
// -----------------------------------------------------------------------------
namespace web
{
bool capture_text(const Panel panel, P pos, const std::string& str, const Color& color)
{
    Grid* const grid = grid_for(panel);

    if (!grid) {
        return false;
    }

    if (pos.y < 0 || pos.y > 200) {
        return true;
    }

    if ((int)grid->size() <= pos.y) {
        grid->resize(pos.y + 1);
    }

    auto& row = (*grid)[pos.y];

    for (const char c : str) {
        if (pos.x >= 0 && pos.x < 300) {
            if ((int)row.size() <= pos.x) {
                row.resize(pos.x + 1);
            }

            row[pos.x] = {c, color};
        }

        ++pos.x;
    }

    return true;
}

bool capture_cover(const Panel panel)
{
    Grid* const grid = grid_for(panel);

    if (!grid) {
        return false;
    }

    if (panel == Panel::map_gui_stats_border || panel == Panel::log) {
        grid->clear();
    }

    return true;
}

void flush()
{
    send_tile_names();

    if (is_in_game()) {
        send(k_status, grid_html(s_status));
        send(k_prompt, grid_text(s_log));
    }
    else {
        send(k_status, "");
        send(k_prompt, "");
    }

    send(k_msgs, messages());
    send(k_inv, inventory_list());
    send(k_vis, visible_list());
    send(k_tiles_mode, config::is_tiles_mode() ? "1" : "0");
}

void set_at_cmd(const bool value)
{
    s_at_cmd = value;
    s_at_cmd_sent = false;
}

bool poll()
{
    if (!s_at_cmd_sent) {
        s_at_cmd_sent = true;

        send(k_at_cmd, s_at_cmd ? "1" : "0");
    }

    bool redraw = false;

    if (s_pending_tiles) {
        s_pending_tiles = false;

        config::web_toggle_tiles();

        s_pending_size = true;
        redraw = true;
    }

    if (s_pending_scale) {
        const int scale = s_pending_scale;

        s_pending_scale = 0;

        if (scale != config::video_scale_factor()) {
            config::web_set_scale(scale);

            s_pending_size = true;
            redraw = true;
        }
    }

    if (s_pending_size && s_req_w > 0 && s_req_h > 0) {
        s_pending_size = false;

        const P min_px = min_window_px();

        const int w = std::max(s_req_w, min_px.x);
        const int h = std::max(s_req_h, min_px.y);

        int cur_w = 0;
        int cur_h = 0;

        SDL_GetWindowSize(io::g_sdl_window, &cur_w, &cur_h);

        if (w != cur_w || h != cur_h) {
            SDL_SetWindowSize(io::g_sdl_window, w, h);

            return true;
        }

        on_window_size();
    }

    if (redraw) {
        on_window_size();

        states::draw();
        io::update_screen();
    }

    return false;
}

void on_window_size()
{
    int w = 0;
    int h = 0;

    SDL_GetWindowSize(io::g_sdl_window, &w, &h);

    send(k_canvas, std::to_string(w) + " " + std::to_string(h) + " " + std::to_string(config::video_scale_factor()));
}

void sync()
{
    web_js_sync();
}

void config_override(int& window_px_w, int& window_px_h, int& scale, bool& fullscreen)
{
    fullscreen = false;

    const char* const w = getenv("IA_W");
    const char* const h = getenv("IA_H");
    const char* const s = getenv("IA_SCALE");

    if (s && atoi(s) >= 1) {
        scale = std::clamp(atoi(s), 1, 4);
    }

    if (w && h && atoi(w) > 0 && atoi(h) > 0) {
        s_req_w = atoi(w);
        s_req_h = atoi(h);
    }

    if (s_req_w > 0) {
        const P min_px = min_window_px();

        window_px_w = std::max(s_req_w, min_px.x);
        window_px_h = std::max(s_req_h, min_px.y);
    }
}

}  // namespace web

#else  // Native: nothing to do

namespace web
{
bool capture_text(Panel, P, const std::string&, const Color&) { return false; }
bool capture_cover(Panel) { return false; }
void flush() {}
void set_at_cmd(bool) {}
bool poll() { return false; }
void on_window_size() {}
void sync() {}
void config_override(int&, int&, int&, bool&) {}
}  // namespace web

#endif  // __EMSCRIPTEN__

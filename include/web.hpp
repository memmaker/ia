// =============================================================================
// RVIP web page glue (Emscripten only; no-ops natively).
//
// The game's status panel and message rows are captured as text instead of
// drawn on the canvas and sent to the page's HTML windows, together with the
// message history, the inventory and what is in view. The page asks for canvas
// size, video scale and tiles/text through web_* exports; they are applied at
// the next key poll (io::read_input).
// =============================================================================

#ifndef WEB_HPP
#define WEB_HPP

#include <string>

#include "colors.hpp"
#include "panel.hpp"
#include "pos.hpp"
#include "rect.hpp"

namespace web
{
// Text drawn to a captured panel (status, message rows): stored, not drawn.
bool capture_text(Panel panel, P pos, const std::string& str, const Color& color);

// cover_panel() on a captured panel: clears it, nothing drawn.
bool capture_cover(Panel panel);


// Pop-up capture: while a text state over the game (menu, pop-up, inventory,
// character sheet...) draws, all its text goes to one screen-cell grid that
// the page shows as an HTML pop-up; boxes, fills and tiles are not drawn.
void popup_clear();
void popup_begin();
void popup_end();
bool popup_capturing();
void popup_cover(Panel panel, const R& area);
void popup_char(Panel panel, P pos, char c, const Color& color);

// After each SDL_RenderPresent: send what changed to the page.
void flush();

// Waiting for a player command (prompt line hides on a key only then).
void set_at_cmd(bool value);

// From the key poll loop: apply a pending canvas size, scale or tiles switch.
// Returns true when the window size changed (caller runs the resize path).
bool poll();

// Called after a window (re)size: tell the page the canvas size.
void on_window_size();

// Write the user dir (IDBFS) back to IndexedDB.
void sync();

// Page-chosen window size and video scale, read over the config file.
void config_override(int& window_px_w, int& window_px_h, int& scale, bool& fullscreen);

// Run report (graveyard beacon): the monster that last hit the player ("" =
// no monster), and one report per ended run (ev = death, win or quit).
void set_killer(const std::string& name_a);
void report_run(const char* ev, const std::string& name, int score, int depth, int turns, int lvl);

}  // namespace web

#endif  // WEB_HPP

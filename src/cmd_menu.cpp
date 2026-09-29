// RVIP: floating menus sized to their content (Enter command menu, item menus).
#include "cmd_menu.hpp"

#include <SDL.h>
#include <algorithm>
#include <memory>

#include "colors.hpp"
#include "draw_box.hpp"
#include "game_commands.hpp"
#include "io.hpp"
#include "panel.hpp"
#include "rect.hpp"
#include "state.hpp"

namespace
{
class FloatMenu : public State
{
public:
    FloatMenu(std::string title, std::vector<cmd_menu::Entry> entries, int* result) :
        m_title(std::move(title)),
        m_entries(std::move(entries)),
        m_result(result)
    {
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].id >= 0) {
                m_sel.push_back((int)i);
            }
        }
    }

    StateId id() const override
    {
        return StateId::popup;
    }

    bool draw_overlayed() const override
    {
        return true;
    }

    void draw() override;
    void update() override;

private:
    void choose(int sel_idx)
    {
        *m_result = m_entries[m_sel[sel_idx]].id;
        states::pop();
    }

    std::string m_title;
    std::vector<cmd_menu::Entry> m_entries;
    std::vector<int> m_sel {};  // Indexes of selectable entries
    int* m_result;
    int m_cur {0};
    int m_top {0};
};

int key_col_w(const std::vector<cmd_menu::Entry>& entries)
{
    int w = 0;

    for (const auto& e : entries) {
        if (e.id >= 0) {
            w = std::max(w, (int)e.key.size());
        }
    }

    return w;
}

void FloatMenu::draw()
{
    const int kw = key_col_w(m_entries);

    int w = (int)m_title.size();

    for (const auto& e : m_entries) {
        const int ew =
            (e.id < 0)
            ? (int)e.label.size()
            : ((kw > 0 ? kw + 1 : 0) + (int)e.label.size());

        w = std::max(w, ew);
    }

    const int scr_w = panels::w(Panel::screen);
    const int scr_h = panels::h(Panel::screen);

    // Border + one space padding on each side
    const int box_w = std::min(w + 4, scr_w);
    const int rows_max = scr_h - 2;
    const int nr_rows = std::min((int)m_entries.size(), rows_max);

    // Keep the cursor visible
    const int cur_row = m_sel.empty() ? 0 : m_sel[m_cur];

    if (cur_row < m_top) {
        m_top = cur_row;
        // Show the group header above the first entry
        if ((m_top > 0) && (m_entries[m_top - 1].id < 0)) {
            --m_top;
        }
    }

    if (cur_row >= m_top + nr_rows) {
        m_top = cur_row - nr_rows + 1;
    }

    const int box_h = nr_rows + 2;
    const int x0 = (scr_w - box_w) / 2;
    const int y0 = (scr_h - box_h) / 2;
    const R box(x0, y0, x0 + box_w - 1, y0 + box_h - 1);

    io::cover_area(Panel::screen, box);
    draw_box(box);

    if (!m_title.empty()) {
        io::draw_text(m_title, Panel::screen, {x0 + (box_w - (int)m_title.size()) / 2, y0}, colors::title());
    }

    for (int r = 0; r < nr_rows; ++r) {
        const int i = m_top + r;
        const auto& e = m_entries[i];
        const P p(x0 + 2, y0 + 1 + r);

        if (e.id < 0) {
            io::draw_text(e.label, Panel::screen, p, colors::title());
            continue;
        }

        const bool marked = (i == cur_row);

        if (kw > 0) {
            io::draw_text(
                e.key,
                Panel::screen,
                p,
                marked ? colors::menu_key_highlight() : colors::menu_key_dark());
        }

        io::draw_text(
            e.label,
            Panel::screen,
            p.with_x_offset(kw > 0 ? kw + 1 : 0),
            marked ? colors::menu_highlight() : colors::menu_dark());
    }
}

void FloatMenu::update()
{
    const io::InputData input = io::read_input();

    if (m_sel.empty()) {
        *m_result = -1;
        states::pop();
        return;
    }

    // Accelerators first (item letters win over cursor keys)
    if (!input.is_ctrl_held) {
        for (size_t s = 0; s < m_sel.size(); ++s) {
            if (m_entries[m_sel[s]].key_code == input.key) {
                choose((int)s);
                return;
            }
        }
    }

    const int n = (int)m_sel.size();

    switch (input.key) {
    case SDLK_UP:
    case SDLK_KP_8:
        m_cur = (m_cur + n - 1) % n;
        break;

    case SDLK_DOWN:
    case SDLK_KP_2:
        m_cur = (m_cur + 1) % n;
        break;

    case SDLK_PAGEUP:
        m_cur = std::max(0, m_cur - 10);
        break;

    case SDLK_PAGEDOWN:
        m_cur = std::min(n - 1, m_cur + 10);
        break;

    case SDLK_RETURN:
    case SDLK_KP_5:
    case SDLK_KP_6:
        choose(m_cur);
        break;

    case SDLK_ESCAPE:
    case SDLK_SPACE:
    case SDLK_KP_0:
    case SDLK_KP_4:
    case '.':
        *m_result = -1;
        states::pop();
        break;

    default:
        break;
    }
}

struct CmdDef
{
    GameCmd cmd;
    const char* label;
};

struct GroupDef
{
    const char* title;
    std::vector<CmdDef> cmds;
};

// Grouped as in the manual ("Gameplay commands" etc.); no movement entries.
const std::vector<GroupDef>& groups()
{
    static const std::vector<GroupDef> g = {
        {"Gameplay commands",
         {
             {GameCmd::apply_item, "Consume or activate item"},
             {GameCmd::close, "Close door / jam with spike"},
             {GameCmd::char_descr, "Character information"},
             {GameCmd::drop_item, "Drop item"},
             {GameCmd::fire, "Aim/fire weapon"},
             {GameCmd::get, "Pick up item"},
             {GameCmd::msg_history, "Message history"},
             {GameCmd::inventory, "Inventory"},
             {GameCmd::kick, "Kick / destroy corpse"},
             {GameCmd::minimap, "Map"},
             {GameCmd::make_noise, "Make noise"},
             {GameCmd::disarm, "Disarm trap"},
             {GameCmd::reload, "Reload"},
             {GameCmd::wait, "Wait one turn"},
             {GameCmd::throw_item, "Throw item"},
             {GameCmd::unload, "Unload firearm / pick up"},
             {GameCmd::look, "Look"},
             {GameCmd::cast_spell, "Cast spell"},
             {GameCmd::swap_weapon, "Swap to readied weapon"},
             {GameCmd::quit, "Quit"},
         }},
        {"Convenience commands",
         {
             {GameCmd::use_medical_bag, "Use medical bag"},
             {GameCmd::toggle_lantern, "Toggle lantern"},
             {GameCmd::wait_long, "Wait five turns"},
             {GameCmd::auto_interact, "Attack adjacent / disarm"},
             {GameCmd::explore, "Explore"},
             {GameCmd::stairs_down, "Descend / walk to stairs"},
             {GameCmd::stairs_up, "Ascend"},
         }},
        {"Menus",
         {
             {GameCmd::game_menu, "Game menu"},
             {GameCmd::manual, "Manual"},
             {GameCmd::options, "Options"},
         }},
    };

    return g;
}

std::string key_name(const int key)
{
    switch (key) {
    case SDLK_TAB:
        return "Tab";
    case SDLK_ESCAPE:
        return "Esc";
    case SDLK_F1:
        return "F1";
    case SDLK_KP_0:
        return "Num0";
    case SDLK_KP_5:
        return "Num5";
    default:
        return std::string(1, (char)key);
    }
}

// Reverse lookup in the current keyset: first key that gives this command.
int find_key(const GameCmd cmd)
{
    std::vector<int> keys;

    for (int c = 33; c < 127; ++c) {
        keys.push_back(c);
    }

    for (int k : {SDLK_TAB, SDLK_ESCAPE, SDLK_F1, SDLK_KP_5, SDLK_KP_0}) {
        keys.push_back(k);
    }

    for (const int k : keys) {
        io::InputData d;
        d.key = k;

        if (game_commands::to_cmd(d) == cmd) {
            return k;
        }
    }

    return -1;
}

}  // namespace

namespace cmd_menu
{
int run(const std::string& title, const std::vector<Entry>& entries)
{
    int result = -1;

    states::run_until_state_done(
        std::make_unique<FloatMenu>(title, entries, &result));

    return result;
}

void run_command_menu()
{
    std::vector<Entry> entries;

    const auto& gs = groups();

    for (const auto& g : gs) {
        entries.push_back({"", g.title, -1, -1});

        for (const auto& c : g.cmds) {
            const int k = find_key(c.cmd);

            if (k < 0) {
                continue;
            }

            // Esc keeps closing the menu
            const int accel = (k == SDLK_ESCAPE) ? -1 : k;

            entries.push_back({key_name(k), c.label, accel, (int)c.cmd});
        }
    }

    const int choice = run("Commands", entries);

    if (choice >= 0) {
        game_commands::handle((GameCmd)choice);
    }
}

}  // namespace cmd_menu

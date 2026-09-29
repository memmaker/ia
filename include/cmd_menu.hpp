// RVIP: floating menus sized to their content (Enter command menu, item menus).
#ifndef CMD_MENU_HPP
#define CMD_MENU_HPP

#include <string>
#include <vector>

namespace cmd_menu
{
struct Entry
{
    std::string key {};  // Shown key text ("" for none)
    std::string label {};
    int key_code {-1};  // Accelerator (-1 = none)
    int id {-1};  // < 0: group header (not selectable)
};

// Shows a floating menu, returns the chosen entry's id, or -1 if cancelled.
int run(const std::string& title, const std::vector<Entry>& entries);

// Enter: menu of every game command (keys of the current keyset).
void run_command_menu();

}  // namespace cmd_menu

#endif  // CMD_MENU_HPP

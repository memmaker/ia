// RVIP: auto-explore (X) and stair walks (>, <).
#ifndef EXPLORE_HPP
#define EXPLORE_HPP

#include "direction.hpp"
#include "pos.hpp"

namespace explore
{
enum class Mode
{
    off,
    explore,
    stairs,
};

// Key commands
void cmd_explore();
void cmd_descend();
void cmd_ascend();

void stop();
bool is_active();

// Called when the player is about to act: returns the direction of the
// next step, or Dir::END (walk over, input is read as usual).
Dir next_dir();

// Called after the step was made.
void after_step(const P& pos_before);

// Called by msg_log::add()
void on_msg();

}  // namespace explore

#endif  // EXPLORE_HPP

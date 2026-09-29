// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#include <algorithm>
#include <iterator>
#include <vector>

#include "catch.hpp"
#include "player_bon.hpp"
#include "test_utils.hpp"

static bool can_be_removed(const TraitId id, const std::vector<TraitId> traits_can_be_removed)
{
    const auto result =
        std::find(
            std::begin(traits_can_be_removed),
            std::end(traits_can_be_removed),
            id);

    return result != std::end(traits_can_be_removed);
}

TEST_CASE("Get traits that can be removed")
{
    test_utils::init_all();

    player_bon::pick_bg(Bg::war_vet);

    player_bon::set_all_traits_to_picked();

    player_bon::remove_trait(TraitId::master_marksman);

    const auto traits_be_removed = player_bon::traits_can_be_removed();

    REQUIRE(!can_be_removed(TraitId::adept_marksman, traits_be_removed));
    REQUIRE(can_be_removed(TraitId::expert_marksman, traits_be_removed));
    REQUIRE(!can_be_removed(TraitId::master_marksman, traits_be_removed));
}

/*  Home Box Navigation
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_PokemonHome_BoxNavigation_H
#define PokemonAutomation_PokemonHome_BoxNavigation_H

#include <array>
#include <cstddef>
#include <optional>
#include <vector>
#include <utility>
#include "Common/Cpp/Time.h"
#include "PokemonHome_BoxNavigationTiming.h"
#include "NintendoSwitch/NintendoSwitch_SingleSwitchProgram.h"
#include "Pokemon/Pokemon_BoxCursor.h"
#include "Pokemon/Pokemon_CollectedPokemonInfo.h"
#include "Pokemon/Options/Pokemon_BoxSortingTable.h"

class QJsonObject;

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonHome{

using namespace std::chrono_literals;

struct HomeBoxVisualEvidence{
    const ImageViewRGB32* first = nullptr;
    const ImageViewRGB32* second = nullptr;
    size_t row = 0, column = 0;
    bool independent_frames = false;
};

inline constexpr size_t MAX_HOME_BOXES = 300;

// Return the unique visible slot cursor, excluding the Box Spaces overview.
std::optional<std::array<size_t, 2>> read_box_slot_cursor_position(const ImageViewRGB32& screen);
// Observe without controller input; require the same slot on three fresh frames.
std::optional<std::array<size_t, 2>> read_home_slot_cursor_stable(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context
);

// Read a stable (current box, total boxes) pair from the ordinary box counter.
std::optional<std::pair<size_t, size_t>> read_home_box_identity(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context, bool allow_held = false
);
// Unheld navigation only. Verify the actual destination and recover once from
// a readable missed endpoint; leave the cursor at its first slot.
void move_home_box_verified(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    size_t target_box, BoxNavigationTiming& timing, bool via_overview
);

// Enter a verified destination through Box Spaces without placing the held copy.
// Returns with the held cursor at slot 0; any uncertain state stops held.
void move_home_box_held_overview_verified(SingleSwitchProgramEnvironment& env,
    ProControllerContext& context, size_t target_box, BoxNavigationTiming& timing,
    size_t current_row, size_t current_column);

// Bounded restoring diagnostic using the same per-slot identity reader.
void compare_home_held_routes(SingleSwitchProgramEnvironment& env,
    ProControllerContext& context, Milliseconds game_delay);

// Confirm held mode, physical box number, and the held hand's destination slot.
bool confirm_home_held_cursor(SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    size_t physical_box, size_t row, size_t column);

// Move the red cursor to the first slot of the box
bool go_to_first_slot(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    Milliseconds VIDEO_DELAY
);

// Move the cursor to the given coordinates, knowing current pos via the cursor struct
[[nodiscard]] Pokemon::BoxCursor move_cursor_to(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    const Pokemon::BoxCursor& cur_cursor,
    const Pokemon::BoxCursor& dest_cursor,
    Milliseconds GAME_DELAY,
    BoxNavigationTiming* adaptive_timing = nullptr,
    bool pokemon_held = false
);

// Read current screen to find occupied and empty slots in the box.
// Returns the (row, col) index of the first pokemon (aka non-empty) slot in the box.
std::array<size_t, 2> find_occupied_slots_in_box(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    std::vector<std::optional<Pokemon::CollectedPokemonInfo>>& boxes_data,
    const std::vector<Pokemon::SortingRule>& sort_preferences
);

// Read the current summary screen and assign various pokemon info into cur_pokemon_info
void read_summary_screen(
    SingleSwitchProgramEnvironment& env,
    Pokemon::CollectedPokemonInfo& cur_pokemon_info,
    Language ot_name_language = Language::None,
    Language home_language = Language::None
);

void read_summary_screen(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    Pokemon::CollectedPokemonInfo& cur_pokemon_info,
    Language ot_name_language = Language::None,
    Language home_language = Language::None,
    QJsonObject* collection_observation = nullptr,
    const HomeBoxVisualEvidence* visual_evidence = nullptr,
    bool collection_identity_only = false
);

void print_boxes_data(
    const std::vector<std::optional<Pokemon::CollectedPokemonInfo>>& boxes_data,
    SingleSwitchProgramEnvironment& env
);

void exit_menus(SingleSwitchProgramEnvironment& env, ProControllerContext& context, std::chrono::milliseconds video_delay);

}
}
}
#endif

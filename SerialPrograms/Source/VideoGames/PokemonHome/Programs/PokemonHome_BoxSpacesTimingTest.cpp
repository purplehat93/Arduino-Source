/*  Pokemon HOME Box Spaces timing test
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <vector>
#include "Common/Cpp/Exceptions.h"
#include "CommonFramework/ImageTools/ImageBoxes.h"
#include "CommonFramework/ImageTools/ImageStats.h"
#include "CommonFramework/ImageTypes/ImageRGB32.h"
#include "CommonFramework/VideoPipeline/VideoFeed.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "CommonTools/Async/InferenceRoutines.h"
#include "CommonTools/InferenceCallbacks/VisualInferenceCallback.h"
#include "CommonTools/Images/ImageFilter.h"
#include "CommonTools/OCR/OCR_Routines.h"
#include "CommonTools/StartupChecks/StartProgramChecks.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "Pokemon/Pokemon_Strings.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_BoxSpacesDetector.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_BoxViewDetector.h"
#include "PokemonHome_BoxSpacesTimingTest.h"
#include "PokemonHome_BoxNavigation.h"
#include "PokemonHome_NavigationTimingRoutes.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonHome{

namespace{

#include "PokemonHome_OverviewNavigation.inc"

void log_latency_summary(
    SingleSwitchProgramEnvironment& env,
    const std::vector<int64_t>& values,
    const char* direction,
    int page_before,
    int page_after
){
    if (values.empty()){
        env.log(std::string("HOME_TIMING summary phase=latency direction=") + direction + " samples=0 result=no_samples");
        return;
    }
    std::vector<int64_t> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const size_t p50_index = (sorted.size() + 1) / 2 - 1;
    const size_t p90_index = (sorted.size() * 90 + 99) / 100 - 1;
    std::ostringstream line;
    line << "HOME_TIMING summary phase=latency direction=" << direction;
    if (page_before != 0){
        line << " page_before=" << page_before << " page_after=" << page_after;
    }
    line << " samples=" << sorted.size()
         << " min_ms=" << sorted.front()
         << " p50_ms=" << sorted[p50_index]
         << " p90_ms=" << sorted[p90_index]
         << " max_ms=" << sorted.back();
    env.console.log(line.str());
    env.log_to_ui(line.str());
}

using namespace NavigationTiming;

void log_test_event(SingleSwitchProgramEnvironment& env, const std::string& text){
    env.console.log(text);
    env.log_to_ui(text);
}

struct TimingProfile{
    const char* name;
    int conservative;
    int gap;
    int best = -1;
    int failed_gap = -1;
    size_t successes = 0;
    size_t failures = 0;
    std::vector<int64_t> queue_samples;
    std::vector<int64_t> total_samples;
};

struct TrialResult{ bool confirmed; bool recovered; };

TrialResult record_trial(
    SingleSwitchProgramEnvironment& env, TimingProfile& profile,
    const char* phase, size_t trial, int from, int target, int actual,
    size_t presses, int gap, std::chrono::steady_clock::time_point started,
    std::chrono::steady_clock::time_point queue_finished, bool recovered, int box_context = 0
){
    const auto finished = std::chrono::steady_clock::now();
    const auto queue_ms = std::chrono::duration_cast<Milliseconds>(queue_finished - started).count();
    const auto total_ms = std::chrono::duration_cast<Milliseconds>(finished - started).count();
    const bool confirmed = recovered && actual == target;
    if (confirmed){
        profile.successes++;
        // Candidate timings have different gaps. Only validation samples are
        // comparable for the recommended profile; failed routes stay in logs.
        if (std::string(phase).find("validation") == 0){
            profile.queue_samples.push_back(queue_ms);
            profile.total_samples.push_back(total_ms);
        }
    }else{
        profile.failures++;
    }
    std::ostringstream line;
    line << "HOME_TIMING action=" << profile.name << " phase=" << phase
         << " trial=" << trial << " from=" << from << " expected=" << target
         << " actual=" << actual << " box_context=" << box_context << " presses=" << presses
         << " input_gap_ms=" << gap << " queue_ms=" << queue_ms
         << " confirmation_overhead_ms=" << std::max<int64_t>(0, total_ms - queue_ms)
         << " total_ms=" << total_ms
         << " result=" << (confirmed ? "confirmed" : actual < 0 ? "unreadable" : "wrong_destination")
         << " recovered=" << (recovered ? "yes" : "no");
    log_test_event(env, line.str());
    return {confirmed, recovered};
}

// Setup uses separate conservative gaps and individually confirmed actions.
// Candidate/validation routes queue their entire input sequence before checking
// the endpoint, so feedback cannot hide an input gap that is too short.
bool prepare_box(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    BoxSpacesDetector& detector, int box
){
    BoxSpacesWatcher overview(&env.console.overlay());
    if (wait_until(env.console, context, Seconds(2), {overview}) != 0){
        if (!wait_for_box_view(env, context)
            || !ensure_box_cursor_at_first_slot(env, context, 480ms)
            || !return_to_overview(env, context, 960ms)) return false;
    }
    return navigate_overview_to_box(env, context, detector, box, 1800ms, 480ms, "setup", 0);
}

TrialResult page_trial(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    BoxSpacesDetector& detector, TimingProfile& profile,
    int from, int target, int gap, const char* phase, size_t trial
){
    if (!prepare_box(env, context, detector, (from - 1) * 30 + 1)) return {false, false};
    const bool right = target > from;
    const size_t presses = (size_t)std::abs(target - from);
    const auto started = std::chrono::steady_clock::now();
    for (size_t i = 0; i < presses; i++){
        pbf_press_button(context, right ? BUTTON_R : BUTTON_L, 80ms, Milliseconds(gap));
    }
    context.wait_for_all_requests();
    const auto queue_finished = std::chrono::steady_clock::now();
    BoxSpacesPageNumberWatcher expected(target);
    const bool matched = wait_until(env.console, context, Seconds(8), {expected}) == 0;
    // On failure read the actual stable state, rather than trusting the plan.
    OverviewSelection actual = read_overview_selection(env, context);
    const bool recovered = actual.valid() && actual.box_number <= 200 && detector.detect(env.console.video().snapshot());
    return record_trial(env, profile, phase, trial, from, target,
        matched && recovered ? actual.page : recovered ? actual.page : -1,
        presses, gap, started, queue_finished, recovered);
}

TrialResult grid_trial(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    BoxSpacesDetector& detector, TimingProfile& profile, int box,
    GridRoute route, bool slots, int gap, const char* phase, size_t trial,
    bool horizontal_wrap = false, bool reuse_box_view = false
){
    if (!reuse_box_view && !prepare_box(env, context, detector, box)) return {false, false};
    const int page = (box - 1) / 30 + 1;
    GridPosition cursor;
    const GridPosition from{route.from / 6, route.from % 6};
    const GridPosition target{route.to / 6, route.to % 6};
    if (slots){
        if (reuse_box_view){
            if (!wait_for_box_view(env, context)) return {false, false};
        }else if (!enter_selected_box(env, context)) return {false, false};
        BoxSlotCursorWatcher watcher;
        if (wait_until(env.console, context, Seconds(5), {watcher}) != 0) return {false, false};
        cursor = watcher.position();
        if (!move_box_slot_to(env, context, cursor, from, 480ms, 0)) return {false, false};
    }else{
        OverviewSelection actual = read_overview_selection(env, context);
        if (!actual.valid()) return {false, false};
        cursor = actual.position;
        if (!move_grid_to(env, context, cursor, from, 480ms, "setup", page == 7 ? 20 : 30)) return {false, false};
    }
    size_t presses = 0;
    const auto started = std::chrono::steady_clock::now();
    if (horizontal_wrap){
        pbf_press_dpad(context, route.from % 6 == 5 ? DPAD_RIGHT : DPAD_LEFT, 80ms, Milliseconds(gap));
        presses = 1;
    }else{
        GridPosition previous = from;
        for (int cell : grid_path(route.from, route.to, !slots && page == 7 ? 20 : 30)){
            GridPosition next{cell / 6, cell % 6};
            const DpadPosition direction = next.row > previous.row ? DPAD_DOWN
                : next.row < previous.row ? DPAD_UP
                : next.column > previous.column ? DPAD_RIGHT : DPAD_LEFT;
            pbf_press_dpad(context, direction, 80ms, Milliseconds(gap));
            presses++;
            previous = next;
        }
    }
    context.wait_for_all_requests();
    const auto queue_finished = std::chrono::steady_clock::now();
    int actual_cell = -1;
    bool recovered = false;
    if (slots){
        BoxSlotCursorWatcher expected(target);
        const bool matched = wait_until(env.console, context, Seconds(5), {expected}) == 0;
        BoxSlotCursorWatcher observed;
        if (matched || wait_until(env.console, context, Seconds(3), {observed}) == 0){
            const GridPosition actual = matched ? expected.position() : observed.position();
            recovered = actual.valid() && wait_for_box_view(env, context);
            if (recovered) actual_cell = actual.row * 6 + actual.column;
        }
    }else{
        BoxSpacesGridPositionWatcher expected(target);
        wait_until(env.console, context, Seconds(5), {expected});
        const OverviewSelection actual = read_overview_selection(env, context);
        recovered = actual.valid() && actual.page == page && actual.box_number <= 200
            && detector.detect(env.console.video().snapshot());
        if (recovered) actual_cell = actual.position.row * 6 + actual.position.column;
    }
    return record_trial(env, profile, phase, trial, route.from + 1, route.to + 1,
        actual_cell < 0 ? -1 : actual_cell + 1, presses, gap,
        started, queue_finished, recovered, box);
}

TrialResult box_trial(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    BoxSpacesDetector& detector, TimingProfile& profile, BoxRoute route,
    int gap, const char* phase, size_t trial
){
    if (!prepare_box(env, context, detector, route.from)
        || !enter_selected_box(env, context)
        || !ensure_box_cursor_at_first_slot(env, context, 480ms)) return {false, false};
    const size_t presses = (size_t)std::abs(route.to - route.from);
    const auto started = std::chrono::steady_clock::now();
    for (size_t i = 0; i < presses; i++){
        pbf_press_button(context, route.to > route.from ? BUTTON_R : BUTTON_L, 80ms, Milliseconds(gap));
    }
    context.wait_for_all_requests();
    const auto queue_finished = std::chrono::steady_clock::now();
    // Capture box identity through the overview, not a box name or Pokemon.
    bool recovered = wait_for_box_view(env, context)
        && ensure_box_cursor_at_first_slot(env, context, 480ms)
        && return_to_overview(env, context, 960ms);
    const OverviewSelection actual = recovered ? read_overview_selection(env, context) : OverviewSelection{};
    recovered = recovered && actual.valid() && actual.box_number <= 200;
    return record_trial(env, profile, phase, trial, route.from, route.to,
        recovered ? actual.box_number : -1, presses, gap, started, queue_finished, recovered);
}

template <class Suite>
bool calibrate(SingleSwitchProgramEnvironment& env, TimingProfile& profile, bool pages, bool extended, Suite suite){
    size_t candidate_index = 0;
    const std::vector<int> gaps = extended ? candidates(profile.conservative, pages)
        : pages ? std::vector<int>{profile.conservative, 1100, 900}
        : std::vector<int>{profile.conservative, profile.conservative >= 960 ? 480 : 240, 120};
    for (int gap : gaps){
        log_test_event(env, std::string("Calibrating ") + profile.name + ": input gap " + std::to_string(gap) + " ms; varied routes, both directions.");
        const TrialResult result = suite(gap, candidate_index++);
        log_test_event(env, std::string("HOME_TIMING candidate action=") + profile.name
            + " input_gap_ms=" + std::to_string(gap)
            + " result=" + (result.confirmed ? "passed" : "failed"));
        if (!result.confirmed){
            profile.failed_gap = gap;
            if (!result.recovered || profile.best < 0) return false;
            break;
        }
        profile.best = gap;
    }
    // A passing sample is not a guarantee. Validate a slightly larger gap on
    // different starts, routes and occupied/empty cells in the same real setup.
    profile.gap = std::min(profile.conservative, profile.best + std::max(40, profile.best / 5));
    log_test_event(env, std::string("HOME_TIMING learned action=") + profile.name
        + " lowest_tested_passing_gap_ms=" + std::to_string(profile.best)
        + " first_failed_gap_ms=" + std::to_string(profile.failed_gap)
        + " validation_gap_ms=" + std::to_string(profile.gap));
    return true;
}

template <class Trial>
bool validate(SingleSwitchProgramEnvironment& env, TimingProfile& profile, Trial trial){
    TrialResult result = trial(profile.gap, "validation");
    if (result.confirmed) return true;
    if (!result.recovered) return false;
    // Back off and reinitialize from observed state before retrying once.
    profile.gap = std::max(profile.conservative, profile.gap + profile.gap / 2);
    profile.queue_samples.clear();
    profile.total_samples.clear();
    log_test_event(env, std::string("HOME_TIMING backoff action=") + profile.name
        + " new_gap_ms=" + std::to_string(profile.gap));
    return trial(profile.gap, "validation_retry").confirmed;
}

}

BoxSpacesTimingTest_Descriptor::BoxSpacesTimingTest_Descriptor()
    : SingleSwitchProgramDescriptor(
        "PokemonHome:BoxSpacesTimingTest",
        "Pokemon Home", "Navigation Timing Test",
        "",
        "Calibrate representative box, page and cursor routes. Extended validation is optional. Start in Box Spaces with no Pokemon held.",
        ProgramControllerClass::StandardController_NoRestrictions,
        FeedbackType::REQUIRED,
        AllowCommandsWhenRunning::DISABLE_COMMANDS
    )
{}

BoxSpacesTimingTest::BoxSpacesTimingTest()
    : GAME_DELAY(
        "<b>Initial Game Delay:</b><br>Starting fallback; the test learns separate gaps for box changes, overview movement and slot movement.",
        LockMode::LOCK_WHILE_RUNNING,
        "240 ms"
    )
    , PAGE_DELAY(
        "<b>Initial Page Delay:</b><br>Starting fallback; page cadence is calibrated automatically.",
        LockMode::LOCK_WHILE_RUNNING,
        "240 ms"
    )
    , EXTENDED("<b>Extended validation:</b><br>Run broader coverage and repeated stress trials. Leave off for the short calibration.", LockMode::LOCK_WHILE_RUNNING, false)
    , SORTER_CHECK_ONLY("<b>Sorter navigation check only:</b><br>Check shared sorter slots and verified cross-box routes without picking up Pokemon, then restore the starting box. Skip calibration.", LockMode::LOCK_WHILE_RUNNING, false)
    , ROUTING_COMPARE_ONLY("<b>Routing speed comparison only:</b><br>Compare verified L/R and Box Spaces travel over 4 and 70 boxes, returning after every trip. Nothing is picked up. Start in ordinary box view or Box Spaces.", LockMode::LOCK_WHILE_RUNNING, false)
    , HELD_ROUTING_COMPARE_ONLY("<b>Held routing speed comparison only:</b><br>Carry the first-slot Pokemon 70 boxes away and back using L/R and automatic routing. Place it only back in its source slot and verify its identity. Start in ordinary box view; keep HOME in English.", LockMode::LOCK_WHILE_RUNNING, false)
{
    PA_ADD_OPTION(HELD_ROUTING_COMPARE_ONLY);
    PA_ADD_OPTION(ROUTING_COMPARE_ONLY);
    PA_ADD_OPTION(SORTER_CHECK_ONLY);
    PA_ADD_OPTION(EXTENDED);
    PA_ADD_OPTION(GAME_DELAY);
    PA_ADD_OPTION(PAGE_DELAY);
}

void BoxSpacesTimingTest::program(SingleSwitchProgramEnvironment& env, ProControllerContext& context){
    StartProgramChecks::check_performance_class_wired_or_wireless(context);
    if (HELD_ROUTING_COMPARE_ONLY){
        compare_home_held_routes(env, context, GAME_DELAY.get());
        return;
    }
    BoxSpacesDetector detector(&env.console.overlay());
    BoxSpacesWatcher overview(&env.console.overlay());
    if (wait_until(env.console, context, Seconds(5), {overview}) != 0){
        if ((!SORTER_CHECK_ONLY && !ROUTING_COMPARE_ONLY) || !wait_for_box_view(env, context)
            || !ensure_box_cursor_at_first_slot(env, context, 480ms)
            || !return_to_overview(env, context, 960ms)){
            throw UserSetupError(env.console, "Open Pokemon HOME Box Spaces first, with no Pokemon held.");
        }
    }
    const OverviewSelection starting = read_overview_selection(env, context);
    if (!starting.valid() || starting.box_number > 200){
        throw UserSetupError(env.console, "Could not read the selected box. No navigation inputs were sent.");
    }
    if (ROUTING_COMPARE_ONLY){
        if (!enter_selected_box(env, context)){
            throw UserSetupError(env.console, "Cannot enter the starting box for routing comparison.");
        }
        BoxNavigationTiming timing(GAME_DELAY.get());
        log_test_event(env, "HOME_SPEED diagnostic=routing_comparison result=started held_mode=none");
        for (size_t distance : {size_t(4), size_t(70)}){
            const size_t origin = (size_t)starting.box_number;
            const size_t target = origin + distance <= 200 ? origin + distance : origin - distance;
            for (bool via_overview : {false, true}){
                const auto begin = std::chrono::steady_clock::now();
                move_home_box_verified(env, context, target, timing, via_overview);
                const auto arrived = std::chrono::steady_clock::now();
                move_home_box_verified(env, context, origin, timing, via_overview);
                const auto restored = std::chrono::steady_clock::now();
                log_test_event(env, "HOME_SPEED route=" + std::string(via_overview ? "overview" : "linear")
                    + " held_mode=none distance=" + std::to_string(distance)
                    + " start_box=" + std::to_string(origin) + " target_box=" + std::to_string(target)
                    + " outbound_ms=" + std::to_string(std::chrono::duration_cast<Milliseconds>(arrived - begin).count())
                    + " return_ms=" + std::to_string(std::chrono::duration_cast<Milliseconds>(restored - arrived).count())
                    + " total_ms=" + std::to_string(std::chrono::duration_cast<Milliseconds>(restored - begin).count())
                    + " restored=yes");
            }
        }
        log_test_event(env, "HOME_SPEED diagnostic=routing_comparison result=completed moves=0 releases=0 restored=yes");
        return;
    }
    if (SORTER_CHECK_ONLY){
        log_test_event(env, "HOME_TIMING diagnostic=sorter_navigation result=started held_mode=none start_box=" + std::to_string(starting.box_number));
        if (!enter_selected_box(env, context) || !ensure_box_cursor_at_first_slot(env, context, 480ms)){
            throw UserSetupError(env.console, "Could not prepare the ordinary box cursor. No Pokemon were picked up.");
        }
        Pokemon::BoxCursor cursor{(size_t)starting.box_number - 1, 0, 0};
        BoxNavigationTiming timing(GAME_DELAY.get());
        // Same-slot check, horizontal wraps, both vertical extremes and varied
        // interior routes. Use production navigation and its destination guard.
        for (int slot : {0, 5, 0, 9, 16, 29, 24, 0, 12, 17, 2, 27, 5, 0, 9, 16, 29, 24, 0}){
            const Pokemon::BoxCursor destination{cursor.box, (size_t)slot / 6, (size_t)slot % 6};
            cursor = move_cursor_to(env, context, cursor, destination, GAME_DELAY.get(), &timing);
        }
        const auto initial_box = read_home_box_identity(env, context);
        if (!initial_box || initial_box->first != (size_t)starting.box_number){
            throw UserSetupError(env.console, "Could not confirm ordinary box identity before cross-box testing.");
        }
        const size_t recovery_target = initial_box->first + 10 <= initial_box->second
            ? initial_box->first + 10 : initial_box->first - 10;
        timing.box_delay = 120ms;
        log_test_event(env, "HOME_TIMING diagnostic=sorter_navigation phase=miss_recovery_probe box_gap_ms=120");
        move_home_box_verified(env, context, recovery_target, timing, false);
        timing.box_delay = std::max(timing.box_delay, 576ms);
        move_home_box_verified(env, context, initial_box->first, timing, false);
        for (bool via_overview : {false, true}){
            for (int distance : {1, 10, 20}){
                const size_t target = initial_box->first + distance <= initial_box->second
                    ? initial_box->first + distance : initial_box->first - distance;
                move_home_box_verified(env, context, target, timing, via_overview);
                move_home_box_verified(env, context, initial_box->first, timing, via_overview);
            }
        }
        // Three-digit ordinary counter and partial final overview page.
        if (initial_box->second == 200){
            move_home_box_verified(env, context, 200, timing, true);
            move_home_box_verified(env, context, initial_box->first, timing, true);
        }
        const size_t long_target = initial_box->first + 20 <= initial_box->second
            ? initial_box->first + 20 : initial_box->first - 20;
        cursor = move_cursor_to(env, context, cursor, Pokemon::BoxCursor{long_target - 1, 1, 3}, GAME_DELAY.get(), &timing);
        cursor = move_cursor_to(env, context, cursor, Pokemon::BoxCursor{initial_box->first - 1, 0, 0}, GAME_DELAY.get(), &timing);
        const bool overview_restored = return_to_overview(env, context, 960ms);
        const OverviewSelection restored = overview_restored ? read_overview_selection(env, context) : OverviewSelection{};
        if (!restored.valid() || restored.box_number != starting.box_number){
            throw UserSetupError(env.console, "Sorter navigation check finished but could not confirm restoration. No Pokemon were picked up.");
        }
        log_test_event(env, "HOME_TIMING diagnostic=sorter_navigation result=completed held_mode=none restored=yes start_box="
            + std::to_string(starting.box_number) + " confirmed_moves=" + std::to_string(timing.confirmed_local_moves)
            + " final_slot_gap_ms=" + std::to_string(timing.local_cursor_delay.count()));
        return;
    }
    const bool extended = EXTENDED;
    const unsigned rounds = extended ? 3 : 1;
    const int anchor_page = std::min(starting.page, 5);
    const int anchor_box = (anchor_page - 1) * 30 + 1;
    const int game_fallback = (int)GAME_DELAY.get().count();
    const int page_fallback = (int)PAGE_DELAY.get().count();
    TimingProfile pages{"overview_pages", std::max(1800, page_fallback), std::max(1800, page_fallback)};
    TimingProfile grid{"overview_grid", std::max(480, game_fallback), std::max(480, game_fallback)};
    TimingProfile slots{"box_slots", std::max(480, game_fallback), std::max(480, game_fallback)};
    TimingProfile boxes{"linear_boxes", std::max(960, game_fallback + 240), std::max(960, game_fallback + 240)};
    log_test_event(env, "HOME_TIMING diagnostic=general_navigation start_box=" + std::to_string(starting.box_number)
        + " mode=" + (extended ? "extended" : "short") + " validation_rounds=" + std::to_string(rounds) + " held_mode=none");
    log_test_event(env, extended ? "Extended navigation calibration: four input gaps and three varied validation rounds." : "Short navigation calibration: up to three gaps per action and one representative validation pass.");
    bool complete = calibrate(env, pages, true, extended, [&](int gap, size_t candidate){
        const std::array<BoxRoute, 4> routes = candidate % 2 == 0
            ? std::array<BoxRoute, 4>{{{1, 3}, {7, 3}, {5, 4}, {2, 6}}}
            : std::array<BoxRoute, 4>{{{6, 2}, {1, 5}, {3, 2}, {4, 6}}};
        const auto selected_routes = extended ? std::vector<BoxRoute>(routes.begin(), routes.end())
            : std::vector<BoxRoute>{{anchor_page, anchor_page + 2}, {anchor_page + 2, anchor_page}};
        for (size_t i = 0; i < selected_routes.size(); i++){
            const TrialResult result = page_trial(env, context, detector, pages,
                selected_routes[i].from, selected_routes[i].to, gap, "calibration", i + 1);
            if (!result.confirmed) return result;
        }
        return TrialResult{true, true};
    });
    if (complete){
        complete = calibrate(env, grid, false, extended, [&](int gap, size_t candidate){
            const int box = extended ? (int)(candidate % 6) * 30 + 1 : anchor_box;
            size_t trial = 0;
            for (GridRoute route : grid_routes(extended ? (unsigned)candidate : 2)){
                const TrialResult result = grid_trial(env, context, detector, grid, box,
                    route, false, gap, "calibration", ++trial);
                if (!result.confirmed) return result;
            }
            return TrialResult{true, true};
        });
    }
    if (complete){
        complete = calibrate(env, slots, false, extended, [&](int gap, size_t candidate){
            const int box = extended ? 1 + (int)(candidate * 47 % 200) : anchor_box + 12;
            size_t trial = 0;
            for (GridRoute route : grid_routes(extended ? (unsigned)candidate + 1 : 2)){
                trial++;
                const TrialResult result = grid_trial(env, context, detector, slots, box,
                    route, true, gap, "calibration", trial, false, trial > 1);
                if (!result.confirmed) return result;
            }
            return TrialResult{true, true};
        });
    }
    if (complete){
        complete = calibrate(env, boxes, false, extended, [&](int gap, size_t candidate){
            size_t trial = 0;
            for (BoxRoute route : extended ? box_routes((unsigned)candidate, true)
                : std::vector<BoxRoute>{{anchor_box, anchor_box + 5}, {anchor_box + 5, anchor_box}}){
                const TrialResult result = box_trial(env, context, detector, boxes,
                    route, gap, "calibration", ++trial);
                if (!result.confirmed) return result;
            }
            return TrialResult{true, true};
        });
    }
    size_t finished_rounds = 0;
    for (unsigned round = 0; round < rounds && complete; round++){
        log_test_event(env, "Validation round " + std::to_string(round + 1) + "/" + std::to_string(rounds) + ": representative page, grid, slot and box routes.");
        size_t trial = 0;
        for (int target : extended ? overview_targets(round)
            : std::vector<int>{anchor_box + 14, anchor_box + 29, anchor_box + 30, 200}){
            OverviewSelection current = read_overview_selection(env, context);
            const int target_page = (target - 1) / 30 + 1;
            if (!current.valid()){ complete = false; break; }
            if (current.page != target_page){
                const int from_page = current.page;
                complete = validate(env, pages, [&](int gap, const char* phase){
                    return page_trial(env, context, detector, pages, from_page, target_page, gap, phase, ++trial);
                });
                if (!complete) break;
            }
            current = read_overview_selection(env, context);
            if (!current.valid()){ complete = false; break; }
            const int from_box = current.box_number;
            const GridRoute route{current.position.row * 6 + current.position.column, (target - 1) % 30};
            if (route.from != route.to){
                complete = validate(env, grid, [&](int gap, const char* phase){
                    return grid_trial(env, context, detector, grid, from_box, route, false, gap, phase, ++trial);
                });
                if (!complete) break;
            }
        }
        // Slot sweeps use different boxes on each pass, including box 200.
        const int slot_box = extended ? (round == 2 ? 200 : 1 + (int)(round * 83 + 28)) : 200;
        int cursor = 0;
        bool slot_view_ready = false;
        for (int target : extended ? slot_targets(round) : std::vector<int>{5, 29, 24, 0, 9, 16, 0}){
            if (cursor == target) continue;
            const GridRoute route{cursor, target};
            complete = complete && validate(env, slots, [&](int gap, const char* phase){
                return grid_trial(env, context, detector, slots, slot_box, route, true, gap, phase, ++trial, false, slot_view_ready);
            });
            if (!complete) break;
            cursor = target;
            slot_view_ready = true;
        }
        // Horizontal wrap is part of the sorter's route logic; check it in
        // both directions on a different row each pass. Vertical wrap passes
        // through menu controls and is deliberately not inferred from a grid.
        for (GridRoute route : std::array<GridRoute, 2>{{{(int)round * 6 + 5, (int)round * 6}, {(int)round * 6, (int)round * 6 + 5}}}){
            complete = complete && validate(env, slots, [&](int gap, const char* phase){
                return grid_trial(env, context, detector, slots, slot_box, route, true, gap, phase, ++trial, true, slot_view_ready);
            });
            if (!complete) break;
        }
        for (BoxRoute route : extended ? box_routes(round)
            : std::vector<BoxRoute>{{anchor_box, anchor_box + 1}, {anchor_box + 1, anchor_box}, {anchor_box, anchor_box + 10}, {anchor_box + 10, anchor_box}}){
            complete = complete && validate(env, boxes, [&](int gap, const char* phase){
                return box_trial(env, context, detector, boxes, route, gap, phase, ++trial);
            });
            if (!complete) break;
        }
        if (complete) finished_rounds++;
    }
    const bool restored = prepare_box(env, context, detector, starting.box_number);
    const OverviewSelection final_selection = restored ? read_overview_selection(env, context) : OverviewSelection{};
    const bool restore_confirmed = restored && final_selection.valid() && final_selection.box_number == starting.box_number;
    for (TimingProfile* profile : std::array<TimingProfile*, 4>{&pages, &grid, &slots, &boxes}){
        log_test_event(env, std::string("HOME_TIMING profile action=") + profile->name
            + " lowest_tested_passing_gap_ms=" + std::to_string(profile->best)
            + " first_failed_gap_ms=" + std::to_string(profile->failed_gap)
            + " recommended_input_gap_ms=" + std::to_string(profile->gap)
            + " successes=" + std::to_string(profile->successes)
            + " failures=" + std::to_string(profile->failures)
            + " validated=" + (complete && restore_confirmed ? "yes" : "no")
            + " persisted_to_sorter=no");
        log_latency_summary(env, profile->queue_samples, (std::string(profile->name) + "_queue").c_str(), 0, 0);
        log_latency_summary(env, profile->total_samples, (std::string(profile->name) + "_including_confirmation").c_str(), 0, 0);
    }
    log_test_event(env, std::string("HOME_TIMING diagnostic=general_navigation result=")
        + (complete && restore_confirmed ? "completed" : "incomplete")
        + " validation_rounds=" + std::to_string(finished_rounds)
        + " starting_box=" + std::to_string(starting.box_number)
        + " restored=" + (restore_confirmed ? "yes" : "no")
        + " held_mode=none");
    if (!restore_confirmed){
        env.log_to_ui("The starting box could not be confirmed. Inspect Box Spaces before continuing.");
    }
}

}
}
}

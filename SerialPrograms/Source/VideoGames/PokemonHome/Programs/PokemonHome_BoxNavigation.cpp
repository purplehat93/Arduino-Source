/*  Home Box Navigation
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <array>
#include <chrono>
#include <optional>
#include <sstream>
#include <vector>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "CommonFramework/GlobalAutoPaths.h"
#include "CommonFramework/ImageTypes/ImageRGB32.h"
#include "CommonTools/OCR/OCR_Routines.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_SummaryScreenDetector.h"
#include "PokemonHome_CollectionObservationPolicy.h"
#include "PokemonHome_CollectionFormPolicy.h"
#include "PokemonHome_CollectionExecutionPolicy.h"
#include "PokemonHome_CollectionVisualPolicy.h"
#include "Pokemon/Resources/Pokemon_PokemonHomeSprites.h"
#include "CommonFramework/Exceptions/OperationFailedExceptionWithScreenshot.h"
#include "CommonFramework/ImageTools/ImageBoxes.h"
#include "CommonFramework/ImageTools/ImageStats.h"
#include "CommonFramework/Notifications/ProgramInfo.h"
#include "CommonFramework/Tools/ErrorDumper.h"
#include "CommonFramework/VideoPipeline/VideoFeed.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "CommonTools/Images/ImageFilter.h"
#include "CommonTools/Async/InferenceRoutines.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "Pokemon/Resources/Pokemon_PokemonSlugs.h"
#include "Pokemon/Inference/Pokemon_TypeReader.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_BallReader.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_AlphaDetector.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_BoxGenderDetector.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_GigantamaxDetector.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_OriginMarkReader.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_ShinyDetector.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_SummaryReader.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_TeraTypeReader.h"
#include "PokemonHome_BoxNavigation.h"
#include "PokemonHome_SlotPointerPolicy.h"
#include "CommonTools/InferenceCallbacks/VisualInferenceCallback.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_BoxViewDetector.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_BoxSpacesDetector.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonHome{
using namespace Pokemon;

std::optional<std::array<size_t, 2>> read_box_slot_cursor_position(const ImageViewRGB32& screen){
    if (!screen || screen.width() < 640 || screen.height() < 360) return std::nullopt;
    BoxViewDetector box;
    if (!box.detect(screen)) return std::nullopt;
    // HOME shows the selected slot with a blue or orange downward pointer above
    // its row. Detect its triangular cross-section (a stronger center stripe
    // than either edge) rather than scoring Pokemon artwork or the blue box
    // header. Ambiguous frames remain unreadable, so no inputs follow.
    const double brightness = home_ui_brightness_scale(screen);
    if (brightness == 0) return std::nullopt;
    size_t blue_best_score = 0;
    size_t blue_second_score = 0;
    size_t blue_center_total = 0;
    std::array<size_t, 2> blue_best{};
    for (size_t row = 0; row < 5; ++row){
        for (size_t column = 0; column < 6; ++column){
            const bool pointer_shape = home_slot_pointer_shape(screen.data(), screen.width(), screen.height(),
                screen.bytes_per_row() / sizeof(uint32_t), row, column,
                home_ui_scaled_color(combine_rgb(0, 110, 190), brightness, brightness < 0.8 ? -8 : 0),
                home_ui_scaled_color(combine_rgb(80, 230, 255), brightness, brightness < 0.8 ? 8 : 0))
                || home_slot_pointer_shape(screen.data(), screen.width(), screen.height(),
                screen.bytes_per_row() / sizeof(uint32_t), row, column,
                home_ui_scaled_color(combine_rgb(190, 40, 0), brightness, brightness < 0.8 ? -8 : 0),
                home_ui_scaled_color(combine_rgb(255, 160, 80), brightness, brightness < 0.8 ? 8 : 0));
            if (!pointer_shape) continue;
            const double left = 0.056 + 0.072 * column;
            const double top = 0.155 + 0.105 * row;
            auto blue_pixels = [&](const ImageFloatBox& region){
                size_t count = 0;
                filter_rgb32_range(
                    count, extract_box_reference(screen, region),
                    home_ui_scaled_color(combine_rgb(0, 110, 190), brightness, brightness < 0.8 ? -8 : 0),
                    home_ui_scaled_color(combine_rgb(80, 230, 255), brightness, brightness < 0.8 ? 8 : 0),
                    Color(0), true
                );
                size_t orange = 0;
                filter_rgb32_range(
                    orange, extract_box_reference(screen, region),
                    home_ui_scaled_color(combine_rgb(190, 40, 0), brightness, brightness < 0.8 ? -8 : 0),
                    home_ui_scaled_color(combine_rgb(255, 160, 80), brightness, brightness < 0.8 ? 8 : 0),
                    Color(0), true
                );
                return count + orange;
            };
            const size_t left_count = blue_pixels({left, top, 0.012, 0.020});
            const size_t center_count = blue_pixels({left + 0.012, top, 0.012, 0.020});
            const size_t right_count = blue_pixels({left + 0.024, top, 0.012, 0.020});
            const size_t score = center_count > std::max(left_count, right_count)
                ? center_count - std::max(left_count, right_count) : 0;
            if (score > blue_best_score){
                blue_second_score = blue_best_score;
                blue_best_score = score;
                blue_center_total = (size_t)(screen.width() * 0.012) * (size_t)(screen.height() * 0.020);
                blue_best = {row, column};
            }else if (score > blue_second_score){
                blue_second_score = score;
            }
        }
    }
    if (blue_center_total != 0 && blue_best_score * 100 >= blue_center_total * 20
        && blue_best_score * 100 >= blue_second_score * 150){
        return blue_best;
    }
    return std::nullopt;
}

static bool cursor_is_at_slot(SingleSwitchProgramEnvironment& env, size_t row, size_t column){
    const VideoSnapshot screen = env.console.video().snapshot();
    const auto position = read_box_slot_cursor_position(screen);
    return position && (*position)[0] == row && (*position)[1] == column;
}

namespace{
class HeldCursorWatcher : public VisualInferenceCallback{
public:
    HeldCursorWatcher(size_t row, size_t column)
        : VisualInferenceCallback("HomeHeldCursorWatcher"), row(row), column(column){}
    void make_overlays(VideoOverlaySet& items) const override{
        items.add(COLOR_RED, {0.066 + 0.072 * column, 0.153 + 0.105 * row, 0.015, 0.016});
    }
    bool process_frame(const ImageViewRGB32& frame, WallClock) override{
        ButtonDetector cancel(COLOR_BLACK, ButtonType::ButtonB, {0.100, 0.956, 0.107, 0.041});
        if (!frame || !cancel.detect(frame)){ matches = 0; return false; }
        const auto marker = extract_box_reference(frame, ImageFloatBox{0.066 + 0.072 * column, 0.153 + 0.105 * row, 0.015, 0.016});
        const double scale = home_ui_brightness_scale(frame);
        size_t black = 0, white = 0;
        filter_rgb32_range(black, marker, uint32_t(0), home_ui_scaled_color(combine_rgb(65,65,65), scale), Color(0), true);
        filter_rgb32_range(white, marker, home_ui_scaled_color(combine_rgb(210,210,210), scale), uint32_t(0xffffffff), Color(0), true);
        const size_t pixels = marker.width() * marker.height();
        const bool hand = pixels && black * 100 > pixels * 7 && white * 100 > pixels * 65
            && (black + white) * 100 > pixels * 96;
        matches = hand ? matches + 1 : 0;
        return matches >= 2;
    }
private:
    size_t row, column, matches = 0;
};
}
bool confirm_home_held_cursor(SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    size_t physical_box, size_t row, size_t column){
    const auto identity = read_home_box_identity(env, context, true);
    if (!identity || identity->first != physical_box || row >= 5 || column >= 6) return false;
    HeldCursorWatcher watcher(row, column);
    const bool matched = wait_until(env.console, context, Seconds(3), {watcher}) == 0;
    env.console.log("HOME_TIMING action=held_cursor physical_box=" + std::to_string(physical_box)
        + " row=" + std::to_string(row) + " column=" + std::to_string(column)
        + " result=" + (matched ? "confirmed" : "mismatch_or_unreadable"));
    return matched;
}

// Move the red cursor to the first slot of the box
// If the cursor is not at the first slot, move the cursor to the left and up one row at a time until it is at the first slot.
bool go_to_first_slot(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    Milliseconds VIDEO_DELAY
) {
    ImageFloatBox first_slot_cursor_box(0.07, 0.15, 0.01, 0.01); //cursor position of the first slot of the box
    VideoSnapshot screen = env.console.video().snapshot();
    FloatPixel first_slot_cursor_color = image_stats(extract_box_reference(screen, first_slot_cursor_box)).average;
    env.console.log("BoxCursor color detection: " + first_slot_cursor_color.to_string());
    VideoOverlaySet BoxRender(env.console);
    BoxRender.add(COLOR_BLUE, first_slot_cursor_box);

    // If the cursor is not at the first slot
    if (first_slot_cursor_color.r <= first_slot_cursor_color.g + first_slot_cursor_color.b) {
        bool cursor_found = false;
        for (uint8_t rows = 0; rows < 7; rows++) {
            for (uint8_t column = 0; column < 5; column++) {
                pbf_press_dpad(context, DPAD_LEFT, 80ms, VIDEO_DELAY);
                context.wait_for_all_requests();
                screen = env.console.video().snapshot();
                first_slot_cursor_color = image_stats(extract_box_reference(screen, first_slot_cursor_box)).average;
                env.console.log("BoxCursor color detection: " + first_slot_cursor_color.to_string());

                if (first_slot_cursor_color.r > first_slot_cursor_color.g + first_slot_cursor_color.b) {
                    cursor_found = true;
                    break;
                }
            }
            if (!cursor_found) {
                pbf_press_dpad(context, DPAD_UP, 80ms, VIDEO_DELAY);
                context.wait_for_all_requests();
                screen = env.console.video().snapshot();
                first_slot_cursor_color = image_stats(extract_box_reference(screen, first_slot_cursor_box)).average;
                env.console.log("BoxCursor color detection: " + first_slot_cursor_color.to_string());

                if (first_slot_cursor_color.r > first_slot_cursor_color.g + first_slot_cursor_color.b) {
                    cursor_found = true;
                    break;
                }
            }
            else {
                break;
            }
        }
        if (!cursor_found) {
            return false;
        }
    }
    BoxRender.clear();
    return true;
}

//Move the cursor to the given coordinates, knowing current pos via the cursor struct
[[nodiscard]] BoxCursor move_cursor_to(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    const BoxCursor& cur_cursor,
    const BoxCursor& dest_cursor,
    Milliseconds GAME_DELAY,
    BoxNavigationTiming* adaptive_timing,
    bool pokemon_held
) {
    const auto move_started = std::chrono::steady_clock::now();
    BoxCursor observed_start = cur_cursor;
    std::optional<std::pair<size_t, size_t>> held_origin;
    if (pokemon_held){
        held_origin = read_home_box_identity(env, context, true);
        if (!held_origin || !confirm_home_held_cursor(env, context, held_origin->first, cur_cursor.row, cur_cursor.column)){
            OperationFailedExceptionWithScreenshot::fire(ErrorReportMode::SEND_ERROR_REPORT,
                "Held HOME cursor cannot be confirmed. No movement was sent.", env.console);
        }
    }

    if (pokemon_held){
        const int64_t target = (int64_t)held_origin->first + (int64_t)dest_cursor.box - (int64_t)cur_cursor.box;
        if (target < 1 || target > (int64_t)held_origin->second || dest_cursor.row >= 5 || dest_cursor.column >= 6){
            throw UserSetupError(env.console, "Held navigation destination is outside the observed HOME range.");
        }
    }

    const bool same_box = cur_cursor.box == dest_cursor.box;
    const bool has_local_movement = cur_cursor.row != dest_cursor.row || cur_cursor.column != dest_cursor.column;
    const bool verify_local_cursor = adaptive_timing != nullptr && !pokemon_held;
    const bool can_learn_local_delay = verify_local_cursor && same_box && has_local_movement;
    const Milliseconds cursor_delay = can_learn_local_delay
        ? adaptive_timing->local_cursor_delay
        : GAME_DELAY;

    std::ostringstream ss;
    ss << "Moving cursor from " << cur_cursor << " to " << dest_cursor;
    env.console.log(ss.str());

    const size_t held_target = pokemon_held
        ? (size_t)((int64_t)held_origin->first + (int64_t)dest_cursor.box - (int64_t)cur_cursor.box) : 0;
    if (!same_box && adaptive_timing && pokemon_held
        && home_overview_route_is_faster(held_origin->first, held_target, held_origin->second,
            *adaptive_timing, cur_cursor.row, cur_cursor.column)){
        move_home_box_held_overview_verified(env, context, held_target, *adaptive_timing,
            cur_cursor.row, cur_cursor.column);
        observed_start.row = 0;
        observed_start.column = 0;
    }else if (!same_box && adaptive_timing && !pokemon_held){
        const auto identity = read_home_box_identity(env, context);
        if (!identity){
            OperationFailedExceptionWithScreenshot::fire(ErrorReportMode::SEND_ERROR_REPORT,
                "Cannot read HOME box identity. Stopped before cross-box navigation.", env.console);
        }
        // Box Sorter uses relative box indices; Living Dex uses absolute ones.
        // Apply the requested difference to the visually read current box.
        const int64_t target = (int64_t)identity->first + (int64_t)dest_cursor.box - (int64_t)cur_cursor.box;
        if (target < 1 || target > (int64_t)identity->second){
            OperationFailedExceptionWithScreenshot::fire(ErrorReportMode::SEND_ERROR_REPORT,
                "Requested HOME box is outside the observed box range.", env.console);
        }
        move_home_box_verified(env, context, (size_t)target, *adaptive_timing,
            home_overview_route_is_faster(identity->first, (size_t)target, identity->second, *adaptive_timing));
        observed_start.row = 0;
        observed_start.column = 0;
    }else{
        for (size_t i = cur_cursor.box; i < dest_cursor.box; ++i){
            pbf_press_button(context, BUTTON_R, 80ms, pokemon_held ? std::max(576ms, GAME_DELAY + 240ms) : GAME_DELAY + 240ms);
        }
        for (size_t i = dest_cursor.box; i < cur_cursor.box; ++i){
            pbf_press_button(context, BUTTON_L, 80ms, pokemon_held ? std::max(576ms, GAME_DELAY + 240ms) : GAME_DELAY + 240ms);
        }
    }

    // direct nav up or down through rows
    if (pokemon_held || (!(observed_start.row == 0 && dest_cursor.row == 4) && !(dest_cursor.row == 0 && observed_start.row == 4))) {
        for (size_t i = observed_start.row; i < dest_cursor.row; ++i) {
            pbf_press_dpad(context, DPAD_DOWN, 80ms, cursor_delay);
        }
        for (size_t i = dest_cursor.row; i < observed_start.row; ++i) {
            pbf_press_dpad(context, DPAD_UP, 80ms, cursor_delay);
        }
    }
    else { // wrap around is faster to move between first or last row
        if (observed_start.row == 0 && dest_cursor.row == 4) {
            for (size_t i = 0; i <= 2; ++i) {
                pbf_press_dpad(context, DPAD_UP, 80ms, cursor_delay);
            }
        }
        else {
            for (size_t i = 0; i <= 2; ++i) {
                pbf_press_dpad(context, DPAD_DOWN, 80ms, cursor_delay);
            }
        }
    }

    // direct nav forward or backward through columns
    if ((dest_cursor.column > observed_start.column && dest_cursor.column - observed_start.column <= 3) || (observed_start.column > dest_cursor.column && observed_start.column - dest_cursor.column <= 3)) {
        for (size_t i = observed_start.column; i < dest_cursor.column; ++i) {
            pbf_press_dpad(context, DPAD_RIGHT, 80ms, cursor_delay);
        }
        for (size_t i = dest_cursor.column; i < observed_start.column; ++i) {
            pbf_press_dpad(context, DPAD_LEFT, 80ms, cursor_delay);
        }
    }
    else { // wrap around is faster if direct movement is more than 3 away
        if (dest_cursor.column > observed_start.column) {
            for (size_t i = 0; i < BOX_COLS - (dest_cursor.column - observed_start.column); ++i) {
                pbf_press_dpad(context, DPAD_LEFT, 80ms, cursor_delay);
            }
        }
        if (observed_start.column > dest_cursor.column) {
            for (size_t i = 0; i < BOX_COLS - (observed_start.column - dest_cursor.column); ++i) {
                pbf_press_dpad(context, DPAD_RIGHT, 80ms, cursor_delay);
            }
        }
    }

    context.wait_for_all_requests();

    if (pokemon_held){
        const int64_t expected = (int64_t)held_origin->first + (int64_t)dest_cursor.box - (int64_t)cur_cursor.box;
        if (expected < 1 || expected > (int64_t)held_origin->second
            || !confirm_home_held_cursor(env, context, (size_t)expected, dest_cursor.row, dest_cursor.column)){
            OperationFailedExceptionWithScreenshot::fire(ErrorReportMode::SEND_ERROR_REPORT,
                "Held HOME destination was not confirmed. Stopped without dropping the Pokemon.", env.console);
        }
    }
    const auto queue_finished = std::chrono::steady_clock::now();
    bool cursor_confirmed = false;
    Milliseconds extra_settle = 0ms;
    if (verify_local_cursor){
        // A rejected second frame restarts agreement within the same timeout.
        // Do not accept different slots or send movement while observing.
        const auto confirm_started = std::chrono::steady_clock::now();
        size_t matching_frames = 0, observation_waits = 0;
        do{
            matching_frames = cursor_is_at_slot(env, dest_cursor.row, dest_cursor.column)
                ? matching_frames + 1 : 0;
            if (matching_frames >= 3){cursor_confirmed = true;break;}
            pbf_wait(context, 50ms);
            context.wait_for_all_requests();
            ++observation_waits;
        }while(std::chrono::steady_clock::now() - confirm_started < 2000ms);
        // The two regular inter-frame waits and queue overhead are not backoff.
        extra_settle = observation_waits > 2 ? Milliseconds((observation_waits - 2) * 50) : 0ms;
    }
    const auto queue_ms = std::chrono::duration_cast<Milliseconds>(queue_finished - move_started).count();
    const auto observed_ms = std::chrono::duration_cast<Milliseconds>(std::chrono::steady_clock::now() - move_started).count();
    std::ostringstream timing;
    timing << "HOME_TIMING action=cursor_move"
           << " row=" << dest_cursor.row
           << " column=" << dest_cursor.column
           << " box_distance=" << (cur_cursor.box > dest_cursor.box ? cur_cursor.box - dest_cursor.box : dest_cursor.box - cur_cursor.box)
           << " queue_ms=" << queue_ms
           << " detection_ms=" << observed_ms
           << " cursor=" << (pokemon_held ? "skipped_held" : !verify_local_cursor ? "not_checked" : cursor_confirmed ? "confirmed" : "mismatch")
           << " extra_settle_ms=" << extra_settle.count()
           << " delay_ms=" << cursor_delay.count();
    env.console.log(timing.str());

    if (verify_local_cursor && !cursor_confirmed){
        OperationFailedExceptionWithScreenshot::fire(
            ErrorReportMode::SEND_ERROR_REPORT,
            "Pokemon HOME cursor did not reach the expected slot. Sorting stopped before the next action.",
            env.console
        );
    }
    if (can_learn_local_delay){
        const Milliseconds old_delay = adaptive_timing->local_cursor_delay;
        adaptive_timing->confirmed_local_move(extra_settle);
        if (adaptive_timing->local_cursor_delay != old_delay){
            env.console.log(
                "HOME_TIMING adaptive action=local_cursor_delay confirmed_moves="
                + std::to_string(adaptive_timing->confirmed_local_moves)
                + " old_ms=" + std::to_string(old_delay.count())
                + " new_ms=" + std::to_string(adaptive_timing->local_cursor_delay.count())
            );
        }
    }
    return dest_cursor;
}

// Read current screen to find occupied and empty slots in the box.
// Add a placeholder value for each slot in order into `boxes_data`. For empty slot the value is just std::nullopt, while
// for the occupied slot it is an empty pokemon struct `CollectedPokemonInfo`.
// Return the (row, col) index of the first pokemon (aka non-empty) slot in the box.
std::array<size_t, 2> find_occupied_slots_in_box(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    std::vector<std::optional<CollectedPokemonInfo>>& boxes_data,
    const std::vector<SortingRule>& sort_preferences)
{
    //BoxSorter_Descriptor::Stats& stats = env.current_stats< BoxSorter_Descriptor::Stats>();

    VideoSnapshot screen = env.console.video().snapshot();

    std::ostringstream ss;
    ss << "\n";

    std::array<size_t, 2> first_pokemon_slot = { SIZE_MAX, SIZE_MAX };

    int num_empty_slots = 0;
    for (size_t row = 0; row < BOX_ROWS; row++) {
        for (size_t col = 0; col < BOX_COLS; col++) {
            ImageFloatBox slot_box(0.06 + (0.072 * col), 0.2 + (0.105 * row), 0.03, 0.057);
            int current_box_value = (int)image_stddev(extract_box_reference(screen, slot_box)).sum();

            ss << current_box_value;

            //checking color to know if a pokemon is on the slot or not
            if (current_box_value < 10) {
                //stats.empty++;
                num_empty_slots++;
                boxes_data.push_back(std::nullopt); //empty optional to make sorting easier later
                ss << "\u274c ";    //  "X"
            }
            else {
                if (first_pokemon_slot[0] == SIZE_MAX) {
                    first_pokemon_slot = { row, col };
                }
                //stats.pkmn++;
                boxes_data.push_back(
                    CollectedPokemonInfo{
                        .preferences = &sort_preferences
                    }
                ); //default initialised pokemon to know there is a pokemon here that needs a value
                ss << "\u2705 ";    //  checkbox
            }
        }
        ss << "\n";
    }

    //env.update_stats();
    env.log(ss.str());
    env.log_to_ui("Empty: " + std::to_string(num_empty_slots) + "/30");

    return first_pokemon_slot;
}

// Read the current summary screen and assign various pokemon info into `cur_pokemon_info`
#include "PokemonHome_CollectionObservation.inc"

// Keep upstream's read-only summary API for existing programs and tests.
void read_summary_screen(
    SingleSwitchProgramEnvironment& env, CollectedPokemonInfo& cur_pokemon_info,
    Language ot_name_language, Language home_language
){
    VideoOverlaySet video_overlay_set(env.console);

    ImageFloatBox pokemon_box(0.69, 0.18, 0.28, 0.46); // pokemon render pos
    ImageFloatBox type_box(0.615, 0.240, 0.071, 0.057); // Type symbols
    video_overlay_set.add(COLOR_DARK_BLUE, pokemon_box);
    video_overlay_set.add(COLOR_RED, type_box);

    GigantamaxDetector gmax_detector(COLOR_RED, &env.console.overlay());
    ShinyDetector shiny_detector(COLOR_RED, &env.console.overlay());
    AlphaDetector alpha_detector(COLOR_RED, &env.console.overlay());
    OriginMarkReader origin_mark_reader(COLOR_RED, &env.console.overlay());
    SummaryReader summary_reader(COLOR_RED);

    gmax_detector.make_overlays(video_overlay_set);
    shiny_detector.make_overlays(video_overlay_set);
    alpha_detector.make_overlays(video_overlay_set);
    origin_mark_reader.make_overlays(video_overlay_set);
    summary_reader.make_overlays(video_overlay_set);
    BoxGenderDetector::make_overlays(video_overlay_set);

    VideoSnapshot screen = env.console.video().snapshot();

    const int dex_number = summary_reader.read_national_dex(env.console, screen);
    if (dex_number <= 0 || dex_number > static_cast<int>(NATIONAL_DEX_SLUGS().size())) {
        OperationFailedExceptionWithScreenshot::fire(
            ErrorReportMode::SEND_ERROR_REPORT,
            "BoxSorter Check Summary: Unable to read a correct dex number, found: " + std::to_string(dex_number),
            env.console
        );
    }
    cur_pokemon_info.dex_number = static_cast<uint16_t>(dex_number);
    cur_pokemon_info.name_slug = NATIONAL_DEX_SLUGS()[dex_number - 1];

    const bool is_shiny = shiny_detector.detect(screen);
    cur_pokemon_info.shiny = is_shiny;

    cur_pokemon_info.gmax = gmax_detector.detect(screen);

    cur_pokemon_info.tera_type = read_pokemon_tera_type(screen);

    const bool is_alpha = alpha_detector.detect(screen);
    cur_pokemon_info.alpha = is_alpha;

    BallReader ball_reader(env.console);
    cur_pokemon_info.ball_slug = ball_reader.read_ball(screen);

    const StatsHuntGenderFilter gender = BoxGenderDetector::detect(screen);
    env.console.log("Gender: " + gender_to_string(gender), COLOR_GREEN);
    cur_pokemon_info.gender = gender;

    const int ot_id = summary_reader.read_original_trainer_id(env.console, screen);
    if (ot_id < 0 || ot_id > 999'999) {
        dump_image(env.console, ProgramInfo(), "ReadSummary_OT", screen);
    }
    cur_pokemon_info.ot_id = ot_id;

    auto [primary_type, secondary_type] = read_pokemon_types(screen, type_box, PokemonTypeGeneration::GEN9);

    cur_pokemon_info.primary_type = primary_type;
    cur_pokemon_info.secondary_type = secondary_type;

    cur_pokemon_info.ot_name = summary_reader.read_original_trainer_name(
        ot_name_language, screen
    );

    if (home_language != Language::None){
        cur_pokemon_info.nature = summary_reader.read_nature(
            env.console, home_language, screen
        );
        cur_pokemon_info.ability = summary_reader.read_ability(
            env.console, home_language, screen
        );
    }

    cur_pokemon_info.origin_mark = origin_mark_reader.read_mark(screen);

    cur_pokemon_info.language_of_origin = summary_reader.read_language_of_origin(screen);

    env.log_to_ui(create_overlay_info(cur_pokemon_info));
    video_overlay_set.clear();

}

// Collection capture adds repeated identity/Judge observations without advancing
// to another Pokemon. The scanning caller owns summary navigation.
void read_summary_screen(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    CollectedPokemonInfo& cur_pokemon_info, Language ot_name_language, Language home_language,
    QJsonObject* collection_observation, const HomeBoxVisualEvidence* visual_evidence, bool collection_identity_only
){
    if (collection_observation) ensure_home_numeric_summary(env, context);
    read_summary_screen(env, cur_pokemon_info, ot_name_language, home_language);
    if (collection_observation){
        *collection_observation = read_home_collection_observation(env, context, cur_pokemon_info,
            home_language, visual_evidence, collection_identity_only);
    }
}

void print_boxes_data(const std::vector<std::optional<CollectedPokemonInfo>>& boxes_data, SingleSwitchProgramEnvironment& env) {
    std::ostringstream ss;
    for (const std::optional<CollectedPokemonInfo>& pokemon : boxes_data) {
        ss << pokemon << "\n";
    }
    env.console.log(ss.str());
}

void exit_menus(SingleSwitchProgramEnvironment& env, ProControllerContext& context, std::chrono::milliseconds video_delay) {
    VideoSnapshot screen = env.console.video().snapshot();
    VideoOverlaySet video_overlay_set(env.console);

    ImageFloatBox select_check(0.495, 0.0045, 0.01, 0.005); // square color to check which mode is active
    FloatPixel image_value = image_stats(extract_box_reference(screen, select_check)).average;

    env.console.log("Color detected from the select square: " + image_value.to_string());

    //if the correct color is not detected, getting out of every possible menu to make sure the program work no matter where you start it in your pokemon home
    video_overlay_set.add(COLOR_BLUE, select_check);
    if (image_value.r <= image_value.g + image_value.b) {
        for (int var = 0; var < 5; ++var) {
            pbf_press_button(context, BUTTON_B, 80ms, video_delay + 80ms);
        }
        context.wait_for_all_requests();
        context.wait_for(std::chrono::milliseconds(video_delay));
        screen = env.console.video().snapshot();
        image_value = image_stats(extract_box_reference(screen, select_check)).average;
        env.console.log("Color detected from the select square: " + image_value.to_string());
        if (image_value.r <= image_value.g + image_value.b) {
            for (int i = 0; i < 2; ++i) {
                pbf_press_button(context, BUTTON_ZR, 80ms, video_delay + 240ms); //additional delay because this animation is slower than the rest
                context.wait_for_all_requests();
                screen = env.console.video().snapshot();
                image_value = image_stats(extract_box_reference(screen, select_check)).average;
                env.console.log("Color detected from the select square: " + image_value.to_string());
                if (image_value.r > image_value.g + image_value.b) {
                    break;
                }
                else if (i == 1) {
                    dump_image(env.console, ProgramInfo(), "SelectSquare", screen);
                    env.console.log("ERROR: Could not find correct color mode please check color logs and timings\n", COLOR_RED);
                    return;
                }
            }
        }
    }

    video_overlay_set.clear();
}

}
}
}

/*  Home Box Sorter
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

/* TODO ideas
read pokemon name and store the slug (easier to detect missread than reading a number)
Optimise the swapping algo
Add enum for ball ? Also, BDSP is reading from swsh data. Worth refactoring ?

ideas for more checks :
ability
nature
type
original game
OT
moves
stats
level
surname
language
"stamps"
*/

#include <array>
#include <map>
#include <optional>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <functional>
#include <cmath>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QTemporaryFile>
#include "CommonFramework/VideoPipeline/VideoFeed.h"
#include "CommonFramework/ImageTools/ImageBoxes.h"
#include "CommonFramework/ImageTypes/ImageRGB32.h"
#include "CommonTools/InferenceCallbacks/VisualInferenceCallback.h"
#include "CommonTools/OCR/OCR_Routines.h"
#include "Common/Cpp/Exceptions.h"
#include "Common/Cpp/Json/JsonObject.h"
#include "CommonFramework/Exceptions/OperationFailedExceptionWithScreenshot.h"
#include "CommonFramework/ImageTools/ImageStats.h"
#include "CommonFramework/Notifications/ProgramNotifications.h"
#include "CommonFramework/ProgramStats/StatsTracking.h"
#include "CommonFramework/VideoPipeline/VideoOverlay.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "CommonTools/Async/InferenceRoutines.h"
#include "CommonTools/StartupChecks/StartProgramChecks.h"
#include "CommonTools/VisualDetectors/FrozenImageDetector.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "Pokemon/Pokemon_Strings.h"
#include "Pokemon/Pokemon_BoxCursor.h"
#include "Pokemon/Pokemon_CollectedPokemonInfo.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_BoxViewDetector.h"
#include "VideoGames/PokemonHome/Inference/PokemonHome_SummaryScreenDetector.h"
#include "PokemonHome_BoxNavigation.h"
#include "PokemonHome_BoxSorter.h"
#include "PokemonHome_CollectionManager.h"
#include "PokemonHome_CollectionExecutionPolicy.h"
#include "PokemonHome_CollectionMenuPolicy.h"
#include "PokemonHome_CollectionIndexCheckpoint.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonHome{

static void log_home_transition_timing(
    SingleSwitchProgramEnvironment& env,
    const char* action,
    std::chrono::steady_clock::time_point started,
    std::chrono::steady_clock::time_point queue_finished,
    bool confirmed
){
    const auto queue_ms = std::chrono::duration_cast<Milliseconds>(queue_finished - started).count();
    const auto total_ms = std::chrono::duration_cast<Milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::ostringstream line;
    line << "HOME_TIMING action=" << action
         << " queue_ms=" << queue_ms
         << " detection_ms=" << total_ms
         << " result=" << (confirmed ? "confirmed" : "timeout");
    env.console.log(line.str());
}
using namespace Pokemon;


BoxSorter_Descriptor::BoxSorter_Descriptor()
    : SingleSwitchProgramDescriptor(
        "PokemonHome:BoxSorter",
        STRING_POKEMON + " Home", "Box Sorter",
        "Programs/PokemonHome/BoxSorter.html",
        "Order boxes of " + STRING_POKEMON + ".",
        ProgramControllerClass::StandardController_RequiresPrecision,
        FeedbackType::REQUIRED,
        AllowCommandsWhenRunning::DISABLE_COMMANDS
    )
{}
struct BoxSorter_Descriptor::Stats : public StatsTracker{
    Stats()
        : pkmn(m_stats["Pokemon"])
        , empty(m_stats["Empty Slots"])
        , compare(m_stats["Compares"])
        , swaps(m_stats["Swaps"])
    {
        m_display_order.emplace_back(Stat("Pokemon"));
        m_display_order.emplace_back(Stat("Empty Slots"));
        m_display_order.emplace_back(Stat("Compares"));
        m_display_order.emplace_back(Stat("Swaps"));
    }
    std::atomic<uint64_t>& pkmn;
    std::atomic<uint64_t>& empty;
    std::atomic<uint64_t>& compare;
    std::atomic<uint64_t>& swaps;
};
std::unique_ptr<StatsTracker> BoxSorter_Descriptor::make_stats() const{
    return std::unique_ptr<StatsTracker>(new Stats());
}

BoxSorter::BoxSorter()
    : BOX_NUMBER(
        "<b>Number of Boxes to Sort:</b>",
        LockMode::LOCK_WHILE_RUNNING,
        1, 1, MAX_HOME_BOXES
    )
    , HOME_LANGUAGE(
        "<b>Home Language:</b>",
        {
            Language::English,
            Language::Japanese,
            Language::Spanish,
            Language::French,
            Language::German,
            Language::Italian,
            Language::Korean,
            Language::ChineseSimplified,
            Language::ChineseTraditional,
        },
        LockMode::LOCK_WHILE_RUNNING
    )
    , VIDEO_DELAY(
          "<b>Capture Card Delay:</b>",
          LockMode::LOCK_WHILE_RUNNING,
          "400 ms"
    )
    , GAME_DELAY(
          "<b>" + STRING_POKEMON + " Home App Delay:</b>",
          LockMode::LOCK_WHILE_RUNNING,
          "240 ms"
    )
    , SORT_TABLE(
          "<b>Sort Order Rules:</b><br>Sort order rules will be applied top to bottom."
    )
    , OUTPUT_FILE(
          false,
          "<b>Output File:</b><br>JSON file basename to catalogue box info found by the program.",
          LockMode::LOCK_WHILE_RUNNING,
          "box_order",
          "box_order"
    )
    , DRY_RUN(
          "<b>Dry Run:</b><br>Catalogue and make sorting plan without execution. Check output at <output_file>.json and <output_file>-sorted.json)",
          LockMode::LOCK_WHILE_RUNNING,
          false
    )
    , SAVED_PLAN(false, "<b>Saved Plan File (optional):</b><br>Load an indexed plan instead of creating a new sort. Leave empty for normal sorting.", LockMode::LOCK_WHILE_RUNNING, "", "")
    , RESUME_SAVED_PLAN("<b>Resume Saved Plan:</b><br>Verify the checkpoint against a fresh scan before continuing.", LockMode::LOCK_WHILE_RUNNING, false)
    , PLAN_MOVES_PER_RUN("<b>Plan Moves This Run (0 = all):</b><br>Pause after verified moves; the checkpoint is saved beside the plan.", LockMode::LOCK_WHILE_RUNNING, 1, 0, 65535)
    , NOTIFICATIONS({
          &NOTIFICATION_PROGRAM_FINISH
    })
{
    PA_ADD_OPTION(BOX_NUMBER); //number of box to check and sort
    PA_ADD_OPTION(HOME_LANGUAGE);
    PA_ADD_OPTION(VIDEO_DELAY); //delay for every input that need video feedback, user will be able to modify this to enhance capture card delay compatibility
    PA_ADD_OPTION(GAME_DELAY);  //delay for non video feedback, this way I can go as fast as pokemon home can handle movement when needed
    PA_ADD_OPTION(SORT_TABLE);
    PA_ADD_OPTION(OUTPUT_FILE);
    PA_ADD_OPTION(DRY_RUN);
    PA_ADD_OPTION(SAVED_PLAN);
    PA_ADD_OPTION(RESUME_SAVED_PLAN);
    PA_ADD_OPTION(PLAN_MOVES_PER_RUN);
    PA_ADD_OPTION(NOTIFICATIONS);
}

// Fail closed before either A press: the orange single-selection mode opens
// a context menu; blue Swap mode picks up a Pokemon instead.
class HomeSummaryEntryWatcher : public VisualInferenceCallback{
public:
    enum class State{MENU_MODE, MENU_MOVE, MENU_SUMMARY};
    explicit HomeSummaryEntryWatcher(State state)
        : VisualInferenceCallback("HomeSummaryEntryWatcher"), m_state(state){}
    void make_overlays(VideoOverlaySet&) const override{}
    bool process_frame(const ImageViewRGB32& frame, WallClock) override{
        bool matched = false;
        if (frame){
            auto orange_fraction = [&](const ImageFloatBox& box){
                auto crop = extract_box_reference(frame, box);
                size_t count = 0;
                for (size_t y = 0; y < crop.height(); ++y){
                    for (size_t x = 0; x < crop.width(); ++x){
                        const uint32_t pixel = crop.pixel(x, y);
                        const unsigned r = (pixel >> 16) & 255;
                        const unsigned g = (pixel >> 8) & 255;
                        const unsigned b = pixel & 255;
                        count += r > 45 && r > g * 1.15 && g > b * 1.8 && b < r * .45;
                    }
                }
                return double(count) / (crop.width() * crop.height());
            };
            if (m_state == State::MENU_MODE){
                BoxViewDetector box;
                matched = box.detect(frame)
                    && orange_fraction({.476, .013, .002, .05}) > .65
                    && orange_fraction({.521, .013, .002, .05}) > .65;
            }else{
                {
                    const auto highlight = home_summary_menu_highlight(frame.data(),
                        frame.width(), frame.height(), frame.bytes_per_row() / sizeof(uint32_t));
                    if (highlight){
                        auto read_label = [&](double center_y){
                            auto label = extract_box_reference(frame, ImageFloatBox{
                                highlight->left + .022, center_y - .0165,
                                highlight->width - .034, .033}).copy();
                            unsigned maximum_blue = 0;
                            for (size_t y = 0; y < label.height(); ++y){
                                for (size_t x = 0; x < label.width(); ++x){
                                    maximum_blue = std::max(maximum_blue, label.pixel(x, y) & 255);
                                }
                            }
                            for (size_t y = 0; y < label.height(); ++y){
                                for (size_t x = 0; x < label.width(); ++x){
                                    label.pixel(x, y) = (label.pixel(x, y) & 255) > maximum_blue / 2
                                        ? 0xff000000 : 0xffffffff;
                                }
                            }
                            return OCR::tesseract_ocr_read(Language::English, label, OCR::PageSegMode::SINGLE_LINE);
                        };
                        const auto selected_text = read_label(highlight->center_y);
                        const bool move_selected = selected_text.find("Move") != std::string::npos;
                        raw_text = m_state == State::MENU_MOVE
                            ? read_label(highlight->center_y + .084) : selected_text;
                        matched = raw_text.find("Check Summary") != std::string::npos
                            && (m_state != State::MENU_MOVE || move_selected);
                    }
                }
            }
        }
        m_matches = matched ? m_matches + 1 : 0;
        return m_matches >= 2;
    }
public:
    std::string raw_text;
private:
    State m_state;
    size_t m_matches = 0;
};

static void ensure_home_summary_menu_mode(SingleSwitchProgramEnvironment& env, ProControllerContext& context){
    for (size_t attempt = 0; attempt < 3; ++attempt){
        HomeSummaryEntryWatcher mode(HomeSummaryEntryWatcher::State::MENU_MODE);
        if (wait_until(env.console, context, Seconds(2), {mode}) == 0){
            env.console.log("HOME_SCAN selection_mode=menu result=confirmed");
            return;
        }
        BoxViewDetector box;
        if (!box.detect(env.console.video().snapshot()) || attempt == 2) break;
        // ZR changes only selection mode; never pick up or place Pokemon here.
        pbf_press_button(context, BUTTON_ZR, 80ms, 480ms);
        context.wait_for_all_requests();
    }
    throw UserSetupError(env.console, "Cannot confirm HOME single-selection menu mode. No Pokemon selection was sent.");
}

static std::vector<std::optional<CollectedPokemonInfo>> scan_home_range(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    size_t box_count, const std::pair<size_t, size_t>& origin,
    const std::vector<SortingRule>& sort_preferences, Milliseconds VIDEO_DELAY,
    Milliseconds GAME_DELAY, Language HOME_LANGUAGE, BoxNavigationTiming& adaptive_timing,
    std::vector<QJsonObject>* collection_observations = nullptr, bool collection_identity_only = false
){
    ensure_home_summary_menu_mode(env, context);
    std::vector<std::optional<CollectedPokemonInfo>> boxes_data;
    BoxCursor dest_cursor, nav_cursor{0, 0, 0};
    const auto scan_origin = std::optional<std::pair<size_t, size_t>>(origin);
    VideoOverlaySet video_overlay_set(env.console);
    BoxViewWatcher box_view_watcher(&env.console.overlay());
    SummaryScreenWatcher summary_screen_watcher(&env.console.overlay());
    //cycle through each box
    for (size_t box_idx = 0; box_idx < box_count; box_idx++){
        if(box_idx != 0){
            // Press button R to move to next box
            const auto identity = read_home_box_identity(env, context);
            if (!identity || identity->first == identity->second){
                throw UserSetupError(env.logger(), "Cannot confirm the next HOME box within the observed box range.");
            }
            move_home_box_verified(env, context, identity->first + 1, adaptive_timing, false);
        }else{
            // Read the actual selected slot before moving. The legacy
            // first-slot color heuristic assumes an older HOME cursor marker
            // and can walk unpredictably when the newer blue pointer is used.
            const auto start = read_home_slot_cursor_stable(env, context);
            if (!start){
                OperationFailedExceptionWithScreenshot::fire(
                    ErrorReportMode::SEND_ERROR_REPORT,
                    "HOME selected slot stayed unreadable for 3 seconds. Completed inventory boxes remain saved; no selection was sent.",
                    env.console);
            }
            nav_cursor = BoxCursor{0, (*start)[0], (*start)[1]};
            dest_cursor = BoxCursor{0, 0, 0};
            nav_cursor = move_cursor_to(env, context, nav_cursor, dest_cursor, GAME_DELAY, &adaptive_timing);
        }

        context.wait_for_all_requests();
        const std::string log_msg = "Checking box " + std::to_string(box_idx+1) + "/" + std::to_string(box_count);
        env.log(log_msg);
        env.console.overlay().add_log(log_msg);


        const auto observed_box = read_home_box_identity(env, context);
        if (!observed_box || observed_box->first != scan_origin->first + box_idx
            || observed_box->second != scan_origin->second){
            throw UserSetupError(env.console, "Physical HOME box changed unexpectedly during scanning. Rescan before sorting.");
        }

        // Check box grid color stddev to find occupied slots and fill boxes_data with placeholder pokemon info.
        const std::array<size_t, 2> first_pokemon_slot = find_occupied_slots_in_box(env, context, boxes_data, sort_preferences);

        if (collection_observations) collection_observations->resize(boxes_data.size());
        // The tooltip is above the selected slot. At (0,0) it sits over the
        // header, leaving the entire icon grid unobstructed. Reuse the existing
        // first-slot navigation; this capture adds no navigation inputs.
        VideoSnapshot visual_first, visual_second;
        ImageViewRGB32 visual_first_view, visual_second_view;
        bool visual_frames_independent = false;
        if (collection_observations && !collection_identity_only){
            visual_first = env.console.video().snapshot();
            pbf_wait(context, 150ms);
            context.wait_for_all_requests();
            visual_second = env.console.video().snapshot();
            const auto first_cursor = read_box_slot_cursor_position(visual_first);
            const auto second_cursor = read_box_slot_cursor_position(visual_second);
            BoxViewDetector ordinary;
            visual_frames_independent = visual_first && visual_second
                && visual_second.timestamp > visual_first.timestamp
                && ordinary.detect(visual_first) && ordinary.detect(visual_second)
                && first_cursor && second_cursor && (*first_cursor)[0] == 0 && (*first_cursor)[1] == 0
                && *first_cursor == *second_cursor;
            if (visual_frames_independent){
                visual_first_view = visual_first;
                visual_second_view = visual_second;
            }
            env.log("HOME_VISUAL_CAPTURE box=" + std::to_string(observed_box->first)
                + " independent_frames=" + (visual_frames_independent ? "true" : "false")
                + " cursor=first_slot navigation_inputs=0");
        }

        //enter the summary screen to read pokemon info
        if (first_pokemon_slot[0] != SIZE_MAX){
            // moving cursor to the first pokemon slot
            dest_cursor = {0, first_pokemon_slot[0], first_pokemon_slot[1]};
            nav_cursor = move_cursor_to(env, context, nav_cursor, dest_cursor, GAME_DELAY, &adaptive_timing);

            env.log_to_ui("Checking Summary...");

            ensure_home_summary_menu_mode(env, context);
            const auto summary_transition_started = std::chrono::steady_clock::now();
            pbf_press_button(context, BUTTON_A, 80ms, GAME_DELAY);
            context.wait_for_all_requests();
            video_overlay_set.clear();
            HomeSummaryEntryWatcher menu_move(HomeSummaryEntryWatcher::State::MENU_MOVE);
            if (wait_until(env.console, context, Seconds(4), {menu_move}) != 0){
                env.console.log("HOME_SCAN menu_ocr=" + menu_move.raw_text);
                throw UserSetupError(env.console, "HOME Pokemon menu was not confirmed. No menu navigation or confirmation was sent.");
            }
            pbf_press_dpad(context, DPAD_DOWN, 80ms, GAME_DELAY);
            context.wait_for_all_requests();
            HomeSummaryEntryWatcher menu_summary(HomeSummaryEntryWatcher::State::MENU_SUMMARY);
            if (wait_until(env.console, context, Seconds(4), {menu_summary}) != 0){
                env.console.log("HOME_SCAN menu_ocr=" + menu_summary.raw_text);
                throw UserSetupError(env.console, "HOME Check Summary selection was not confirmed. No confirmation was sent.");
            }
            env.console.log("HOME_SCAN summary_menu=confirmed");
            pbf_press_button(context, BUTTON_A, 80ms, 100ms);
            context.wait_for_all_requests();
            const auto summary_queue_finished = std::chrono::steady_clock::now();
            int ret = wait_until(env.console, context, Seconds(5), {summary_screen_watcher});
            log_home_transition_timing(env, "summary_open", summary_transition_started, summary_queue_finished, ret == 0);
            if (ret != 0){
                OperationFailedExceptionWithScreenshot::fire(
                    ErrorReportMode::SEND_ERROR_REPORT, "HomeBoxSorter(): does not find summary screen after 5 sec", env.console
                );
            }

            // cycle through each summary of the current box and fill pokemon information
            for (size_t row = 0; row < BOX_ROWS; row++){
                for (size_t column = 0; column < BOX_COLS; column++){
                    const size_t global_idx = to_global_index(box_idx, row, column);
                    if (!boxes_data[global_idx].has_value()){
                        continue;
                    }

                    // Read the summary screen and assign data to boxes_data[global_idx]
                    const HomeBoxVisualEvidence visual{&visual_first_view, &visual_second_view,
                        row, column, visual_frames_independent};
                    read_summary_screen(env, context, boxes_data[global_idx].value(), Language::None, HOME_LANGUAGE,
                        collection_observations ? &collection_observations->at(global_idx) : nullptr,
                        collection_observations && !collection_identity_only ? &visual : nullptr, collection_identity_only);

                    // Press button R to go to next summary screen
                    pbf_press_button(context, BUTTON_R, 80ms, collection_observations ? 600ms : 300ms);
                    context.wait_for_all_requests();

                    // Wait for the summary screen transition to end
                    FrozenImageDetector frozen_image_detector(COLOR_GREEN, { 0.388, 0.238, 0.109, 0.062 }, Milliseconds(80), 20);
                    frozen_image_detector.make_overlays(video_overlay_set);
                    wait_until(env.console, context, 5s, { frozen_image_detector });
                }
            }

            // log detailed pokemon infomation of this box
            std::ostringstream ss;
            ss << std::endl;
            for (size_t row = 0; row < BOX_ROWS; row++){
                for (size_t column = 0; column < BOX_COLS; column++){
                    ss << "[" << row << ", " << column << "]: " << boxes_data[to_global_index(box_idx, row, column)] << std::endl;
                }
            }
            env.log(ss.str());

            //get out of summary with a lot of delay because it's slow for some reasons
            const auto box_transition_started = std::chrono::steady_clock::now();
            pbf_press_button(context, BUTTON_B, 80ms, 100ms);
            context.wait_for_all_requests();
            const auto box_queue_finished = std::chrono::steady_clock::now();
            ret = wait_until(env.console, context, Seconds(5), {box_view_watcher});
            log_home_transition_timing(env, "box_view_return", box_transition_started, box_queue_finished, ret == 0);
            if (ret != 0){
                OperationFailedExceptionWithScreenshot::fire(
                    ErrorReportMode::SEND_ERROR_REPORT, "HomeBoxSorter(): does not find box view after 5 sec", env.console
                );
            }
            video_overlay_set.clear();
        }

        video_overlay_set.clear();

        dest_cursor = {0, 0, 0};
        nav_cursor = move_cursor_to(env, context, nav_cursor, dest_cursor, GAME_DELAY, &adaptive_timing);
        context.wait_for_all_requests();
    } // end if (find_first_poke){

    return boxes_data;
}

#include "PokemonHome_SavedPlanExecution.inc"

static QJsonValue read_home_saved_plan_slot(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context, size_t index,
    BoxCursor& cursor, BoxNavigationTiming& timing, Milliseconds game_delay, Language language,
    const std::vector<SortingRule>& preferences, const std::string& scratch_path, bool collection_identity,
    bool verify_judge = false
){
    const BoxCursor destination(index);
    cursor = move_cursor_to(env, context, cursor, destination, game_delay, &timing);
    std::vector<std::optional<CollectedPokemonInfo>> occupancy;
    find_occupied_slots_in_box(env, context, occupancy, preferences);
    auto info = occupancy[index % 30];
    QJsonObject observation;
    if (info){
        ensure_home_summary_menu_mode(env, context);
        pbf_press_button(context, BUTTON_A, 80ms, game_delay);
        context.wait_for_all_requests();
        HomeSummaryEntryWatcher menu_move(HomeSummaryEntryWatcher::State::MENU_MOVE);
        if (wait_until(env.console, context, Seconds(4), {menu_move}) != 0){
            throw UserSetupError(env.console, "Cannot confirm Pokemon menu before verification.");
        }
        pbf_press_dpad(context, DPAD_DOWN, 80ms, game_delay);
        context.wait_for_all_requests();
        HomeSummaryEntryWatcher menu_summary(HomeSummaryEntryWatcher::State::MENU_SUMMARY);
        if (wait_until(env.console, context, Seconds(4), {menu_summary}) != 0){
            throw UserSetupError(env.console, "Cannot confirm Check Summary before verification.");
        }
        bool summary_open = false;
        for (size_t attempt = 0; attempt < 2; ++attempt){
            pbf_press_button(context, BUTTON_A, 80ms, game_delay);
            context.wait_for_all_requests();
            SummaryScreenWatcher summary(&env.console.overlay());
            if (wait_until(env.console, context, Seconds(5), {summary}) == 0){
                summary_open = true;
                break;
            }
            if (attempt == 1) break;
            // HOME can ignore confirmation during a menu animation. Retry only
            // if the exact safe menu item is still selected in fresh frames.
            HomeSummaryEntryWatcher still_summary(HomeSummaryEntryWatcher::State::MENU_SUMMARY);
            if (wait_until(env.console, context, Seconds(1), {still_summary}) != 0) break;
            env.log("HOME_PLAN action=summary_entry result=retry menu=check_summary");
        }
        if (!summary_open){
            throw UserSetupError(env.console, "Cannot verify the affected Pokemon summary.");
        }
        read_summary_screen(env, context, *info, Language::None, language,
            collection_identity ? &observation : nullptr, nullptr, collection_identity && !verify_judge);
        if (verify_judge){
            env.log("HOME_PLAN verification=judge status=" + observation.value("judge").toObject().value("status").toString().toStdString());
        }
        pbf_press_button(context, BUTTON_B, 80ms, 100ms);
        context.wait_for_all_requests();
        BoxViewWatcher ordinary(&env.console.overlay());
        if (wait_until(env.console, context, Seconds(5), {ordinary}) != 0){
            throw UserSetupError(env.console, "Cannot confirm unheld box after summary verification.");
        }
        const auto observed = read_home_slot_cursor_stable(env, context);
        if (!observed) throw UserSetupError(env.console, "Cannot locate the cursor after reading summary.");
        cursor = BoxCursor{destination.box, (*observed)[0], (*observed)[1]};
    }
    auto result = single_slot_json(env, info, scratch_path);
    if (info && collection_identity){
        auto record = result.toObject(); record.insert("collection_observation", observation); result = record;
    }
    return result;
}

void sort(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    std::vector<std::optional<CollectedPokemonInfo>> boxes_data,
    std::vector<std::optional<CollectedPokemonInfo>> boxes_sorted,
    BoxSorter_Descriptor::Stats& stats,
    BoxCursor& cur_cursor,
    Milliseconds GAME_DELAY,
    BoxNavigationTiming& adaptive_timing
){
    env.log("Start sorting...");
    env.log_to_ui("Start Sorting...");

    std::ostringstream ss;
    // this need to be separated into functions when I will redo the whole thing but I just wanted it to work

    // going thru the sorted list one by one and for each one go through the current pokemon layout to find the closest possible match to fill the slot
    for (size_t poke_nb_s = 0; poke_nb_s < boxes_sorted.size(); poke_nb_s++){
        if (boxes_sorted[poke_nb_s] == std::nullopt){ // we've hit the end of the sorted list.
            break;
        }
        for (size_t poke_nb = poke_nb_s; poke_nb < boxes_data.size(); poke_nb++){
            BoxCursor cursor_s(poke_nb_s);
            BoxCursor cursor(poke_nb);

            // ss << "Comparing " << boxes_data[poke_nb] << " at " << cursor << " to " << boxes_sorted[poke_nb_s] << " at " << cursor_s;
            // env.console.log(ss.str());
            // ss.str("");

            //check for a match and also check if the pokemon is not already in the slot
            stats.compare++;
            env.update_stats();
            if(boxes_sorted[poke_nb_s] == boxes_data[poke_nb] && poke_nb_s == poke_nb){ // Same spot no need to move.
                break;
            }
            if(boxes_sorted[poke_nb_s] == boxes_data[poke_nb]){
                ss << "Swapping " << boxes_data[poke_nb] << " at " << cursor << " and " << boxes_sorted[poke_nb_s] << " at " << cursor_s;
                env.console.log(ss.str());
                ss.str("");

                //moving cursor to the pokemon to pick it up
                cur_cursor = move_cursor_to(env, context, cur_cursor, cursor, GAME_DELAY, &adaptive_timing);
                pbf_press_button(context, BUTTON_Y, 80ms, GAME_DELAY + 240ms);

                //moving to destination to place it or swap it
                cur_cursor = move_cursor_to(env, context, cur_cursor, cursor_s, GAME_DELAY, &adaptive_timing, true);
                pbf_press_button(context, BUTTON_Y, 80ms, GAME_DELAY + 240ms);

                context.wait_for_all_requests();

                std::swap(boxes_data[poke_nb_s], boxes_data[poke_nb]);
                stats.swaps++;
                env.update_stats();

                break;
            }
        }
    }
}

void BoxSorter::program(SingleSwitchProgramEnvironment& env, ProControllerContext& context){
    StartProgramChecks::check_performance_class_wired_or_wireless(context);
    const std::string saved_plan_path = SAVED_PLAN;
    const auto saved_plan = saved_plan_path.empty() ? std::optional<SavedHomePlan>{}
        : std::optional<SavedHomePlan>{load_saved_home_plan(env, saved_plan_path)};
    if (saved_plan && saved_plan->collection_execution) plan_error(env, "Run this collection plan through Collection Manager.");
    if (saved_plan) check_home_plan_output_paths(env, saved_plan_path, OUTPUT_FILE);
    const size_t scan_box_count = saved_plan ? saved_plan->box_count : (size_t)BOX_NUMBER;
    BoxViewWatcher unheld(&env.console.overlay());
    if (saved_plan && wait_until(env.console, context, Seconds(4), {unheld}) != 0){
        throw UserSetupError(env.console, "Open an ordinary HOME box with no Pokemon held before checking a saved plan.");
    }


    const std::vector<SortingRule> sort_preferences = SORT_TABLE.preferences();
    if (sort_preferences.empty()){
        throw UserSetupError(env.console, "At least one sorting method selection needs to be made!");
    }

    BoxSorter_Descriptor::Stats& stats = env.current_stats< BoxSorter_Descriptor::Stats>();

    // vector that will store data for each slot
    std::vector<std::optional<CollectedPokemonInfo>> boxes_data;

    BoxCursor cur_cursor{scan_box_count - 1, 0, 0};

    VideoOverlaySet video_overlay_set(env.console);

    BoxCursor dest_cursor;
    BoxCursor nav_cursor = {0, 0, 0};
    BoxNavigationTiming adaptive_timing(GAME_DELAY.get());

    exit_menus(env, context, VIDEO_DELAY.get());
    auto scan_origin = read_home_box_identity(env, context);
    if (saved_plan && scan_origin && scan_origin->second == saved_plan->total_boxes){
        move_home_box_verified(env, context, saved_plan->first_box, adaptive_timing, false);
        scan_origin = read_home_box_identity(env, context);
    }
    if (saved_plan && (!scan_origin || scan_origin->first != saved_plan->first_box || scan_origin->second != saved_plan->total_boxes)){
        throw UserSetupError(env.console, "Saved plan physical range does not match this HOME setup.");
    }
    if (!scan_origin || scan_origin->first + scan_box_count - 1 > scan_origin->second){
        throw UserSetupError(env.console, "Cannot confirm the physical HOME box range to scan.");
    }
    const std::string scan_id = "home-scan-" + std::to_string(
        std::chrono::duration_cast<Milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    env.console.log("HOME_TIMING action=scan_origin physical_first_box=" + std::to_string(scan_origin->first)
        + " box_count=" + std::to_string(scan_box_count) + " total_boxes=" + std::to_string(scan_origin->second)
        + " identity=physical_counter labels_used=no");


    BoxViewWatcher box_view_watcher(&env.console.overlay());
    SummaryScreenWatcher summary_screen_watcher(&env.console.overlay());

    boxes_data = scan_home_range(env, context, scan_box_count, *scan_origin, sort_preferences,
        VIDEO_DELAY.get(), GAME_DELAY.get(), HOME_LANGUAGE, adaptive_timing);

    // copy boxes data to sort
    std::vector<std::optional<CollectedPokemonInfo>> boxes_sorted = boxes_data;

    // sorting copy of boxes_data
    std::sort(boxes_sorted.begin(), boxes_sorted.end());

    env.log("Current boxes data :");
    print_boxes_data(boxes_data, env);
    const std::string json_path_basename = OUTPUT_FILE;
    save_boxes_data_to_json(boxes_data, json_path_basename + ".json");

    env.log("Sorted boxes data :");
    print_boxes_data(boxes_sorted, env);
    save_boxes_data_to_json(boxes_sorted, json_path_basename + "-sorted.json");
    JsonObject scan_context;
    scan_context["version"] = 1;
    scan_context["scan_id"] = scan_id;
    scan_context["physical_start_box"] = scan_origin->first;
    scan_context["physical_end_box"] = scan_origin->first + scan_box_count - 1;
    scan_context["total_boxes"] = scan_origin->second;
    scan_context["box_count"] = scan_box_count;
    scan_context["coordinate_basis"] = "zero_based_relative_to_physical_start_box";
    scan_context["box_identity"] = "physical_counter_not_editable_label";
    scan_context["source_export"] = json_path_basename + ".json";
    scan_context["target_export"] = json_path_basename + "-sorted.json";
    scan_context.dump(json_path_basename + "-scan.json");


    if (saved_plan){
        auto read_slot = [&](size_t index){
            return read_home_saved_plan_slot(env, context, index, cur_cursor, adaptive_timing,
                GAME_DELAY.get(), HOME_LANGUAGE, sort_preferences,
                json_path_basename + "-slot-verification.json", false);
        };
        execute_saved_home_plan(env, context, *saved_plan, saved_plan_path,
            read_slot_export(env, json_path_basename + ".json"), cur_cursor,
            adaptive_timing, GAME_DELAY.get(), (bool)DRY_RUN, (bool)RESUME_SAVED_PLAN,
            (size_t)PLAN_MOVES_PER_RUN, read_slot, [&]{stats.swaps++; env.update_stats();});
    }else if (!DRY_RUN){
        sort(env, context, boxes_data, boxes_sorted, stats, cur_cursor, GAME_DELAY, adaptive_timing);
    }

    send_program_finished_notification(env, NOTIFICATION_PROGRAM_FINISH);

}

#include "PokemonHome_CollectionManager.inc"

}
}
}

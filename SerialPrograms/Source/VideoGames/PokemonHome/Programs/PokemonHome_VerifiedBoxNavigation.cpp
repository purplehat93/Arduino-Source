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
#include "CommonFramework/Exceptions/OperationFailedExceptionWithScreenshot.h"
#include "PokemonHome_BoxNavigation.h"
#include "PokemonHome_NavigationTimingRoutes.h"

namespace PokemonAutomation{ namespace NintendoSwitch{ namespace PokemonHome{
namespace{
#include "PokemonHome_OverviewNavigation.inc"
class HomeBoxIdentityWatcher : public VisualInferenceCallback{
public:
    HomeBoxIdentityWatcher(size_t expected = 0, bool allow_held = false)
        : VisualInferenceCallback("HomeBoxIdentityWatcher"), expected_box(expected), allow_held(allow_held){}
    void make_overlays(VideoOverlaySet& items) const override{ items.add(COLOR_BLUE, {0.082, 0.719, 0.065, 0.040}); }
    bool process_frame(const ImageViewRGB32& frame, WallClock) override{
        BoxViewDetector box;
        ButtonDetector held(COLOR_BLACK, ButtonType::ButtonB, {0.100, 0.956, 0.107, 0.041});
        if (!frame || (!box.detect(frame) && !(allow_held && held.detect(frame)))){ matches = 0; return false; }
        const ImageFloatBox crops[] = {
            {0.082, 0.719, 0.065, 0.040},
            {0.065, 0.719, 0.085, 0.040},
        };
        std::optional<std::pair<size_t, size_t>> parsed;
        for (const auto& region : crops){
            const auto crop = extract_box_reference(frame, region);
            for (size_t scale : {4, 6}){
                const auto enlarged = crop.copy().scale_to(crop.width() * scale, crop.height() * scale);
                raw_ocr = OCR::tesseract_ocr_read(Language::English, enlarged, OCR::PageSegMode::SINGLE_LINE);
                parsed = parse_home_box_counter(raw_ocr);
                if (parsed) break;
                raw_ocr = OCR::tesseract_ocr_read(Language::English, enlarged, OCR::PageSegMode::SINGLE_BLOCK);
                parsed = parse_home_box_counter(raw_ocr);
                if (parsed) break;
            }
            if (parsed) break;
        }
        if (!parsed){ matches = 0; return false; }
        const auto next = *parsed;
        if (expected_box && next.first != expected_box){ matches = 0; return false; }
        matches = next == identity ? matches + 1 : 1;
        identity = next;
        return matches >= 2;
    }
    std::string raw_ocr;
    size_t expected_box;
    bool allow_held;
    std::pair<size_t, size_t> identity{};
    size_t matches = 0;
};
}
std::optional<std::array<size_t, 2>> read_home_slot_cursor_stable(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context
){
    const auto started = std::chrono::steady_clock::now();
    BoxSlotCursorWatcher watcher;
    const bool confirmed = wait_until(env.console, context, Seconds(3), {watcher}) == 0;
    const auto position = watcher.position();
    env.console.log("HOME_SCAN action=cursor_confirm frames=" + std::to_string(watcher.frames())
        + " rejected_frames=" + std::to_string(watcher.rejected())
        + " row=" + std::to_string(position.row) + " column=" + std::to_string(position.column)
        + " total_ms=" + std::to_string(std::chrono::duration_cast<Milliseconds>(std::chrono::steady_clock::now() - started).count())
        + " result=" + (confirmed ? "confirmed" : "unreadable"));
    if (!confirmed) return std::nullopt;
    return std::array<size_t, 2>{size_t(position.row), size_t(position.column)};
}
std::optional<std::pair<size_t, size_t>> read_home_box_identity(SingleSwitchProgramEnvironment& env, ProControllerContext& context, bool allow_held){
    HomeBoxIdentityWatcher watcher(0, allow_held);
    if (wait_until(env.console, context, Seconds(4), {watcher}) != 0){
        env.console.log("HOME_TIMING action=box_identity result=unreadable counter_ocr=" + watcher.raw_ocr.substr(0, 20));
        return std::nullopt;
    }
    return watcher.identity;
}
void move_home_box_verified(SingleSwitchProgramEnvironment& env, ProControllerContext& context,
    size_t target_box, BoxNavigationTiming& timing, bool via_overview){
    const auto initial = read_home_box_identity(env, context);
    if (!initial || target_box < 1 || target_box > initial->second || (via_overview && initial->second != 200)){
        OperationFailedExceptionWithScreenshot::fire(ErrorReportMode::SEND_ERROR_REPORT,
            "HOME box identity or requested range is unreadable. No box navigation was sent.", env.console);
    }
    const auto started = std::chrono::steady_clock::now();
    for (size_t attempt = 0; attempt < 2; ++attempt){
        bool inputs_ok = true;
        if (via_overview){
            BoxSpacesDetector overview;
            if (!overview.detect(env.console.video().snapshot())){
                inputs_ok = ensure_box_cursor_at_first_slot(env, context, timing.grid_delay)
                    && return_to_overview(env, context, 960ms);
            }
            inputs_ok = inputs_ok && navigate_overview_to_box(env, context, overview, (int)target_box,
                timing.page_delay, timing.grid_delay, "production", attempt + 1);
            // Never press A after a failed selection check.
            inputs_ok = inputs_ok && enter_selected_box(env, context);
        }else{
            const auto current = read_home_box_identity(env, context);
            if (!current) inputs_ok = false;
            else if (current->second != initial->second) inputs_ok = false;
            else{
                const bool right = current->first < target_box;
                const size_t count = right ? target_box - current->first : current->first - target_box;
                for (size_t i = 0; i < count; ++i) pbf_press_button(context, right ? BUTTON_R : BUTTON_L, 80ms, timing.box_delay);
                context.wait_for_all_requests();
            }
        }
        std::optional<std::pair<size_t, size_t>> actual;
        if (inputs_ok){
            HomeBoxIdentityWatcher expected(target_box);
            if (wait_until(env.console, context, Seconds(4), {expected}) == 0) actual = expected.identity;
            else actual = read_home_box_identity(env, context);
        }
        const bool confirmed = actual && actual->first == target_box && actual->second == initial->second;
        env.console.log("HOME_TIMING action=verified_box_route route=" + std::string(via_overview ? "overview" : "linear")
            + " attempt=" + std::to_string(attempt + 1) + " start_box=" + std::to_string(initial->first)
            + " expected_box=" + std::to_string(target_box) + " actual_box=" + (actual ? std::to_string(actual->first) : "unreadable")
            + " total_ms=" + std::to_string(std::chrono::duration_cast<Milliseconds>(std::chrono::steady_clock::now() - started).count())
            + " result=" + (confirmed ? "confirmed" : "mismatch_or_unreadable"));
        if (confirmed && ensure_box_cursor_at_first_slot(env, context, timing.grid_delay)) return;
        if (attempt == 0){
            // Retry only from an independently readable state.
            BoxSpacesDetector overview;
            const bool readable = actual.has_value() || (via_overview && overview.detect(env.console.video().snapshot())
                && read_overview_selection(env, context).valid());
            if (!readable) break;
            timing.box_delay = std::max(576ms, timing.box_delay + 240ms);
            timing.page_delay += 480ms;
            timing.grid_delay += 120ms;
            env.console.log("HOME_TIMING action=verified_box_route backoff=yes box_gap_ms=" + std::to_string(timing.box_delay.count())
                + " page_gap_ms=" + std::to_string(timing.page_delay.count()) + " grid_gap_ms=" + std::to_string(timing.grid_delay.count()));
        }
    }
    OperationFailedExceptionWithScreenshot::fire(ErrorReportMode::SEND_ERROR_REPORT,
        "HOME cross-box destination was not confirmed. Stopped before the next Pokemon interaction.", env.console);
}
void move_home_box_held_overview_verified(SingleSwitchProgramEnvironment& env,
    ProControllerContext& context, size_t target_box, BoxNavigationTiming& timing,
    size_t current_row, size_t current_column){
    const auto started = std::chrono::steady_clock::now();
    const auto origin = read_home_box_identity(env, context, true);
    auto fail = [&](){
        OperationFailedExceptionWithScreenshot::fire(ErrorReportMode::SEND_ERROR_REPORT,
            "Held Box Spaces route was not confirmed. Stopped without placing the Pokemon.", env.console);
    };
    if (!origin || origin->second != 200 || target_box < 1 || target_box > 200
        || !confirm_home_held_cursor(env, context, origin->first, current_row, current_column)) fail();
    const auto gap = std::max(480ms, timing.grid_delay);
    // Held movement never wraps out of the slot grid while finding slot 0.
    while (current_row){
        pbf_press_dpad(context, DPAD_UP, 80ms, gap);
        context.wait_for_all_requests();
        --current_row;
        if (!confirm_home_held_cursor(env, context, origin->first, current_row, current_column)) fail();
    }
    while (current_column){
        pbf_press_dpad(context, DPAD_LEFT, 80ms, gap);
        context.wait_for_all_requests();
        --current_column;
        if (!confirm_home_held_cursor(env, context, origin->first, current_row, current_column)) fail();
    }
    if (!return_to_overview(env, context, 960ms)) fail();
    ButtonDetector cancel(COLOR_BLACK, ButtonType::ButtonB, {0.100, 0.956, 0.107, 0.041});
    BoxSpacesDetector overview;
    if (!cancel.detect(env.console.video().snapshot())
        || !navigate_overview_to_box(env, context, overview, (int)target_box,
            timing.page_delay, timing.grid_delay, "held_production", 1)) fail();
    // A enters the selected box. It is sent only after both its physical
    // number and the still-held overview state have been confirmed.
    const auto selected = read_overview_selection(env, context);
    if (!selected.valid() || selected.box_number != (int)target_box
        || !cancel.detect(env.console.video().snapshot())) fail();
    pbf_press_button(context, BUTTON_A, 80ms, 480ms);
    context.wait_for_all_requests();
    HomeBoxIdentityWatcher destination(target_box, true);
    if (wait_until(env.console, context, Seconds(5), {destination}) != 0
        || destination.identity.second != origin->second
        || !confirm_home_held_cursor(env, context, target_box, 0, 0)) fail();
    env.log("HOME_TIMING action=verified_box_route route=overview held_mode=pokemon"
        " start_box=" + std::to_string(origin->first) + " expected_box=" + std::to_string(target_box)
        + " actual_box=" + std::to_string(destination.identity.first)
        + " total_ms=" + std::to_string(std::chrono::duration_cast<Milliseconds>(std::chrono::steady_clock::now() - started).count())
        + " result=confirmed");
}

}}}

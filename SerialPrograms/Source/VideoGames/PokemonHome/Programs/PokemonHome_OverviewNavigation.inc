// Shared private helpers for production navigation and diagnostics.
// The centered Box Space pill reads "Box Space 3 / 7". This crop isolates
// only the current page digit so unrelated screen changes cannot affect it.
const ImageFloatBox PAGE_NUMBER_REGION{0.279, 0.108, 0.010, 0.031};
constexpr int FIRST_PAGE = 1;
constexpr int LAST_PAGE = 7;
constexpr Milliseconds SAFE_DELAY = 480ms;

// Box Spaces is a 6 by 5 grid. The orange pointer above the selected box is
// position-dependent and remains visible regardless of whether a box is empty.
constexpr size_t GRID_COLUMNS = 6;
constexpr size_t GRID_ROWS = 5;

struct GridPosition{
    int row = -1;
    int column = -1;
    bool valid() const{ return row >= 0 && column >= 0; }
};

ImageFloatBox grid_marker_region(size_t row, size_t column){
    const double center_x = 0.073 + 0.072 * column;
    const double marker_top = 0.150 + 0.105 * row;
    return {center_x - 0.012, marker_top, 0.024, 0.032};
}

GridPosition read_grid_position(const ImageViewRGB32& screen){
    if (!screen || screen.width() < 640 || screen.height() < 360){
        return {};
    }
    BoxSpacesDetector overview;
    if (!overview.detect(screen)) return {};
    const double brightness = home_ui_brightness_scale(screen);
    ButtonDetector cancel(COLOR_BLACK, ButtonType::ButtonB, {0.100, 0.956, 0.107, 0.041});
    if (cancel.detect(screen)){
        GridPosition held;
        for (size_t row = 0; row < GRID_ROWS; ++row){
            for (size_t column = 0; column < GRID_COLUMNS; ++column){
                const double x = 0.073 + 0.072 * column, y = 0.150 + 0.105 * row;
                const auto marker = extract_box_reference(screen, ImageFloatBox{x - 0.014, y + 0.003, 0.028, 0.047});
                size_t black = 0, white = 0;
                filter_rgb32_range(black, marker, uint32_t(0), home_ui_scaled_color(combine_rgb(65,65,65), brightness), Color(0), true);
                filter_rgb32_range(white, marker, home_ui_scaled_color(combine_rgb(210,210,210), brightness), uint32_t(0xffffffff), Color(0), true);
                const size_t pixels = marker.width() * marker.height();
                if (pixels && black * 100 > pixels * 7 && white * 100 > pixels * 45
                    && (black + white) * 100 > pixels * 72){
                    if (held.valid()) return {}; // Never guess between hands.
                    held = {(int)row, (int)column};
                }
            }
        }
        return held;
    }

    size_t best_count = 0;
    GridPosition best;
    bool tied = false;
    for (size_t row = 0; row < GRID_ROWS; row++){
        for (size_t column = 0; column < GRID_COLUMNS; column++){
            const ImageViewRGB32 crop = extract_box_reference(screen, grid_marker_region(row, column));
            size_t count = 0;
            filter_rgb32_range(
                count, crop,
                home_ui_scaled_color(combine_rgb(205, 45, 0), brightness, brightness < 0.8 ? -8 : 0), home_ui_scaled_color(combine_rgb(255, 175, 25), brightness, brightness < 0.8 ? 8 : 0), Color(0), true
            );
            if (count > best_count){
                best_count = count;
                best = {(int)row, (int)column};
                tied = false;
            }else if (count == best_count && count != 0){
                tied = true;
            }
        }
    }
    // Reject ambiguous candidates and weak orange matches (about 8% of the
    // marker crop) rather than logging an unverified cursor position.
    if (best_count < 30 || tied){
        return {};
    }
    return best;
}

class BoxSpacesGridPositionWatcher : public VisualInferenceCallback{
public:
    explicit BoxSpacesGridPositionWatcher(GridPosition expected = {})
        : VisualInferenceCallback("BoxSpacesGridPositionWatcher")
        , m_expected(expected)
    {}

    virtual void make_overlays(VideoOverlaySet& items) const override{
        for (size_t row = 0; row < GRID_ROWS; row++){
            for (size_t column = 0; column < GRID_COLUMNS; column++){
                items.add(COLOR_BLUE, grid_marker_region(row, column));
            }
        }
    }

    virtual bool process_frame(const ImageViewRGB32& frame, WallClock) override{
        m_position = read_grid_position(frame);
        if (!m_position.valid()){
            m_matches = 0;
            return false;
        }
        if (m_expected.valid() && (m_position.row != m_expected.row || m_position.column != m_expected.column)){
            m_matches = 0;
            return false;
        }
        m_matches++;
        return m_matches >= 2;
    }

    GridPosition position() const{ return m_position; }

private:
    GridPosition m_expected;
    GridPosition m_position;
    size_t m_matches = 0;
};

std::string grid_position_string(GridPosition position){
    if (!position.valid()) return "unreadable";
    return "r" + std::to_string(position.row + 1) + "c" + std::to_string(position.column + 1);
}

const char* dpad_direction_name(DpadPosition direction){
    switch (direction){
    case DPAD_UP: return "up";
    case DPAD_DOWN: return "down";
    case DPAD_LEFT: return "left";
    case DPAD_RIGHT: return "right";
    default: return "other";
    }
}

bool grid_position_matches(GridPosition actual, GridPosition expected){
    return actual.valid() && actual.row == expected.row && actual.column == expected.column;
}

const ImageFloatBox& box_slot_cursor_region(size_t row, size_t column){
    static std::array<std::array<ImageFloatBox, 6>, 5> regions = []{
        std::array<std::array<ImageFloatBox, 6>, 5> ret;
        for (size_t r = 0; r < ret.size(); r++){
            for (size_t c = 0; c < ret[r].size(); c++){
                ret[r][c] = {0.07 + 0.072 * c, 0.15 + 0.105 * r, 0.01, 0.01};
            }
        }
        return ret;
    }();
    return regions[row][column];
}

GridPosition read_box_slot_cursor(const ImageViewRGB32& screen){
    const auto position = read_box_slot_cursor_position(screen);
    return position ? GridPosition{(int)(*position)[0], (int)(*position)[1]} : GridPosition{};
}

class BoxSlotCursorWatcher : public VisualInferenceCallback{
public:
    explicit BoxSlotCursorWatcher(GridPosition expected = {})
        : VisualInferenceCallback("BoxSlotCursorWatcher")
        , m_expected(expected)
    {}
    virtual void make_overlays(VideoOverlaySet& items) const override{
        for (size_t row = 0; row < GRID_ROWS; row++){
            for (size_t column = 0; column < GRID_COLUMNS; column++){
                items.add(COLOR_BLUE, box_slot_cursor_region(row, column));
            }
        }
    }
    virtual bool process_frame(const ImageViewRGB32& frame, WallClock) override{
        const auto previous = m_position;
        m_position = read_box_slot_cursor(frame);
        ++m_frames;
        if (!m_position.valid() || (m_expected.valid() && !grid_position_matches(m_position, m_expected))){
            ++m_rejected;
            m_matches = 0;
            return false;
        }
        m_matches = grid_position_matches(previous, m_position) ? m_matches + 1 : 1;
        return m_matches >= 3;
    }
    GridPosition position() const{ return m_position; }
    size_t frames() const{ return m_frames; }
    size_t rejected() const{ return m_rejected; }
private:
    GridPosition m_expected;
    GridPosition m_position;
    size_t m_matches = 0;
    size_t m_frames = 0;
    size_t m_rejected = 0;
};

bool send_box_slot_step(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    DpadPosition direction,
    GridPosition before,
    GridPosition expected,
    Milliseconds delay,
    size_t repetition
){
    const auto started = std::chrono::steady_clock::now();
    pbf_press_dpad(context, direction, 80ms, delay);
    context.wait_for_all_requests();
    const auto queue_finished = std::chrono::steady_clock::now();
    BoxSlotCursorWatcher watcher(expected);
    const int result = wait_until(env.console, context, Seconds(5), {watcher});
    const auto finished = std::chrono::steady_clock::now();
    const GridPosition after = watcher.position();
    const auto queue_ms = std::chrono::duration_cast<Milliseconds>(queue_finished - started).count();
    const auto total_ms = std::chrono::duration_cast<Milliseconds>(finished - started).count();
    std::ostringstream line;
    line << "HOME_TIMING action=box_slot_step phase=cursor_setup trial=" << repetition
         << " direction=" << dpad_direction_name(direction)
         << " position_before=" << grid_position_string(before)
         << " expected_position=" << grid_position_string(expected)
         << " position_after=" << grid_position_string(after)
         << " configured_game_delay_ms=" << delay.count()
         << " queue_ms=" << queue_ms
         << " detection_ms=" << std::max<int64_t>(0, total_ms - queue_ms)
         << " total_ms=" << total_ms
         << " result=" << (result == 0 && grid_position_matches(after, expected) ? "confirmed" : "cursor_timeout_or_unreadable");
    env.console.log(line.str());
    env.log_to_ui(line.str());
    return result == 0 && grid_position_matches(after, expected);
}

bool send_grid_step(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    DpadPosition direction,
    GridPosition before,
    GridPosition expected,
    Milliseconds delay,
    const char* phase,
    size_t trial
){
    const auto started = std::chrono::steady_clock::now();
    pbf_press_dpad(context, direction, 80ms, delay);
    context.wait_for_all_requests();
    const auto queue_finished = std::chrono::steady_clock::now();
    BoxSpacesGridPositionWatcher watcher(expected);
    const int watcher_result = wait_until(env.console, context, Seconds(5), {watcher});
    const auto confirmed_at = std::chrono::steady_clock::now();
    const GridPosition after = watcher.position();
    const auto queue_ms = std::chrono::duration_cast<Milliseconds>(queue_finished - started).count();
    const auto total_ms = std::chrono::duration_cast<Milliseconds>(confirmed_at - started).count();
    std::ostringstream line;
    line << "HOME_TIMING action=overview_grid_step"
         << " phase=" << phase << " trial=" << trial
         << " direction=" << dpad_direction_name(direction)
         << " position_before=" << grid_position_string(before)
         << " expected_position=" << grid_position_string(expected)
         << " position_after=" << grid_position_string(after)
         << " configured_delay_ms=" << delay.count()
         << " queue_ms=" << queue_ms
         << " detection_ms=" << std::max<int64_t>(0, total_ms - queue_ms)
         << " total_ms=" << total_ms
         << " result=" << (watcher_result == 0 && grid_position_matches(after, expected) ? "confirmed" : "position_timeout_or_unreadable");
    env.console.log(line.str());
    env.log_to_ui(line.str());
    return watcher_result == 0 && grid_position_matches(after, expected);
}

bool move_grid_to(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    GridPosition& current,
    GridPosition target,
    Milliseconds delay,
    const char* phase,
    int cells = 30
){
    const auto path = NavigationTiming::grid_path(current.row * 6 + current.column, target.row * 6 + target.column, cells);
    for (int cell : path){
        GridPosition next{cell / 6, cell % 6};
        DpadPosition direction = next.row > current.row ? DPAD_DOWN
            : next.row < current.row ? DPAD_UP
            : next.column > current.column ? DPAD_RIGHT : DPAD_LEFT;
        if (!send_grid_step(env, context, direction, current, next, delay, phase, 0)) return false;
        current = next;
    }
    return grid_position_matches(current, target);
}

int read_box_spaces_page_number(const ImageViewRGB32& screen, std::string& raw_ocr){
    const ImageViewRGB32 crop = extract_box_reference(screen, PAGE_NUMBER_REGION);
    const ImageRGB32 enlarged = crop.copy().scale_to(crop.width() * 4, crop.height() * 4);
    raw_ocr = OCR::tesseract_ocr_read(Language::English, enlarged, OCR::PageSegMode::SINGLE_CHAR);
    for (char ch : raw_ocr){
        if (ch >= '1' && ch <= '7'){
            return ch - '0';
        }
    }
    // In HOME's narrow page-counter font, Tesseract can mistake page 1 for
    // a closing bracket. This crop contains only the page digit.
    if (raw_ocr.find(']') != std::string::npos || raw_ocr.find('|') != std::string::npos){
        return 1;
    }
    return -1;
}

class BoxSpacesPageNumberWatcher : public VisualInferenceCallback{
public:
    explicit BoxSpacesPageNumberWatcher(int expected_page)
        : VisualInferenceCallback("BoxSpacesPageNumberWatcher")
        , m_expected_page(expected_page)
    {}

    virtual void make_overlays(VideoOverlaySet& items) const override{
        items.add(COLOR_BLUE, PAGE_NUMBER_REGION);
    }

    virtual bool process_frame(const ImageViewRGB32& frame, WallClock) override{
        m_page_number = read_box_spaces_page_number(frame, m_raw_ocr);
        if (m_expected_page == 0){
            if (m_page_number >= FIRST_PAGE && m_page_number <= LAST_PAGE){
                if (m_page_number == m_previous_page){
                    m_consecutive_matches++;
                }else{
                    m_previous_page = m_page_number;
                    m_consecutive_matches = 1;
                }
                return m_consecutive_matches >= 2;
            }
            m_previous_page = -1;
            m_consecutive_matches = 0;
            return false;
        }
        if (m_page_number == m_expected_page){
            m_consecutive_matches++;
            return m_consecutive_matches >= 2;
        }
        m_consecutive_matches = 0;
        return false;
    }

    int page_number() const{ return m_page_number; }
    const std::string& raw_ocr() const{ return m_raw_ocr; }

private:
    int m_expected_page;
    int m_page_number = -1;
    size_t m_consecutive_matches = 0;
    int m_previous_page = -1;
    std::string m_raw_ocr;
};

struct PageTurnResult{
    bool confirmed = false;
    int page_number = -1;
    int64_t queue_ms = 0;
    int64_t settle_ms = 0;
    int64_t total_ms = 0;
    bool overview_visible = false;
};

PageTurnResult send_single_turn(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    BoxSpacesDetector& box_spaces_detector,
    Button button,
    const char* direction,
    int page_before,
    int expected_page,
    Milliseconds page_delay,
    const std::string& phase,
    size_t trial
){
    const auto started = std::chrono::steady_clock::now();
    pbf_press_button(context, button, 80ms, page_delay);
    context.wait_for_all_requests();
    const auto queue_finished = std::chrono::steady_clock::now();

    BoxSpacesPageNumberWatcher page_watcher(expected_page);
    const int watcher_result = wait_until(env.console, context, Seconds(10), {page_watcher});
    const auto confirmed_at = std::chrono::steady_clock::now();
    const VideoSnapshot observed = env.console.video().snapshot();

    const int page_after = page_watcher.page_number();
    const bool overview_visible = box_spaces_detector.detect(observed);
    const bool confirmed = watcher_result == 0 && page_after == expected_page;
    const auto queue_ms = std::chrono::duration_cast<Milliseconds>(queue_finished - started).count();
    const auto total_ms = std::chrono::duration_cast<Milliseconds>(confirmed_at - started).count();
    const auto settle_ms = std::max<int64_t>(0, total_ms - queue_ms);

    std::ostringstream line;
    line << "HOME_TIMING action=overview_page_turn"
         << " phase=" << phase
         << " trial=" << trial
         << " direction=" << direction
         << " page_before=" << page_before
         << " expected_page=" << expected_page
         << " page_after=" << page_after
         << " page_ocr=" << (page_watcher.raw_ocr().empty() ? "empty" : page_watcher.raw_ocr())
         << " configured_delay_ms=" << page_delay.count()
         << " queue_ms=" << queue_ms
         << " settle_ms=" << settle_ms
         << " total_ms=" << total_ms
         << " overview=" << (overview_visible ? "confirmed" : "not_detected")
         << " result=" << (confirmed ? "confirmed" : watcher_result != 0 ? "page_timeout" : page_after < 0 ? "page_number_unreadable" : "unexpected_page_number");
    env.console.log(line.str());
    env.log_to_ui(line.str());

    return {confirmed, page_after, queue_ms, settle_ms, total_ms, overview_visible};
}

struct OverviewSelection{
    int page = -1;
    GridPosition position;
    int box_number = -1;
    bool valid() const{ return page >= FIRST_PAGE && page <= LAST_PAGE && position.valid() && box_number > 0; }
};

bool overview_header_focused(const ImageViewRGB32& screen){
    size_t count = 0;
    const auto crop = extract_box_reference(screen, ImageFloatBox{0.11, 0.102, 0.014, 0.028});
    const double brightness = home_ui_brightness_scale(screen);
    filter_rgb32_range(count, crop, home_ui_scaled_color(combine_rgb(205, 45, 0), brightness, brightness < 0.8 ? -8 : 0), home_ui_scaled_color(combine_rgb(255, 175, 25), brightness, brightness < 0.8 ? 8 : 0), Color(0), true);
    return count > crop.width() * crop.height() / 5;
}

OverviewSelection read_overview_selection(SingleSwitchProgramEnvironment& env, ProControllerContext& context){
    BoxSpacesDetector overview;
    const VideoSnapshot screen = env.console.video().snapshot();
    if (!overview.detect(screen)) return {};
    if (overview_header_focused(screen)){
        // A page header has no selected grid cell. Enter the grid explicitly.
        pbf_press_dpad(context, DPAD_DOWN, 80ms, 480ms);
        context.wait_for_all_requests();
        env.console.log("HOME_TIMING action=overview_focus header_to_grid=yes");
    }
    BoxSpacesGridPositionWatcher grid_watcher;
    if (wait_until(env.console, context, Seconds(3), {grid_watcher}) != 0) return {};
    const GridPosition position = grid_watcher.position();
    // First-row labels in the middle columns obscure the page counter. Read
    // it from the second row, then restore and confirm the same selected cell.
    const bool label_over_counter = position.row == 0 && position.column >= 2 && position.column <= 4;
    if (label_over_counter){
        if (!send_grid_step(env, context, DPAD_DOWN, position, {1, position.column}, 480ms, "page_read_probe", 0)) return {};
    }
    BoxSpacesPageNumberWatcher page_watcher(0);
    const bool page_read = wait_until(env.console, context, Seconds(3), {page_watcher}) == 0;
    if (label_over_counter){
        if (!send_grid_step(env, context, DPAD_UP, {1, position.column}, position, 480ms, "page_read_restore", 0)) return {};
    }
    if (!page_read) return {};
    const int box_number = (page_watcher.page_number() - 1) * 30 + position.row * 6 + position.column + 1;
    if (box_number > 200) return {};
    return {page_watcher.page_number(), position, box_number};
}

bool navigate_overview_to_box(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    BoxSpacesDetector& detector,
    int target_box,
    Milliseconds page_delay,
    Milliseconds grid_delay,
    const char* phase,
    size_t trial
){
    if (target_box < 1 || target_box > 200) return false;
    OverviewSelection current = read_overview_selection(env, context);
    if (!current.valid()) return false;
    const int target_page = (target_box - 1) / (int)(GRID_ROWS * GRID_COLUMNS) + 1;
    while (current.page != target_page){
        const bool right = current.page < target_page;
        const int expected = current.page + (right ? 1 : -1);
        PageTurnResult moved = send_single_turn(
            env, context, detector, right ? BUTTON_R : BUTTON_L,
            right ? "right" : "left", current.page, expected,
            page_delay, phase, trial
        );
        if (!moved.confirmed) return false;
        current = read_overview_selection(env, context);
        if (!current.valid()) return false;
    }

    const int within_page = (target_box - 1) % (int)(GRID_ROWS * GRID_COLUMNS);
    const GridPosition target{within_page / (int)GRID_COLUMNS, within_page % (int)GRID_COLUMNS};
    if (!move_grid_to(env, context, current.position, target, grid_delay, phase, current.page == 7 ? 20 : 30)) return false;
    current = read_overview_selection(env, context);
    const bool confirmed = current.valid() && current.box_number == target_box;
    env.console.log(
        std::string("HOME_TIMING action=overview_box_select phase=") + phase
        + " trial=" + std::to_string(trial)
        + " expected_box=" + std::to_string(target_box)
        + " actual_box=" + (current.valid() ? std::to_string(current.box_number) : "unreadable")
        + " result=" + (confirmed ? "confirmed" : "wrong_or_unreadable_box")
    );
    return confirmed;
}

bool move_box_slot_to(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    GridPosition& current,
    GridPosition target,
    Milliseconds delay,
    size_t trial
){
    while (current.row > target.row){
        GridPosition next{current.row - 1, current.column};
        if (!send_box_slot_step(env, context, DPAD_UP, current, next, delay, trial)) return false;
        current = next;
    }
    while (current.row < target.row){
        GridPosition next{current.row + 1, current.column};
        if (!send_box_slot_step(env, context, DPAD_DOWN, current, next, delay, trial)) return false;
        current = next;
    }
    while (current.column > target.column){
        GridPosition next{current.row, current.column - 1};
        if (!send_box_slot_step(env, context, DPAD_LEFT, current, next, delay, trial)) return false;
        current = next;
    }
    while (current.column < target.column){
        GridPosition next{current.row, current.column + 1};
        if (!send_box_slot_step(env, context, DPAD_RIGHT, current, next, delay, trial)) return false;
        current = next;
    }
    return true;
}

bool wait_for_box_view(SingleSwitchProgramEnvironment& env, ProControllerContext& context){
    BoxViewWatcher watcher(&env.console.overlay());
    return wait_until(env.console, context, Seconds(8), {watcher}) == 0;
}

bool enter_selected_box(SingleSwitchProgramEnvironment& env, ProControllerContext& context){
    const auto started = std::chrono::steady_clock::now();
    pbf_press_button(context, BUTTON_A, 80ms, 480ms);
    context.wait_for_all_requests();
    const auto queue_finished = std::chrono::steady_clock::now();
    const bool confirmed = wait_for_box_view(env, context);
    const auto finished = std::chrono::steady_clock::now();
    std::ostringstream line;
    line << "HOME_TIMING action=enter_selected_box queue_ms="
         << std::chrono::duration_cast<Milliseconds>(queue_finished - started).count()
         << " confirmation_ms=" << std::chrono::duration_cast<Milliseconds>(finished - queue_finished).count()
         << " result=" << (confirmed ? "box_view_confirmed" : "box_view_timeout");
    env.console.log(line.str());
    env.log_to_ui(line.str());
    return confirmed;
}

class BoxSpacesControlWatcher : public VisualInferenceCallback{
public:
    BoxSpacesControlWatcher() : VisualInferenceCallback("BoxSpacesControlWatcher"){}
    virtual void make_overlays(VideoOverlaySet& items) const override{
        items.add(COLOR_BLUE, {0.235, 0.72, 0.09, 0.04});
    }
    virtual bool process_frame(const ImageViewRGB32& frame, WallClock) override{
        const auto crop = extract_box_reference(frame, ImageFloatBox{0.235, 0.72, 0.09, 0.04});
        size_t count = 0;
        filter_rgb32_range(count, crop, combine_rgb(230, 140, 0), combine_rgb(255, 220, 80), Color(0), true);
        if (count <= crop.width() * crop.height() / 3){ m_matches = 0; return false; }
        return ++m_matches >= 2;
    }
private:
    size_t m_matches = 0;
};

bool return_to_overview(
    SingleSwitchProgramEnvironment& env, ProControllerContext& context, Milliseconds delay
){
    // From the first slot, two Up inputs select the footer control; A opens
    // it. Confirm the highlighted control before A so we cannot rename a box
    // or interact with a Pokemon after a missed navigation input.
    pbf_press_dpad(context, DPAD_UP, 80ms, delay);
    pbf_press_dpad(context, DPAD_UP, 80ms, delay);
    context.wait_for_all_requests();
    BoxSpacesControlWatcher control;
    if (wait_until(env.console, context, Seconds(3), {control}) != 0){
        env.console.log("HOME_TIMING action=return_to_overview result=footer_control_not_confirmed");
        return false;
    }
    pbf_press_button(context, BUTTON_A, 80ms, 480ms);
    context.wait_for_all_requests();
    BoxSpacesWatcher overview(&env.console.overlay());
    if (wait_until(env.console, context, Seconds(5), {overview}) != 0) return false;
    const auto selected = read_overview_selection(env, context);
    env.console.log("HOME_TIMING action=return_to_overview selected_box="
        + std::to_string(selected.box_number) + " result=" + (selected.valid() ? "confirmed" : "unreadable"));
    return selected.valid();
}

bool ensure_box_cursor_at_first_slot(
    SingleSwitchProgramEnvironment& env,
    ProControllerContext& context,
    Milliseconds delay
){
    BoxSlotCursorWatcher watcher;
    if (wait_until(env.console, context, Seconds(5), {watcher}) != 0 || !watcher.position().valid()){
        return false;
    }
    GridPosition position = watcher.position();
    return move_box_slot_to(env, context, position, {0, 0}, delay, 0);
}

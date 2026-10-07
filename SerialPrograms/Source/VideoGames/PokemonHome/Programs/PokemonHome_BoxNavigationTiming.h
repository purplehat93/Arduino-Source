/* Per-run policy for visually confirmed HOME slot navigation. */
#ifndef PokemonAutomation_PokemonHome_BoxNavigationTiming_H
#define PokemonAutomation_PokemonHome_BoxNavigationTiming_H
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
namespace PokemonAutomation{ namespace NintendoSwitch{ namespace PokemonHome{
inline std::optional<std::pair<size_t, size_t>> parse_home_box_counter(const std::string& text){
    size_t current = 0, total = 0;
    bool slash = false, first_digit = false, second_digit = false;
    // The HOME counter crop can include a thin vertical edge of the adjacent
    // Box Spaces icon. Tesseract reports that edge as `|`; tolerate it only at
    // the outside of the OCR result, while keeping the counter itself strict.
    size_t begin = 0, end = text.size();
    auto outer_space = [](char ch){ return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t'; };
    while (begin < end && (outer_space(text[begin]) || text[begin] == '|')) ++begin;
    while (end > begin && (outer_space(text[end - 1]) || text[end - 1] == '|')) --end;
    for (size_t i = begin; i < end; ++i){
        const char ch = text[i];
        if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
        if (ch == '/' && !slash && first_digit){ slash = true; continue; }
        if (ch < '0' || ch > '9') return std::nullopt;
        size_t& value = slash ? total : current;
        value = value * 10 + (size_t)(ch - '0');
        if (value > 300) return std::nullopt;
        (slash ? second_digit : first_digit) = true;
    }
    if (!slash || !second_digit || !current || current > total) return std::nullopt;
    return std::pair<size_t, size_t>{current, total};
}

struct BoxNavigationTiming{
    using Delay = std::chrono::milliseconds;
    explicit BoxNavigationTiming(Delay initial_delay)
        : box_delay(std::max(initial_delay + Delay(240), Delay(576)))
        , local_cursor_delay(std::max(initial_delay, Delay(160)))
    {}
    void confirmed_local_move(Delay extra_settle){
        ++confirmed_local_moves;
        if (extra_settle > Delay(0)){
            clean_moves = 0;
            // Never reduce a user-configured long delay during backoff.
            local_cursor_delay += extra_settle + Delay(20);
        }else if (++clean_moves >= 8){
            clean_moves = 0;
            local_cursor_delay = std::max(Delay(160), local_cursor_delay - Delay(10));
        }
    }
    Delay box_delay;
    Delay page_delay{1800};
    Delay grid_delay{480};
    Delay local_cursor_delay;
    size_t confirmed_local_moves = 0;
    size_t clean_moves = 0;
};
// Keep short routes linear; include the cost of resetting a held slot cursor.
inline bool home_overview_route_is_faster(size_t from, size_t to, size_t total,
    const BoxNavigationTiming& timing, size_t held_row = 0, size_t held_column = 0){
    if (total != 200 || from < 1 || to < 1 || from > total || to > total
        || held_row >= 5 || held_column >= 6) return false;
    auto distance = [](size_t a, size_t b){ return a > b ? a - b : b - a; };
    const size_t a = (from - 1) % 30, z = (to - 1) % 30;
    const auto overview = std::chrono::milliseconds(6500)
        + distance((from - 1) / 30, (to - 1) / 30) * (timing.page_delay + std::chrono::milliseconds(80))
        + (distance(a / 6, z / 6) + distance(a % 6, z % 6)) * (timing.grid_delay + std::chrono::milliseconds(80))
        + (held_row + held_column) * (std::max(timing.grid_delay, std::chrono::milliseconds(480)) + std::chrono::milliseconds(80));
    return overview < distance(from, to) * (timing.box_delay + std::chrono::milliseconds(80));
}

}}}
#endif

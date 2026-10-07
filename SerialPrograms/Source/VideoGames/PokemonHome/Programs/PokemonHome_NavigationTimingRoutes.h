/* Deterministic coverage plan for the unheld HOME navigation diagnostic. */
#ifndef PokemonAutomation_PokemonHome_NavigationTimingRoutes_H
#define PokemonAutomation_PokemonHome_NavigationTimingRoutes_H

#include <array>
#include <vector>

namespace PokemonAutomation{ namespace NintendoSwitch{ namespace PokemonHome{ namespace NavigationTiming{

struct BoxRoute{ int from; int to; };
struct GridRoute{ int from; int to; }; // zero-based row-major cells

inline std::vector<int> grid_path(int from, int to, int cells = 30){
    std::vector<int> path;
    if (from < 0 || to < 0 || from >= cells || to >= cells) return path;
    int row = from / 6, column = from % 6;
    const int target_row = to / 6, target_column = to % 6;
    auto rows = [&](){
        while (row != target_row){
            row += row < target_row ? 1 : -1;
            path.push_back(row * 6 + column);
        }
    };
    auto columns = [&](){
        while (column != target_column){
            column += column < target_column ? 1 : -1;
            path.push_back(row * 6 + column);
        }
    };
    // Page 7 contains 20 boxes: its fourth row has only two cells.
    // Move into a valid column before descending into that partial row.
    if (target_row * 6 + column >= cells){ columns(); rows(); }
    else{ rows(); columns(); }
    return path;
}

inline std::vector<int> overview_targets(unsigned round){
    constexpr std::array<int, 7> pages{1, 4, 2, 7, 3, 6, 5};
    std::vector<int> targets;
    for (unsigned i = 0; i < pages.size(); i++){
        const int page = pages[(i + round) % pages.size()];
        const int cells = page == 7 ? 20 : 30;
        // Vary corners and interior cells, including the partial final page.
        const std::array<int, 3> offsets{0, cells / 2, cells - 1};
        for (unsigned j = 0; j < offsets.size(); j++){
            targets.push_back((page - 1) * 30 + offsets[(j + round + i) % offsets.size()] + 1);
        }
    }
    return targets;
}

inline std::vector<BoxRoute> box_routes(unsigned round, bool calibration = false){
    std::vector<BoxRoute> routes;
    constexpr std::array<int, 5> distances{1, 5, 10, 20, 29};
    for (unsigned i = 0; i < distances.size(); i++){
        const int distance = distances[i];
        if (calibration && distance != 5 && distance != 20) continue;
        const int right_start = 1 + (round * 41 + i * 37) % (200 - distance);
        const int left_start = 200 - (round * 43 + i * 31) % (200 - distance);
        routes.push_back({right_start, right_start + distance});
        routes.push_back({left_start, left_start - distance});
    }
    if (!calibration){
        // Cross 30-box page boundaries and exercise both ends of the range.
        routes.push_back({29, 31});
        routes.push_back({182, 179});
        routes.push_back({199, 200});
        routes.push_back({2, 1});
    }
    return routes;
}

inline std::vector<GridRoute> grid_routes(unsigned round){
    const int row = round % 5;
    const int column = (round * 2 + 1) % 6;
    return {{row * 6, row * 6 + 5}, {row * 6 + 5, row * 6},
        {column, 24 + column}, {24 + column, column}};
}

inline std::vector<int> slot_targets(unsigned round){
    std::vector<int> targets;
    // All 30 cells. Reverse each pass to exercise opposite directions.
    for (int row = 0; row < 5; row++){
        for (int i = 0; i < 6; i++){
            const int column = row % 2 == 0 ? i : 5 - i;
            targets.push_back(row * 6 + column);
        }
    }
    if (round % 2){
        return std::vector<int>(targets.rbegin(), targets.rend());
    }
    return targets;
}

inline std::vector<int> candidates(int conservative, bool page){
    const std::vector<int> gaps = page
        ? std::vector<int>{1800, 1400, 1100, 900, 700, 480, 320, 160, 80}
        : std::vector<int>{960, 720, 560, 480, 400, 320, 240, 160, 120, 80};
    std::vector<int> ret{conservative};
    for (int gap : gaps){
        if (gap < ret.back()) ret.push_back(gap);
    }
    return ret;
}

}}}}
#endif

/* Route-coverage checks; compile with g++ -std=c++17 -I SerialPrograms/Source. */
#include <cassert>
#include <algorithm>
#include <cstdlib>
#include <set>
#include <iostream>
#include "VideoGames/PokemonHome/Programs/PokemonHome_NavigationTimingRoutes.h"
using namespace PokemonAutomation::NintendoSwitch::PokemonHome::NavigationTiming;
int main(){
    for (int cells : {20,30}) for (int from = 0; from < cells; from++) for (int to = 0; to < cells; to++){
        auto path = grid_path(from,to,cells);
        int previous = from;
        for (int next : path){
            assert(next >= 0 && next < cells);
            int dr = std::abs(next / 6 - previous / 6), dc = std::abs(next % 6 - previous % 6);
            assert(dr + dc == 1);
            previous = next;
        }
        assert(previous == to);
        assert(path.size() == (unsigned)(std::abs(to / 6 - from / 6) + std::abs(to % 6 - from % 6)));
    }
    std::set<int> box_start_pages;
    for (unsigned round = 0; round < 3; round++){
        std::set<int> pages;
        for (int box : overview_targets(round)){
            assert(box >= 1 && box <= 200);
            pages.insert((box - 1) / 30 + 1);
        }
        assert(pages.size() == 7);
        auto slots = slot_targets(round);
        assert(slots.size() == 30);
        std::set<int> unique(slots.begin(), slots.end());
        assert(unique.size() == 30 && *unique.begin() == 0 && *unique.rbegin() == 29);
        std::set<int> right, left;
        for (BoxRoute route : box_routes(round)){
            assert(route.from >= 1 && route.from <= 200);
            assert(route.to >= 1 && route.to <= 200 && route.to != route.from);
            box_start_pages.insert((route.from - 1) / 30 + 1);
            (route.to > route.from ? right : left).insert(std::abs(route.to - route.from));
        }
        for (int distance : {1,5,10,20,29}) assert(right.count(distance) && left.count(distance));
        std::set<int> directions;
        for (GridRoute route : grid_routes(round)){
            assert(route.from >= 0 && route.from < 30 && route.to >= 0 && route.to < 30);
            int dr = route.to / 6 - route.from / 6, dc = route.to % 6 - route.from % 6;
            assert((dr == 0) != (dc == 0));
            directions.insert(dr > 0 ? 1 : dr < 0 ? 2 : dc > 0 ? 3 : 4);
        }
        assert(directions.size() == 4);
    }
    assert(box_start_pages.size() == 7);
    for (bool page : {false,true}) for (int baseline : {480,960,1800,2000}){
        auto gaps = candidates(baseline,page);
        assert(gaps.front() == baseline && gaps.back() == 80);
        for (unsigned i = 1; i < gaps.size(); i++) assert(gaps[i] < gaps[i-1]);
    }
    std::cout << "PASS: all seven pages, 30 unique slots, both directions at all five box distances, bounded routes, all 1300 full/partial-grid paths and decreasing candidates\n";
}

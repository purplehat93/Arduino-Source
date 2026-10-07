#include <cassert>
#include <iostream>
#include "VideoGames/PokemonHome/Programs/PokemonHome_BoxNavigationTiming.h"
using PokemonAutomation::NintendoSwitch::PokemonHome::BoxNavigationTiming;
using namespace std::chrono_literals;
int main(){
    using PokemonAutomation::NintendoSwitch::PokemonHome::parse_home_box_counter;
    for (const auto& text : {"31/200", " 31 / 200\n", "1/200", "200/200", "| 77/200", "| 77/200 |"}) assert(parse_home_box_counter(text));
    const auto identity = parse_home_box_counter("31/200");
    assert(identity->first == 31 && identity->second == 200);
    for (const auto& text : {"", "0/200", "201/200", "31/0", "31/301", "31", "31/", "/200", "31//200", "31/2O0", "-31/200", "x|31/200", "31/200x", "999999999999999999999/200"}) assert(!parse_home_box_counter(text));

    BoxNavigationTiming timing(240ms);
    for (int i = 0; i < 7; ++i) timing.confirmed_local_move(0ms);
    assert(timing.local_cursor_delay == 240ms);
    timing.confirmed_local_move(0ms);
    assert(timing.local_cursor_delay == 230ms);
    timing.confirmed_local_move(100ms);
    assert(timing.local_cursor_delay == 350ms && timing.clean_moves == 0);
    for (int i = 0; i < 400; ++i) timing.confirmed_local_move(0ms);
    assert(timing.local_cursor_delay == 160ms);
    BoxNavigationTiming fast(50ms);
    assert(fast.local_cursor_delay == 160ms);
    BoxNavigationTiming slow(2000ms);
    slow.confirmed_local_move(50ms);
    assert(slow.local_cursor_delay == 2070ms);
    using PokemonAutomation::NintendoSwitch::PokemonHome::home_overview_route_is_faster;
    BoxNavigationTiming routes(240ms);
    assert(!home_overview_route_is_faster(82, 86, 200, routes));
    assert(home_overview_route_is_faster(82, 152, 200, routes));
    assert(home_overview_route_is_faster(152, 82, 200, routes, 4, 5));
    assert(!home_overview_route_is_faster(82, 82, 200, routes));
    assert(!home_overview_route_is_faster(1, 70, 300, routes));
    assert(!home_overview_route_is_faster(0, 70, 200, routes));
    assert(!home_overview_route_is_faster(1, 201, 200, routes));
    assert(!home_overview_route_is_faster(82, 152, 200, routes, 5, 0));
    assert(!home_overview_route_is_faster(82, 152, 200, routes, 0, 6));
    routes.page_delay = 100000ms;
    assert(!home_overview_route_is_faster(82, 152, 200, routes));
    std::cout << "PASS: cursor adaptation, strict box counters, short/long held route selection, unsupported ranges, and slow-overview fallback\n";
}

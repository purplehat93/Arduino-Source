#include <cassert>
#include <iostream>
#include "VideoGames/PokemonHome/Programs/PokemonHome_CollectionObservationPolicy.h"
using namespace PokemonAutomation::NintendoSwitch::PokemonHome;
int main(){
    assert(parse_home_judge_cycle_hint("Y Base Points") == HomeJudgeCycleState::NUMERIC);
    assert(parse_home_judge_cycle_hint("Y Judge") == HomeJudgeCycleState::BASE_POINTS);
    assert(parse_home_judge_cycle_hint("Y View Stats") == HomeJudgeCycleState::JUDGE);
    assert(parse_home_judge_cycle_hint("Y Condition") == HomeJudgeCycleState::JUDGE);
    assert(parse_home_judge_cycle_hint("Y View Stats", "Condition") == HomeJudgeCycleState::CONDITION);
    assert(parse_home_judge_cycle_hint("") == HomeJudgeCycleState::UNKNOWN);
    assert(parse_home_judge_cycle_hint("Effort Levels") == HomeJudgeCycleState::UNKNOWN);
    assert(parse_home_judge_rating("No good\n").exact() == 0);
    assert(parse_home_judge_rating("Fantastic").exact() == 30);
    assert(parse_home_judge_rating("BEST").exact() == 31);
    for (const auto* raw : {"Decent", "Pretty good", "Very good", "Hyper trained!", "8est", "31", "Not Best", "Best?"}){
        assert(!parse_home_judge_rating(raw).exact());
    }
    const auto hyper = parse_home_judge_rating("Hyper trained!");
    assert(hyper.hyper_trained && !hyper.minimum && !hyper.maximum);
    assert(parse_home_judge_rating("Decent").minimum == 1);
    assert(parse_home_judge_rating("Decent").maximum == 15);
    std::cout << "PASS: exact ratings, intervals, hyper training and rejected OCR\n";
}

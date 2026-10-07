#ifndef PokemonAutomation_PokemonHome_CollectionObservationPolicy_H
#define PokemonAutomation_PokemonHome_CollectionObservationPolicy_H
#include <optional>
#include <string>
namespace PokemonAutomation{ namespace NintendoSwitch{ namespace PokemonHome{
enum class HomeJudgeCycleState{UNKNOWN, NUMERIC, BASE_POINTS, JUDGE, CONDITION};
inline HomeJudgeCycleState parse_home_judge_cycle_hint(const std::string& raw, const std::string& title = ""){
    if (raw.find("Base Points") != std::string::npos) return HomeJudgeCycleState::NUMERIC;
    if (raw.find("View Stats") != std::string::npos) return title.find("Condition") != std::string::npos
        ? HomeJudgeCycleState::CONDITION : HomeJudgeCycleState::JUDGE;
    if (raw.find("Condition") != std::string::npos) return HomeJudgeCycleState::JUDGE;
    if (raw.find("Judge") != std::string::npos) return HomeJudgeCycleState::BASE_POINTS;
    return HomeJudgeCycleState::UNKNOWN;
}
struct HomeJudgeRating{
    std::string label;
    std::optional<int> minimum, maximum;
    bool hyper_trained = false;
    std::optional<int> exact() const{
        return minimum && minimum == maximum ? minimum : std::nullopt;
    }
};
inline HomeJudgeRating parse_home_judge_rating(const std::string& raw){
    std::string key;
    for (unsigned char ch : raw){
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        if (ch >= 'a' && ch <= 'z') key += char(ch);
        else if (ch != ' ' && ch != '\r' && ch != '\n' && ch != '\t' && ch != '!') return {};
    }
    if (key == "nogood") return {"No Good", 0, 0, false};
    if (key == "decent") return {"Decent", 1, 15, false};
    if (key == "prettygood") return {"Pretty Good", 16, 25, false};
    if (key == "verygood") return {"Very Good", 26, 29, false};
    if (key == "fantastic") return {"Fantastic", 30, 30, false};
    if (key == "best") return {"Best", 31, 31, false};
    if (key == "hypertrained") return {"Hyper trained!", {}, {}, true};
    return {};
}
}}}
#endif

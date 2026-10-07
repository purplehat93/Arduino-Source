#include <cassert>
#include "../../Source/VideoGames/PokemonHome/Programs/PokemonHome_CollectionFormPolicy.h"
using namespace PokemonAutomation::NintendoSwitch::PokemonHome;
int main(){
    std::vector<HomeReferenceForm> forms{
        {"typhlosion", "typhlosion", "", 157, {"fire"}},
        {"typhlosion-hisui", "typhlosion", "", 157, {"fire", "ghost"}},
        {"venusaur-male", "venusaur", "Male", 3, {"grass", "poison"}},
        {"venusaur-female", "venusaur", "Female", 3, {"grass", "poison"}},
        {"pichu", "pichu", "", 172, {"electric"}},
        {"pichu-spiky-eared", "pichu", "", 172, {"electric"}}
    };
    assert(match_home_reference_form(forms, 157, "typhlosion", {"ghost", "fire"}, "Male", false).key == "typhlosion-hisui");
    assert(match_home_reference_form(forms, 3, "venusaur", {"grass", "poison"}, "Female", false).key == "venusaur-female");
    assert(match_home_reference_form(forms, 3, "venusaur", {"grass", "poison"}, "unknown", false).key.empty());
    assert(match_home_reference_form(forms, 3, "bulbasaur", {"grass", "poison"}, "Male", false).key.empty());
    assert(match_home_reference_form(forms, 172, "pichu", {"electric"}, "Male", false).key.empty());
    forms[5].home_available = -1;
    assert(match_home_reference_form(forms, 172, "pichu", {"electric"}, "Male", false).key == "pichu");
    assert(match_home_reference_form(forms, 172, "pichu", {"electric"}, "Female", false).key == "pichu");
    assert(match_home_reference_form(forms, 157, "typhlosion", {"fire"}, "Male", true).key.empty());
    assert(match_home_reference_form(forms, 9999, "missing", {"fire"}, "Male", false).key.empty());
    forms.push_back({"unclassified", "typhlosion", "", 157, {}});
    assert(match_home_reference_form(forms, 157, "typhlosion", {"fire"}, "Male", false).key.empty());
    forms.push_back({"rockruff", "rockruff", "", 744, {"rock"}, 1, {"keen-eye", "vital-spirit", "steadfast"}});
    forms.push_back({"rockruff-own-tempo", "rockruff", "", 744, {"rock"}, 1, {"own-tempo"}});
    assert(match_home_reference_form(forms, 744, "rockruff", {"rock"}, "Male", false, "own-tempo").key == "rockruff-own-tempo");
    assert(match_home_reference_form(forms, 744, "rockruff", {"rock"}, "Female", false, "keen-eye").key == "rockruff");
    assert(match_home_reference_form(forms, 744, "rockruff", {"rock"}, "Male", false).reason == "ability_unreadable");
    assert(match_home_reference_form(forms, 744, "rockruff", {"rock"}, "Male", false, "Own Tempo").key == "rockruff-own-tempo");
    forms.back().abilities.clear();
    assert(match_home_reference_form(forms, 744, "rockruff", {"rock"}, "Male", false, "own-tempo").reason == "ability_rules_incomplete");
}

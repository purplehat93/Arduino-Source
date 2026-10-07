#ifndef PokemonAutomation_PokemonHome_CollectionFormPolicy_H
#define PokemonAutomation_PokemonHome_CollectionFormPolicy_H
#include <algorithm>
#include <string>
#include <vector>
namespace PokemonAutomation{ namespace NintendoSwitch{ namespace PokemonHome{
struct HomeReferenceForm{
    std::string key, species, gender;
    int dex = 0;
    std::vector<std::string> types;
    int home_available = 0; // unknown = 0, reviewed yes = 1, reviewed no = -1
    std::vector<std::string> abilities = {};
    std::string visual_normal = {}, visual_shiny = {};
};
struct HomeFormMatch{
    std::string key, reason;
    std::vector<std::string> candidates;
    bool ability_used = false;
};
inline std::string home_canonical_species_name(const std::string& name){
    if (name == "farfetch-d") return "farfetchd";
    if (name == "sirfetch-d") return "sirfetchd";
    return name;
}
inline std::string home_canonical_ability_name(std::string name){
    for (char& ch : name){
        if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
        else if (ch == ' ') ch = '-';
    }
    return name;
}
inline HomeFormMatch match_home_reference_form(const std::vector<HomeReferenceForm>& forms,
    int dex, const std::string& species, std::vector<std::string> types,
    const std::string& gender, bool gmax, const std::string& ability = ""){
    HomeFormMatch result;
    const auto canonical_ability = home_canonical_ability_name(ability);
    if (gmax){result.reason = "gigantamax_factor_unknown_or_present"; return result;}
    if (types.empty() || types[0] == "none"){
        result.reason = "observed_types_unreadable"; return result;
    }
    std::sort(types.begin(), types.end());
    bool covered = false;
    bool ability_required = false, ability_rules_complete = true;
    for (const auto& form : forms){
        if (form.dex != dex || form.home_available == -1) continue;
        covered = true;
        ability_required |= !form.abilities.empty();
        ability_rules_complete &= !form.abilities.empty();
        if (form.types.empty()){
            result.reason = "reference_coverage_needs_review"; return result;
        }
        if (home_canonical_species_name(form.species) != home_canonical_species_name(species)){
            result.reason = "species_name_and_dex_disagree"; return result;
        }
        auto expected = form.types;
        std::sort(expected.begin(), expected.end());
        if (expected == types && (form.gender.empty() || form.gender == gender)
            && (form.abilities.empty() || ability.empty()
                || std::find(form.abilities.begin(), form.abilities.end(), canonical_ability) != form.abilities.end())){
            result.candidates.push_back(form.key);
        }
    }
    if (ability_required && !ability_rules_complete){
        result.candidates.clear(); result.reason = "ability_rules_incomplete"; return result;
    }
    if (ability_required && ability.empty()){
        result.reason = "ability_unreadable"; return result;
    }
    result.ability_used = ability_required;
    if (!covered) result.reason = "reference_coverage_needs_review";
    else if (result.candidates.size() == 1) result.key = result.candidates[0];
    else result.reason = result.candidates.empty() ? "no_matching_reference_form" : "ambiguous_visible_fields";
    return result;
}
}}}
#endif

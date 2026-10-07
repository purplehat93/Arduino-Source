"""Exercise native/Python policy agreement across the entire pinned reference."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from reference_catalog import build_reference, match_form


class ReferenceParityTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ required for native parity")
    def test_all_reference_observations_and_ability_failures(self):
        catalog = build_reference()
        header = Path(__file__).resolve().parents[2] / "Source/VideoGames/PokemonHome/Programs/PokemonHome_CollectionFormPolicy.h"
        source = '#include ' + json.dumps(str(header)) + r'''
#include <iostream>
#include <iomanip>
using namespace PokemonAutomation::NintendoSwitch::PokemonHome;
int main(){
    size_t count, length; std::cin >> count;
    std::vector<HomeReferenceForm> forms(count);
    for (auto& form : forms){
        std::cin >> std::quoted(form.key) >> std::quoted(form.species)
                 >> std::quoted(form.gender) >> form.dex >> form.home_available >> length;
        form.types.resize(length);
        for (auto& type : form.types) std::cin >> std::quoted(type);
        std::cin >> length; form.abilities.resize(length);
        for (auto& ability : form.abilities) std::cin >> std::quoted(ability);
    }
    std::cin >> count;
    for (size_t i = 0; i < count; i++){
        int dex; bool gmax; std::string species, gender, ability;
        std::cin >> dex >> std::quoted(species) >> std::quoted(gender) >> gmax
                 >> std::quoted(ability) >> length;
        std::vector<std::string> types(length);
        for (auto& type : types) std::cin >> std::quoted(type);
        const auto match = match_home_reference_form(forms, dex, species, types, gender, gmax, ability);
        std::cout << match.key << '|' << match.reason << '|';
        for (size_t j = 0; j < match.candidates.size(); j++){
            if (j) std::cout << ',';
            std::cout << match.candidates[j];
        }
        std::cout << '\n';
    }
}
'''
        quoted = lambda value: json.dumps(value or "")
        lines = [str(len(catalog["forms"]))]
        cases = []
        for form in catalog["forms"]:
            types = form.get("types") or []
            abilities = form.get("abilities") or []
            available = 1 if form.get("home_available") is True else -1 if form.get("home_available") is False else 0
            lines.append(" ".join([quoted(form["key"]), quoted(form["species"]), quoted(form["gender"]),
                                   str(form["dex"]), str(available), str(len(types)),
                                   *map(quoted, types), str(len(abilities)), *map(quoted, abilities)]))
            for gender in ([form["gender"]] if form["gender"] else ["Male", "Female"]):
                for ability in ([None, "not-an-ability", *abilities, *[a.replace("-", " ").title() for a in abilities]] if abilities else [None]):
                    cases.append({"dex": form["dex"], "name": form["species"], "gender": gender,
                                  "gmax": False, "ability": ability,
                                  "primary_type": types[0] if types else "none",
                                  "secondary_type": types[1] if len(types) > 1 else "none"})
        cases.extend([{**case, "name": {"farfetchd": "farfetch-d", "sirfetchd": "sirfetch-d"}[case["name"]]}
                      for case in cases.copy() if case["name"] in ("farfetchd", "sirfetchd")])
        lines.append(str(len(cases)))
        for case in cases:
            types = [case["primary_type"]] + ([case["secondary_type"]] if case["secondary_type"] != "none" else [])
            lines.append(" ".join([str(case["dex"]), quoted(case["name"]), quoted(case["gender"]), "0",
                                   quoted(case["ability"]), str(len(types)), *map(quoted, types)]))
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "parity.cpp"
            binary = Path(directory) / "parity"
            cpp.write_text(source)
            subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(binary)], check=True, capture_output=True)
            output = subprocess.run([str(binary)], input="\n".join(lines) + "\n", text=True,
                                    capture_output=True, check=True).stdout.splitlines()
        self.assertEqual(len(output), len(cases))
        for case, native in zip(cases, output):
            match = match_form(case, catalog)
            expected = "|".join([match["key"] or "", match.get("reason", ""),
                                 ",".join(match.get("candidates", []))])
            self.assertEqual(native, expected, str(case))


if __name__ == "__main__":
    unittest.main()

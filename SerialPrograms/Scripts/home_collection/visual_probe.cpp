// Offline harness for the exact pixel policy used by the native HOME scanner.
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include "../../Source/VideoGames/PokemonHome/Programs/PokemonHome_CollectionVisualPolicy.h"
#include "../../Source/VideoGames/PokemonHome/Programs/PokemonHome_CollectionVisualColorExperiment.h"
using namespace PokemonAutomation::NintendoSwitch::PokemonHome;
struct Image{
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> pixels;
    explicit Image(const std::string& path){
        std::ifstream stream(path, std::ios::binary);
        stream.read(reinterpret_cast<char*>(&width), 4);
        stream.read(reinterpret_cast<char*>(&height), 4);
        if (!width || !height || width > 10000 || height > 10000) throw std::runtime_error("Invalid image blob");
        pixels.resize(size_t(width) * height);
        stream.read(reinterpret_cast<char*>(pixels.data()), std::streamsize(pixels.size() * 4));
        if (!stream) throw std::runtime_error("Incomplete image blob");
    }
    HomeVisualImage view() const{ return {pixels.data(), width, height, width}; }
};
int main(int argc, char** argv){
    try{
        if (argc != 2 && (argc != 3 || std::string(argv[2]) != "--color-experiment"))
            throw std::runtime_error("Expected image blob path and optional --color-experiment");
        const Image image(argv[1]);
        size_t count; std::cin >> count;
        for (size_t i = 0; i < count; i++){
            size_t row, column, size; std::cin >> row >> column >> size;
            std::vector<HomeVisualTemplate> templates;
            for (size_t j = 0; j < size; j++){
                std::string key, path; std::cin >> std::quoted(key) >> std::quoted(path);
                const Image reference(path);
                if (reference.width != 64 || reference.height != 64) throw std::runtime_error("Expected 64x64 sprite");
                templates.push_back({key, home_visual_template(reference.view())});
            }
            const auto match = argc == 3
                ? match_home_visual_color_experiment(image.view(), row, column, templates)
                : match_home_visual_icon(image.view(), row, column, templates);
            std::cout << "{\"key\":" << std::quoted(match.key) << ",\"reason\":" << std::quoted(match.reason)
                      << ",\"margin\":" << match.margin << ",\"scores\":[";
            for (size_t j = 0; j < match.scores.size(); j++){
                if (j) std::cout << ',';
                const auto& score = match.scores[j];
                std::cout << "{\"key\":" << std::quoted(score.key) << ",\"distance\":" << score.distance
                          << ",\"overlap\":" << score.overlap << '}';
            }
            std::cout << "]}\n";
        }
    }catch (const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}

// Context-menu geometry regression. Optional raw captures: width/height uint32,
// followed by packed ARGB32 pixels. OCR and live summary entry are separate checks.
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "VideoGames/PokemonHome/Programs/PokemonHome_CollectionMenuPolicy.h"
using namespace PokemonAutomation::NintendoSwitch::PokemonHome;
static void require(bool ok){if (!ok) throw std::runtime_error("Menu geometry regression failed");}
int main(int argc, char** argv){
    constexpr size_t w = 1000, h = 600;
    for (double top : {.19, .38, .55}){
        for (uint32_t orange : {0xffffb900u, 0xff806000u}){
            std::vector<uint32_t> pixels(w * h, 0xff65c9b8u);
            for (size_t y = size_t(h * top); y < size_t(h * (top + .07)); ++y){
                for (size_t x = 350; x < 525; ++x) pixels[y * w + x] = orange;
            }
            // A wider name underline above the menu cannot replace the button.
            for (size_t y = size_t(h * (top - .05)); y < size_t(h * (top - .04)); ++y){
                for (size_t x = 340; x < 535; ++x) pixels[y * w + x] = orange;
            }
            auto hit = home_summary_menu_highlight(pixels.data(), w, h, w);
            require(hit && std::abs(hit->left - .35) < .003
                && std::abs(hit->center_y - (top + .035)) < .005);
        }
    }
    // A valid shaded/rounded button may have its widest solid runs only at
    // the top. Its complete band, including narrower lower rows, fixes height.
    std::vector<uint32_t> shaded(w * h, 0xff65c9b8u);
    for (size_t y = 120; y < 162; ++y){
        const size_t left = y < 132 ? 350 : 362;
        const size_t right = y < 132 ? 525 : 513;
        for (size_t x = left; x < right; ++x) shaded[y*w+x] = 0xffffb900u;
    }
    auto shaded_hit = home_summary_menu_highlight(shaded.data(), w, h, w);
    require(shaded_hit && std::abs(shaded_hit->center_y - .235) < .005);
    std::vector<uint32_t> ordinary(w * h, 0xff65c9b8u);
    // Sprite-sized orange patches cannot stand in for the menu button.
    for (size_t y = 180; y < 240; ++y) for (size_t x = 350; x < 420; ++x) ordinary[y*w+x] = 0xffffb900u;
    require(!home_summary_menu_highlight(ordinary.data(), w, h, w));
    auto underline = ordinary;
    for (size_t y = 150; y < 156; ++y) for (size_t x = 350; x < 545; ++x) underline[y*w+x] = 0xffffb900u;
    require(!home_summary_menu_highlight(underline.data(), w, h, w));
    auto ambiguous = ordinary;
    for (size_t top : {120, 300}){
        for (size_t y = top; y < top + 42; ++y) for (size_t x = 500; x < 675; ++x) ambiguous[y*w+x] = 0xffffb900u;
    }
    require(!home_summary_menu_highlight(ambiguous.data(), w, h, w));
    require(!home_summary_menu_highlight(nullptr, w, h, w));
    require(!home_summary_menu_highlight(ordinary.data(), w, h, w-1));
    for (int i = 1; i < argc; ++i){
        std::ifstream file(argv[i], std::ios::binary);
        uint32_t width = 0, height = 0;
        file.read(reinterpret_cast<char*>(&width), 4); file.read(reinterpret_cast<char*>(&height), 4);
        require(file && width && height && width <= 4096 && height <= 4096);
        std::vector<uint32_t> pixels(size_t(width) * height);
        file.read(reinterpret_cast<char*>(pixels.data()), pixels.size() * 4);require(bool(file));
        auto hit = home_summary_menu_highlight(pixels.data(), width, height, width);require(bool(hit));
        std::cout << argv[i] << ' ' << hit->left << ' ' << hit->width << ' ' << hit->center_y << '\n';
    }
    std::cout << "PASS: upper/lower menu positions, dimmed capture, ordinary sprite rejection, invalid image guards\n";
}

#ifndef PokemonAutomation_PokemonHome_CollectionVisualPolicy_H
#define PokemonAutomation_PokemonHome_CollectionVisualPolicy_H
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace PokemonAutomation{ namespace NintendoSwitch{ namespace PokemonHome{
// Shared by the native scanner and the offline pixel harness. Scores are
// distances, not probabilities. Only unreviewed evidence candidates result.
constexpr double HOME_VISUAL_MAX_DISTANCE = 0.14;
constexpr double HOME_VISUAL_MIN_MARGIN = 0.035;
constexpr double HOME_VISUAL_MIN_OVERLAP = 0.45;
constexpr size_t HOME_VISUAL_SIDE = 64;
using HomeVisualPixels = std::array<std::array<float, 3>, HOME_VISUAL_SIDE * HOME_VISUAL_SIDE>;
struct HomeVisualImage{
    const uint32_t* pixels = nullptr;
    size_t width = 0, height = 0, stride = 0;
    uint32_t pixel(size_t x, size_t y) const{ return pixels[y * stride + x]; }
};
struct HomeVisualTemplate{
    std::string key;
    HomeVisualPixels pixels{};
};
struct HomeVisualScore{
    std::string key;
    double distance = std::numeric_limits<double>::infinity();
    double overlap = 0;
};
struct HomeVisualMatch{
    std::string key, reason;
    double margin = 0;
    std::vector<HomeVisualScore> scores;
};
inline void home_visual_clean(std::array<float, 3>& rgb){
    // HOME's pale grid and cyan icon halo are background, not Pokemon color.
    if (rgb[0] > 185 && rgb[1] > 210 && rgb[2] > 180) rgb = {255, 255, 255};
}
inline HomeVisualPixels home_visual_template(const HomeVisualImage& image){
    HomeVisualPixels result{};
    for (size_t y = 0; y < HOME_VISUAL_SIDE; y++){
        for (size_t x = 0; x < HOME_VISUAL_SIDE; x++){
            const uint32_t color = image.pixel(x, y);
            const float alpha = float(color >> 24) / 255;
            auto& rgb = result[y * HOME_VISUAL_SIDE + x];
            for (size_t channel = 0; channel < 3; channel++){
                rgb[channel] = alpha * float((color >> (16 - 8 * channel)) & 255) + (1 - alpha) * 255;
            }
            home_visual_clean(rgb);
        }
    }
    return result;
}
inline bool home_visual_foreground(const std::array<float, 3>& rgb){
    return std::min({rgb[0], rgb[1], rgb[2]}) < 195;
}
inline std::vector<HomeVisualPixels> home_visual_crops(const HomeVisualImage& image, size_t row, size_t column){
    std::vector<HomeVisualPixels> result;
    if (!image.pixels || image.height < 720 || row >= 5 || column >= 6
        || std::abs(double(image.width) / image.height - 16.0 / 9) > 0.02) return result;
    for (double size_factor : {0.094, 0.100, 0.106}){
        const double side = image.height * size_factor;
        for (double dx : {-0.003, 0.0, 0.003}){
            for (double dy : {-0.003, 0.0, 0.003}){
                const double left = image.width * (0.073 + 0.072 * column) + image.height * dx - side / 2;
                const double top = image.height * (0.226 + 0.105 * row + dy) - side / 2;
                if (left < 0 || top < 0 || left + side >= image.width || top + side >= image.height) continue;
                HomeVisualPixels crop{};
                std::array<float, HOME_VISUAL_SIDE * HOME_VISUAL_SIDE> greens{};
                for (size_t y = 0; y < HOME_VISUAL_SIDE; y++){
                    for (size_t x = 0; x < HOME_VISUAL_SIDE; x++){
                        const double sx = left + (x + 0.5) * side / HOME_VISUAL_SIDE - 0.5;
                        const double sy = top + (y + 0.5) * side / HOME_VISUAL_SIDE - 0.5;
                        const size_t ix = size_t(sx), iy = size_t(sy);
                        const double fx = sx - ix, fy = sy - iy;
                        const uint32_t colors[] = {image.pixel(ix, iy), image.pixel(ix + 1, iy),
                                                  image.pixel(ix, iy + 1), image.pixel(ix + 1, iy + 1)};
                        const double weights[] = {(1-fx)*(1-fy), fx*(1-fy), (1-fx)*fy, fx*fy};
                        auto& rgb = crop[y * HOME_VISUAL_SIDE + x];
                        for (size_t channel = 0; channel < 3; channel++){
                            for (size_t sample = 0; sample < 4; sample++){
                                rgb[channel] += float(weights[sample] * ((colors[sample] >> (16 - 8 * channel)) & 255));
                            }
                        }
                        greens[y * HOME_VISUAL_SIDE + x] = rgb[1];
                    }
                }
                // Adapt to moderate inactivity dimming without making a dark
                // or absent video feed into evidence.
                std::nth_element(greens.begin(), greens.begin() + greens.size() * 9 / 10, greens.end());
                const float brightness = greens[greens.size() * 9 / 10] / 255;
                if (brightness < 0.45) continue;
                size_t foreground = 0;
                for (auto& rgb : crop){
                    for (float& channel : rgb) channel = std::min(255.0f, channel / brightness);
                    home_visual_clean(rgb);
                    foreground += home_visual_foreground(rgb);
                }
                if (foreground >= 48) result.push_back(std::move(crop));
            }
        }
    }
    return result;
}
inline HomeVisualMatch match_home_visual_icon(const HomeVisualImage& image, size_t row, size_t column,
    const std::vector<HomeVisualTemplate>& templates){
    HomeVisualMatch result;
    if (templates.empty()){result.reason = "templates_missing"; return result;}
    const auto crops = home_visual_crops(image, row, column);
    if (crops.empty()){result.reason = "icon_unreadable"; return result;}
    for (const auto& reference : templates){
        HomeVisualScore best{reference.key};
        for (const auto& crop : crops){
            double squared = 0;
            size_t intersection = 0, united = 0;
            for (size_t i = 0; i < crop.size(); i++){
                const bool actual = home_visual_foreground(crop[i]);
                const bool expected = home_visual_foreground(reference.pixels[i]);
                intersection += actual && expected; united += actual || expected;
                for (size_t channel = 0; channel < 3; channel++){
                    const double delta = crop[i][channel] - reference.pixels[i][channel];
                    squared += delta * delta;
                }
            }
            const double distance = std::sqrt(squared / (crop.size() * 3)) / 255;
            if (distance < best.distance){
                best.distance = distance;
                best.overlap = united ? double(intersection) / united : 0;
            }
        }
        result.scores.push_back(best);
    }
    std::sort(result.scores.begin(), result.scores.end(), [](const auto& a, const auto& b){
        return a.distance < b.distance || (a.distance == b.distance && a.key < b.key);
    });
    const auto& best = result.scores.front();
    result.margin = result.scores.size() > 1 ? result.scores[1].distance - best.distance : 1;
    if (result.scores.size() < 2){
        result.reason = "insufficient_alternatives";
    }else if (best.distance > HOME_VISUAL_MAX_DISTANCE || best.overlap < HOME_VISUAL_MIN_OVERLAP){
        result.reason = "icon_score_too_weak";
    }else if (result.margin < HOME_VISUAL_MIN_MARGIN){
        result.reason = "lookalike_margin_too_small";
    }else result.key = best.key;
    return result;
}
inline std::string agree_home_visual_frames(const HomeVisualMatch& first, const HomeVisualMatch& second,
    bool independent_frames){
    return independent_frames && !first.key.empty() && first.key == second.key ? first.key : "";
}
}}}
#endif

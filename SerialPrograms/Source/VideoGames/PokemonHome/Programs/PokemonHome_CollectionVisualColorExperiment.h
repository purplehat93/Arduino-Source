#ifndef PokemonAutomation_PokemonHome_CollectionVisualColorExperiment_H
#define PokemonAutomation_PokemonHome_CollectionVisualColorExperiment_H
#include "PokemonHome_CollectionVisualPolicy.h"

namespace PokemonAutomation{ namespace NintendoSwitch{ namespace PokemonHome{
// Offline experiment only. Not used by Collection Manager or identity import.
// Keep whole-icon shape checks; measure color separation on pixels on which
// the supplied family templates differ, rather than diluting it in background.
inline HomeVisualMatch match_home_visual_color_experiment(const HomeVisualImage& image,
    size_t row, size_t column, const std::vector<HomeVisualTemplate>& templates){
    HomeVisualMatch result;
    if (templates.size() < 2){result.reason = "insufficient_alternatives"; return result;}
    const std::string family = templates.front().key.substr(0, templates.front().key.find('-'));
    if (family != "flabebe" && family != "florges" && family != "burmy"){
        result.reason = "color_family_not_supported"; return result;
    }
    for (const auto& reference : templates){
        if (reference.key.substr(0, reference.key.find('-')) != family){
            result.reason = "mixed_color_families"; return result;
        }
    }
    const auto shape = match_home_visual_icon(image, row, column, templates);
    if (shape.scores.empty()){result.reason = shape.reason; return result;}
    const auto& shape_best = shape.scores.front();
    if (shape_best.distance > HOME_VISUAL_MAX_DISTANCE || shape_best.overlap < HOME_VISUAL_MIN_OVERLAP){
        result.reason = "icon_score_too_weak"; return result;
    }
    std::array<bool, HOME_VISUAL_SIDE * HOME_VISUAL_SIDE> mask{};
    size_t mask_size = 0;
    for (size_t i = 0; i < mask.size(); i++){
        size_t foreground = 0;
        std::array<float, 3> low{255, 255, 255}, high{0, 0, 0};
        for (const auto& reference : templates){
            foreground += home_visual_foreground(reference.pixels[i]);
            for (size_t ch = 0; ch < 3; ch++){
                low[ch] = std::min(low[ch], reference.pixels[i][ch]);
                high[ch] = std::max(high[ch], reference.pixels[i][ch]);
            }
        }
        mask[i] = foreground * 2 >= templates.size()
            && std::max({high[0]-low[0], high[1]-low[1], high[2]-low[2]}) >= 40;
        mask_size += mask[i];
    }
    if (mask_size < 48){result.reason = "color_signal_too_small"; return result;}
    const auto crops = home_visual_crops(image, row, column);
    const auto reference = std::find_if(templates.begin(), templates.end(), [&](const auto& item){
        return item.key == shape_best.key;
    });
    const HomeVisualPixels* aligned = nullptr;
    double best_error = std::numeric_limits<double>::infinity();
    for (const auto& crop : crops){
        double squared = 0;
        for (size_t i = 0; i < crop.size(); i++){
            for (size_t ch = 0; ch < 3; ch++){
                const double delta = crop[i][ch] - reference->pixels[i][ch];
                squared += delta * delta;
            }
        }
        if (squared < best_error){best_error = squared; aligned = &crop;}
    }
    if (!aligned){result.reason = "icon_unreadable"; return result;}
    for (const auto& item : templates){
        double squared = 0;
        for (size_t i = 0; i < mask.size(); i++){
            if (!mask[i]) continue;
            const double actual_peak = std::max({1.0f, (*aligned)[i][0], (*aligned)[i][1], (*aligned)[i][2]});
            const double expected_peak = std::max({1.0f, item.pixels[i][0], item.pixels[i][1], item.pixels[i][2]});
            for (size_t ch = 0; ch < 3; ch++){
                // Chromaticity separates petal hue from shading/brightness.
                const double delta = (*aligned)[i][ch] / actual_peak - item.pixels[i][ch] / expected_peak;
                squared += delta * delta;
            }
        }
        const auto base = std::find_if(shape.scores.begin(), shape.scores.end(), [&](const auto& score){
            return score.key == item.key;
        });
        result.scores.push_back({item.key, std::sqrt(squared / (mask_size * 3)), base->overlap});
    }
    std::sort(result.scores.begin(), result.scores.end(), [](const auto& a, const auto& b){
        return a.distance < b.distance || (a.distance == b.distance && a.key < b.key);
    });
    const auto& best = result.scores.front();
    result.margin = result.scores[1].distance - best.distance;
    if (best.key != shape_best.key){result.reason = "shape_color_disagree";}
    else if (best.distance > 0.20){result.reason = "color_score_too_weak";}
    else if (result.margin < HOME_VISUAL_MIN_MARGIN){result.reason = "color_margin_too_small";}
    else result.key = best.key;
    return result;
}
}}}
#endif

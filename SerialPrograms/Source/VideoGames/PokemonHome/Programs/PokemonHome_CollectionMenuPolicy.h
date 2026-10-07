#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace PokemonAutomation{ namespace NintendoSwitch{ namespace PokemonHome{
struct HomeSummaryMenuHighlight{
    double left, width, center_y;
};
// HOME moves context menus horizontally and vertically with the selected slot.
// Find the long orange button, excluding the small mode marker and sprite art.
inline std::optional<HomeSummaryMenuHighlight> home_summary_menu_highlight(
    const uint32_t* pixels, size_t width, size_t height, size_t stride
){
    if (!pixels || !width || !height || stride < width) return std::nullopt;
    struct Run{size_t left, right, y;};
    std::vector<Run> runs;
    size_t longest = 0;
    const size_t stop = size_t(width * .90);
    for (size_t y = size_t(height * .16); y < size_t(height * .84); y += std::max(size_t(1), height / 540)){
        size_t left = 0;
        bool in_run = false;
        for (size_t x = size_t(width * .05); x <= stop; ++x){
            const uint32_t p = pixels[y * stride + x];
            const unsigned r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            const bool orange = x < stop && r > 45 && r > g * 1.15 && g > b * 1.8 && b < r * .45;
            if (orange && !in_run){left = x; in_run = true;}
            if (!orange && in_run){
                const size_t length = x - left;
                if (length > width * .10 && length < width * .30){
                    longest = std::max(longest, length);
                    runs.push_back({left, x, y});
                }
                in_run = false;
            }
        }
    }
    if (!longest) return std::nullopt;
    // Split separate orange objects into bands before comparing their widths.
    // A long, thin Pokemon-name underline must not win over the menu button.
    std::vector<std::vector<Run>> bands;
    for (const auto& run : runs){
        bool assigned = false;
        for (auto& band : bands){
            const auto& previous = band.back();
            const size_t overlap_left = std::max(previous.left, run.left);
            const size_t overlap_right = std::min(previous.right, run.right);
            if (run.y - previous.y <= height * .025 && overlap_right > overlap_left
                && overlap_right - overlap_left > std::min(previous.right - previous.left, run.right - run.left) * .60){
                band.push_back(run); assigned = true; break;
            }
        }
        if (!assigned) bands.push_back({run});
    }
    std::optional<HomeSummaryMenuHighlight> result;
    for (const auto& band : bands){
        size_t band_longest = 0;
        for (const auto& run : band) band_longest = std::max(band_longest, run.right - run.left);
        // Use the whole orange band for height. Rounded corners, text and
        // capture shading can leave the widest runs only above the label.
        const size_t top = band.front().y, bottom = band.back().y;
        size_t left = width;
        for (const auto& run : band){
            if (run.right - run.left + width * .008 < band_longest) continue;
            left = std::min(left, run.left);
        }
        if (bottom <= top || bottom - top < height * .045 || bottom - top > height * .10) continue;
        // Multiple button-shaped highlights are uncertain, not a safe menu.
        if (result) return std::nullopt;
        result = HomeSummaryMenuHighlight{double(left) / width, double(band_longest) / width,
            double(top + bottom) / (2 * height)};
    }
    return result;
}
}}}

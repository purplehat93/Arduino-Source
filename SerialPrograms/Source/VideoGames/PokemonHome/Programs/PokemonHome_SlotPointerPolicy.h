#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
namespace PokemonAutomation{ namespace NintendoSwitch{ namespace PokemonHome{
// Require one color to form a centered, narrowing downward pointer. Sprite
// colors can dominate the old center-stripe score without having this shape.
inline bool home_slot_pointer_shape(const uint32_t* pixels, size_t width, size_t height,
    size_t stride, size_t row, size_t column, uint32_t low, uint32_t high){
    if (!pixels || width < 640 || height < 360 || stride < width || row >= 5 || column >= 6) return false;
    const size_t center = size_t(width * (.073 + .072 * column));
    auto matches = [&](size_t x, size_t y){
        const uint32_t p = pixels[y * stride + x];
        for (unsigned shift : {0u, 8u, 16u}){
            const auto value = (p >> shift) & 255;
            if (value < ((low >> shift) & 255) || value > ((high >> shift) & 255)) return false;
        }
        return true;
    };
    const int tolerance = std::max(1, int(height * .003));
    for (int offset = -tolerance; offset <= tolerance; ++offset){
        std::array<size_t, 3> widths{};bool centered = true;
        const double levels[] = {.156, .164, .168};
        for (size_t i = 0; i < 3; ++i){
            const int yi = int(height * (levels[i] + .105 * row)) + offset;
            if (yi < 0 || size_t(yi) >= height || !matches(center, size_t(yi))){ centered = false;break; }
            size_t left = center, right = center;
            const size_t radius = size_t(width * .030);
            while (left > center - radius && matches(left - 1, size_t(yi))) --left;
            while (right < center + radius && matches(right + 1, size_t(yi))) ++right;
            const double displacement = (double(left) + right) / 2 - center;
            if (displacement < -double(width) * .002 || displacement > width * .002){centered = false;break;}
            widths[i] = right - left + 1;
        }
        if (centered && widths[0] >= width * .014 && widths[0] <= width * .028
            && widths[1] >= width * .005 && widths[1] <= width * .018
            && widths[2] >= width * .002 && widths[2] <= width * .010
            && widths[1] * 100 < widths[0] * 80 && widths[2] * 100 < widths[1] * 85) return true;
    }
    return false;
}
}}}

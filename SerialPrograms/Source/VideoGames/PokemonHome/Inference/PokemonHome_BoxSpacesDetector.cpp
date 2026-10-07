/*  Box Spaces Screen Detector
 */

#include <algorithm>
#include <cmath>
#include "Common/Cpp/Color.h"
#include "CommonFramework/ImageTools/ImageStats.h"
#include "CommonFramework/ImageTools/ImageBoxes.h"
#include "CommonFramework/ImageTypes/ImageRGB32.h"
#include "CommonFramework/ImageTypes/ImageViewRGB32.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "CommonTools/Images/ImageFilter.h"
#include "PokemonHome_BoxSpacesDetector.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonHome{

double home_ui_brightness_scale(const ImageViewRGB32& screen){
    if (!screen || screen.width() < 640 || screen.height() < 360) return 0;
    const auto white = image_stats(extract_box_reference(screen, ImageFloatBox{0.005, 0.02, 0.01, 0.02})).average;
    const double level = std::max({white.r, white.g, white.b});
    return level < 32 ? 0 : std::min(1.0, level / 255.0);
}

uint32_t home_ui_scaled_color(uint32_t color, double scale, int padding){
    auto channel = [&](uint32_t value){
        return (uint8_t)std::clamp<long>(std::lround(value * scale) + padding, 0, 255);
    };
    return combine_rgb(channel((color >> 16) & 255), channel((color >> 8) & 255), channel(color & 255));
}

namespace{

// Box Spaces has a bright blue header over the six-by-five box grid and a
// teal header over the box-count panel. Requiring both keeps the detector
// independent of the contents and names of the user's boxes.
// The selected-box name overlays the upper header on the first row. This
// bottom strip remains blue in every overview selection, and is teal in the
// ordinary box view (even when its Box Spaces footer button is selected).
const ImageFloatBox BOX_GRID_HEADER{0.03, 0.153, 0.45, 0.008};
const ImageFloatBox BOX_COUNT_HEADER{0.55, 0.315, 0.38, 0.065};

bool has_color_coverage(
    const ImageViewRGB32& screen,
    const ImageFloatBox& region,
    uint32_t minimum,
    uint32_t maximum,
    double required_fraction
){
    const double scale = home_ui_brightness_scale(screen);
    if (scale == 0) return false;
    const ImageViewRGB32 crop = extract_box_reference(screen, region);
    size_t pixels_in_range = 0;
    filter_rgb32_range(
        pixels_in_range, crop,
        home_ui_scaled_color(minimum, scale, scale < 0.8 ? -8 : 0), home_ui_scaled_color(maximum, scale, scale < 0.8 ? 8 : 0), Color(0), true
    );
    const size_t total = crop.width() * crop.height();
    return total != 0 && pixels_in_range >= total * required_fraction;
}

}

BoxSpacesDetector::BoxSpacesDetector(VideoOverlay*){}

void BoxSpacesDetector::make_overlays(VideoOverlaySet& items) const{
    items.add(COLOR_RED, BOX_GRID_HEADER);
    items.add(COLOR_RED, BOX_COUNT_HEADER);
}

bool BoxSpacesDetector::detect(const ImageViewRGB32& screen){
    if (!screen || screen.width() < 640 || screen.height() < 360){
        return false;
    }

    const bool grid_header = has_color_coverage(
        screen, BOX_GRID_HEADER,
        combine_rgb(0, 140, 220), combine_rgb(80, 230, 255),
        0.60
    );
    if (!grid_header){
        return false;
    }

    return has_color_coverage(
        screen, BOX_COUNT_HEADER,
        combine_rgb(0, 130, 130), combine_rgb(80, 230, 230),
        0.60
    );
}

class Test_BoxSpacesDetector : public UnitTest{
public:
    Test_BoxSpacesDetector(const std::string& name, bool blue_header, bool teal_header, bool expected, bool label = false, bool ordinary = false, bool dim = false)
        : UnitTest("PokemonHome::BoxSpacesDetector - " + name)
        , m_blue_header(blue_header)
        , m_teal_header(teal_header)
        , m_expected(expected)
        , m_label(label)
        , m_ordinary(ordinary)
        , m_dim(dim)
    {}

    virtual UnitTestResult run(Logger&, CancellableScope&) const override{
        ImageRGB32 image(1920, 1080);
        image.fill(combine_rgb(170, 220, 190));

        auto fill_region = [&](const ImageFloatBox& box, uint32_t color){
            const size_t x0 = (size_t)(box.x * image.width());
            const size_t y0 = (size_t)(box.y * image.height());
            const size_t x1 = (size_t)((box.x + box.width) * image.width());
            const size_t y1 = (size_t)((box.y + box.height) * image.height());
            for (size_t y = y0; y < y1; y++){
                for (size_t x = x0; x < x1; x++){
                    image.pixel(x, y) = color;
                }
            }
        };

        fill_region({0.005, 0.02, 0.01, 0.02}, combine_rgb(255, 255, 255));
        if (m_blue_header){
            fill_region({0.02, 0.08, 0.47, 0.085}, m_ordinary ? combine_rgb(20, 180, 180) : combine_rgb(20, 187, 251));
            if (m_label){
                // A real top-row box label obscures most of the old header
                // region but leaves the bottom strip and pointer area visible.
                fill_region({0.02, 0.08, 0.47, 0.069}, combine_rgb(40, 50, 50));
            }
        }
        if (m_teal_header){
            fill_region(BOX_COUNT_HEADER, combine_rgb(16, 191, 192));
        }

        if (m_dim){
            for (size_t y = 0; y < image.height(); y++){
                for (size_t x = 0; x < image.width(); x++){
                    image.pixel(x, y) = home_ui_scaled_color(image.pixel(x, y), 0.25);
                }
            }
        }
        BoxSpacesDetector detector;
        return detector.detect(image) == m_expected;
    }

private:
    bool m_blue_header;
    bool m_teal_header;
    bool m_expected;
    bool m_label;
    bool m_ordinary;
    bool m_dim;
};

void add_tests_BoxSpacesDetector(UnitTestDatabase& database){
    database.add<Test_BoxSpacesDetector>("both overview headers", true, true, true);
    database.add<Test_BoxSpacesDetector>("blue header only", true, false, false);
    database.add<Test_BoxSpacesDetector>("teal header only", false, true, false);
    database.add<Test_BoxSpacesDetector>("neither header", false, false, false);
    database.add<Test_BoxSpacesDetector>("top row label obscures header", true, true, true, true);
    database.add<Test_BoxSpacesDetector>("ordinary box footer selected", true, true, false, false, true);
    database.add<Test_BoxSpacesDetector>("dim overview with obscured header", true, true, true, true, false, true);
    database.add<Test_BoxSpacesDetector>("dim ordinary box footer", true, true, false, false, true, true);
}

}
}
}

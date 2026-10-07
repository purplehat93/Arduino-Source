/*  Box Spaces Screen Detector
 */

#ifndef PokemonAutomation_PokemonHome_BoxSpacesDetector_H
#define PokemonAutomation_PokemonHome_BoxSpacesDetector_H

#include "Common/Cpp/TestRunners/UnitTestDatabase.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "CommonTools/VisualDetector.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonHome{

// HOME dims the whole rendered UI after inactivity. Use its invariant white
// top-left margin to scale color thresholds without changing the capture.
double home_ui_brightness_scale(const ImageViewRGB32& screen);
uint32_t home_ui_scaled_color(uint32_t color, double scale, int padding = 0);

class BoxSpacesDetector : public StaticScreenDetector{
public:
    BoxSpacesDetector(VideoOverlay* overlay = nullptr);

    virtual void make_overlays(VideoOverlaySet& items) const override;
    virtual bool detect(const ImageViewRGB32& screen) override;
};

class BoxSpacesWatcher : public DetectorToFinder<BoxSpacesDetector>{
public:
    BoxSpacesWatcher(VideoOverlay* overlay = nullptr)
        : DetectorToFinder("BoxSpacesWatcher", Milliseconds(100), overlay)
    {}
};

void add_tests_BoxSpacesDetector(UnitTestDatabase& database);

}
}
}
#endif

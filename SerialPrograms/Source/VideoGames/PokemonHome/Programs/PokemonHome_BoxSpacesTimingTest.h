/*  Pokemon HOME Box Spaces timing test
 */

#ifndef PokemonAutomation_PokemonHome_BoxSpacesTimingTest_H
#define PokemonAutomation_PokemonHome_BoxSpacesTimingTest_H

#include "Common/Cpp/Options/TimeDurationOption.h"
#include "Common/Cpp/Options/BooleanCheckBoxOption.h"
#include "NintendoSwitch/NintendoSwitch_SingleSwitchProgram.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonHome{

class BoxSpacesTimingTest_Descriptor : public SingleSwitchProgramDescriptor{
public:
    BoxSpacesTimingTest_Descriptor();
};

class BoxSpacesTimingTest : public SingleSwitchProgramInstance{
public:
    using Descriptor = BoxSpacesTimingTest_Descriptor;
    BoxSpacesTimingTest();

    virtual void program(SingleSwitchProgramEnvironment& env, ProControllerContext& context) override;

private:
    MillisecondsOption GAME_DELAY;
    MillisecondsOption PAGE_DELAY;
    BooleanCheckBoxOption EXTENDED;
    BooleanCheckBoxOption SORTER_CHECK_ONLY;
    BooleanCheckBoxOption ROUTING_COMPARE_ONLY;
    BooleanCheckBoxOption HELD_ROUTING_COMPARE_ONLY;
};

}
}
}
#endif

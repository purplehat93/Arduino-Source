#ifndef PokemonAutomation_PokemonHome_CollectionManager_H
#define PokemonAutomation_PokemonHome_CollectionManager_H

#include "Common/Cpp/Options/SimpleIntegerOption.h"
#include "Common/Cpp/Options/BooleanCheckBoxOption.h"
#include "Common/Cpp/Options/StringOption.h"
#include "Common/Cpp/Options/TimeDurationOption.h"
#include "CommonTools/Options/LanguageOCROption.h"
#include "NintendoSwitch/NintendoSwitch_SingleSwitchProgram.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonHome{

class CollectionManager_Descriptor : public SingleSwitchProgramDescriptor{
public:
    CollectionManager_Descriptor();
};

// Inventory and explicitly prepared bounded plans. No release operations.
class CollectionManager : public SingleSwitchProgramInstance{
public:
    using Descriptor = CollectionManager_Descriptor;
    CollectionManager();
    void program(SingleSwitchProgramEnvironment& env, ProControllerContext& context) override;
private:
    SimpleIntegerOption<uint16_t> FIRST_BOX;
    SimpleIntegerOption<uint16_t> BOX_NUMBER;
    OCR::LanguageOCROption HOME_LANGUAGE;
    StringOption OUTPUT_FILE;
    BooleanCheckBoxOption RESUME_INDEX;
    StringOption SAVED_PLAN;
    BooleanCheckBoxOption VERIFY_ONLY;
    BooleanCheckBoxOption RESUME_PLAN;
    SimpleIntegerOption<uint16_t> MOVES_PER_RUN;
    MillisecondsOption VIDEO_DELAY;
    MillisecondsOption GAME_DELAY;
};

}
}
}
#endif

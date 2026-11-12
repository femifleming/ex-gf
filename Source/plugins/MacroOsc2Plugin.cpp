#include "../algorithms/MacroOsc2.h"

class MacroOsc2Plugin final : public ex::ExPlugin
{
public:
    MacroOsc2Plugin() : ex::ExPlugin ({ "EX Macro Oscillator 2", false, false, 1 })
    {
        addAlgorithm (std::make_unique<ex::MacroOsc2> (*this));
        finishSetup();
    }
};

#ifndef EX_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MacroOsc2Plugin(); }
#endif

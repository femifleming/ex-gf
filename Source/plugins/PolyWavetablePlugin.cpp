#include "../algorithms/PolyWavetable.h"

class PolyWavetablePlugin final : public ex::ExPlugin
{
public:
    PolyWavetablePlugin() : ex::ExPlugin ({ "EX Poly Wavetable", false, false, 1 })
    {
        addAlgorithm (std::make_unique<ex::PolyWavetable> (*this));
        finishSetup();
    }
};

#ifndef EX_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PolyWavetablePlugin(); }
#endif

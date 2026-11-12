#include "../algorithms/Resonator.h"

class ResonatorPlugin final : public ex::ExPlugin
{
public:
    ResonatorPlugin() : ex::ExPlugin ({ "EX Resonator", false, true, 1 })
    {
        addAlgorithm (std::make_unique<ex::Resonator> (*this));
        finishSetup();
    }
};

#ifndef EX_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ResonatorPlugin(); }
#endif

#include "../algorithms/Granulator.h"

class GranulatorPlugin final : public ex::ExPlugin
{
public:
    GranulatorPlugin() : ex::ExPlugin ({ "EX Granulator", false, true, 1 })
    {
        addAlgorithm (std::make_unique<ex::Granulator> (*this));
        finishSetup();
    }
};

#ifndef EX_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GranulatorPlugin(); }
#endif

#include "../algorithms/SpectralFreeze.h"

class SpectralFreezePlugin final : public ex::ExPlugin
{
public:
    SpectralFreezePlugin() : ex::ExPlugin ({ "EX Spectral Freeze", false, true, 1 })
    {
        addAlgorithm (std::make_unique<ex::SpectralFreeze> (*this));
        finishSetup();
    }
};

#ifndef EX_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpectralFreezePlugin(); }
#endif

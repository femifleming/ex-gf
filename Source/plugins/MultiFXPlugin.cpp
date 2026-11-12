#include "../algorithms/MultiFX.h"

class MultiFXPlugin final : public ex::ExPlugin
{
public:
    MultiFXPlugin() : ex::ExPlugin ({ "EX Multi-FX", false, true, 1, false })
    {
        addAlgorithm (std::make_unique<ex::MultiFX> (*this));
        finishSetup();
    }
};

#ifndef EX_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MultiFXPlugin(); }
#endif

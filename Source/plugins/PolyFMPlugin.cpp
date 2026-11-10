#include "../algorithms/PolyFM.h"

class PolyFMPlugin final : public ex::ExPlugin
{
public:
    PolyFMPlugin() : ex::ExPlugin ({ "EX Poly FM", true, false, 4 })
    {
        addAlgorithm (std::make_unique<ex::PolyFM> (*this));
        finishSetup();
    }
};

#ifndef EX_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PolyFMPlugin(); }
#endif

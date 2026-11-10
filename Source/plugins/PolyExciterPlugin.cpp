#include "../algorithms/PolyExciter.h"

class PolyExciterPlugin final : public ex::ExPlugin
{
public:
    PolyExciterPlugin() : ex::ExPlugin ({ "EX Poly Exciter", false, true, 1 })
    {
        addAlgorithm (std::make_unique<ex::PolyExciter> (*this));
        finishSetup();
    }
};

#ifndef EX_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PolyExciterPlugin(); }
#endif

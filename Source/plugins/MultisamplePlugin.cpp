#include "../algorithms/SdMultisample.h"
#include "../algorithms/WavRecorder.h"

// SD Multisample (algorithm 3) and WAV Recorder (algorithm 5) in one instrument, switchable from the
// Algorithms menu - the recorder's auto-sampler writes folders that the multisample algorithm plays.
class MultisamplePlugin final : public ex::ExPlugin
{
public:
    MultisamplePlugin() : ex::ExPlugin ({ "EX Multisample", true, true, 3 })
    {
        addAlgorithm (std::make_unique<ex::SdMultisample> (*this));
        addAlgorithm (std::make_unique<ex::WavRecorder> (*this));
        finishSetup();
    }
};

#ifndef EX_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MultisamplePlugin(); }
#endif

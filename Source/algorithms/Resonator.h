#pragma once
// 20 - Resonator: Mutable Instruments Rings (six resonator models + the string-synth mode) with a stereo output mix.
#include "../core/FxAlgorithm.h"
#include "../core/FxBlocks.h"
#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/strummer.h"

namespace ex
{
class Resonator : public FxAlgorithm
{
public:
    explicit Resonator (ExPlugin& p);
    void prepare (double sampleRate, int block) override;
    void configure() override;
    void midi (const juce::MidiMessage& m) override;
    void render (const float* inL, const float* inR, float* const* out, int n) override;
    void drawBottomLine (Oled& o, int cur) override;
    void addMenuItems (std::vector<MenuItem>& items) override { addStandardPolyMenu (items, false, false); }
    std::vector<BrowserSpec> browsers() override;
    std::vector<ActionSpec> actions() override;
    int numMeters() override { return 3; }
    Meter meter (int i) override;
    juce::String metersTitle() override { return "Levels"; }
    bool pushL() override { strumRequest = true; return true; }

private:
    struct Engine { rings::Part part; rings::StringSynthPart synth; rings::Strummer strummer; };
    std::unique_ptr<Zeroed<Engine>> engine;
    rings::Part* part = nullptr;
    rings::StringSynthPart* synth = nullptr;
    rings::Strummer* strummer = nullptr;
    std::vector<uint16_t> reverbBuffer;
    RateAdapter adapter;
    rings::Patch patch {};
    // read once per block
    int mode = 0, midiMode = 3, chord = 0;
    bool noiseGate = true, useInput = true;
    float tonic = 48.0f, inGain = 1, outGain = 1, dryGain = 1, oddL = 1, oddR = 0, evenL = 0, evenR = 1, dryL = 1, dryR = 1;
    // MIDI state
    float midiNote = 48.0f, bendSemis = 0;
    bool strumPending = false;
    std::atomic<bool> strumRequest { false };
    std::atomic<int> strumFlash { 0 };
    float inLevel = 0;      // noise gate follower
    static constexpr int kWork = 512;
    std::vector<float> workIn, workOdd, workEven;
    PeakMeter meters[3];

    void renderBlock (float* const* in, float* const* out);
};
} // namespace ex

#pragma once
// 21 - Macro Oscillator 2: four instances of Mutable Instruments Plaits (firmware 1.2, all 24 models).
#include "../core/FxAlgorithm.h"
#include "../core/FxBlocks.h"
#include "plaits/dsp/voice.h"

namespace ex
{
class MacroOsc2 : public FxAlgorithm
{
public:
    explicit MacroOsc2 (ExPlugin& p);
    ~MacroOsc2() override { stopJobs(); }
    void prepare (double sampleRate, int block) override;
    void configure() override;
    void midi (const juce::MidiMessage& m) override;
    void render (const float* inL, const float* inR, float* const* out, int n) override;
    void allNotesOff() override;
    void drawPage (Oled& o, int cur) override;
    void addMenuItems (std::vector<MenuItem>& items) override;
    void onSelected() override;
    std::vector<BrowserSpec> browsers() override;
    std::vector<ActionSpec> actions() override;
    juce::String statusText() override;
    int numMeters() override { return kOscs; }
    Meter meter (int i) override;
    juce::String metersTitle() override { return "Oscillators"; }

    static constexpr int kOscs = 4, kPerOsc = 11, kFirst = 7;
    static juce::String modelName (int model);
    static int engineFor (int model) { return (juce::jlimit (0, 23, model) + 8) % 24; }
    static bool isSixOp (int model) { return model >= 18 && model <= 20; }
    static juce::String midiOptionName (int v);

private:
    struct BankSet { std::array<std::array<uint8_t, 4096>, 3> data {}; bool custom[3] { false, false, false }; };
    struct Osc
    {
        std::unique_ptr<Zeroed<plaits::Voice>> holder;
        plaits::Voice* voice = nullptr;
        std::unique_ptr<char[]> ram;
        plaits::Patch patch {};
        plaits::Modulations mod {};
        float note = 60.0f, vel = 0.8f;   // last MIDI pitch / velocity
        int held = -1, heldCh = 0;        // note currently holding the gate
        bool gate = false, sustained = false, retrig = false;
        int trigBlocks = 0;
        float gainNow = 1.0f;
        int model = 0, option = 141;      // read once per block
        PeakMeter level;
    };
    Osc osc[kOscs];
    RateAdapter adapter;
    plaits::Voice::Frame frames[plaits::kBlockSize];
    static constexpr int kWork = 512;
    std::vector<float> work[kOscs * 2];
    std::shared_ptr<BankSet> banks, pendingBanks;   // audio thread / handed over from the loader
    std::shared_ptr<const BankSet> names;           // for the voice-name lists
    bool gateMode = false, individual = false;
    float bendRange = 2.0f, outGain[4] { 1, 1, 1, 1 };
    int outAssign[4] { 0, 2, 4, 6 };
    std::atomic<bool> banksPending { false };
    float bendSemis = 0;
    bool sustainOn = false;
    int rr = 0;
    int selOsc = 0;                       // which oscillator the lists edit (UI)
    std::atomic<bool> panic { false };

    int base (int o) const { return kFirst + kPerOsc * (juce::jlimit (1, kOscs, pv (59 + o)) - 1); }   // first parameter of the set oscillator o uses
    void renderBlock (float* const* out);
    void noteOn (int ch, int note, float vel);
    void noteOff (int ch, int note);
    void trigger (Osc& o, int ch, int note, float vel, bool setPitch, bool hold);
    void loadBanks();
    juce::String patchName (int bank, int index) const;
};
} // namespace ex

#pragma once
#include "../core/PolyAlgorithm.h"
#include "plaits/dsp/fm/algorithms.h"
#include "plaits/dsp/fm/lfo.h"
#include "plaits/dsp/fm/patch.h"
#include "plaits/dsp/fm/voice.h"

namespace ex
{
struct FmBank { juce::String name; std::array<uint8_t, 4096> data {}; bool builtin = false; };

// DX7 VCED (155 byte) <-> VMEM (128 byte packed) conversion
void dx7Pack (const uint8_t* vced, uint8_t* vmem);
void dx7Unpack (const uint8_t* vmem, uint8_t* vced);
juce::String dx7Name (const uint8_t* vmem);

class PolyFM : public PolyAlgorithm, private juce::Timer
{
public:
    explicit PolyFM (ExPlugin& p);
    ~PolyFM() override { stopTimer(); stopJobs(); }
    void prepare (double sampleRate, int block) override;
    void configure() override;
    void renderVoices (float* const* out, int start, int n) override;
    void postProcess (float* const* out, int n) override;
    void onOtherMidi (const juce::MidiMessage& m) override;
    int bendRangeParam() const override { return 38; }
    int sustainParam() const override { return 37; }
    void drawPage (Oled& o, int cur) override;
    void addMenuItems (std::vector<MenuItem>& items) override;
    void onSelected() override;

    std::shared_ptr<std::vector<FmBank>> banks() const { return std::atomic_load (&bankList); }
    static constexpr int kTimbres = 4;
    int uiTimbre = 0;
    std::vector<BrowserSpec> browsers() override;
    std::vector<ActionSpec> actions() override;

private:
    plaits::fm::Algorithms<6> algorithms;
    struct Voice
    {
        plaits::fm::Voice<6> fm;
        plaits::fm::Lfo lfo;
        plaits::fm::Patch patch;
        plaits::fm::Voice<6>::Parameters params {};
        bool active = false, gate = false, needRetrig = false;
        int timbre = 0, transpose = 0;
        float pitch = 60, vel = 1, level = 0;
        int quietSamples = 0;
    };
    Voice voices[PolyNoteEngine::kMaxVoices];
    std::shared_ptr<std::vector<FmBank>> bankList;
    // per-timbre current packed voice (bank/voice or edited)
    uint8_t timbreVoice[kTimbres][128] {};
    bool timbreEdited[kTimbres] {};
    std::atomic<bool> editedFlag[kTimbres];
    int loadedBank[kTimbres] { -1, -1, -1, -1 }, loadedVoice[kTimbres] { -1, -1, -1, -1 };
    std::shared_ptr<std::vector<FmBank>> loadedList;
    StereoDelay delay;
    JunoChorus chorus;
    std::vector<float> tmp;
    std::atomic<int> sendRequest { 0 }; // 1 voice dump, 2 bank dump
    juce::String statusMsg;

    int effBank (int t) const;
    int effVoice (int t) const;
    void refreshTimbres();
    void scanBanks();
    void timerCallback() override;
    void sendVoiceDump (juce::MidiBuffer& out, int t);
    void sendBankDump (juce::MidiBuffer& out, int t);
    void writeBuiltinToSd();
};
} // namespace ex

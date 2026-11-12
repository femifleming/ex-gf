#pragma once
#include "../core/FxAlgorithm.h"
#include "../core/FxBlocks.h"
#include <juce_dsp/juce_dsp.h>

namespace ex
{
// disting EX algorithm 24: four phase-vocoder 'freeze' voices with movement, pitch shift, delay and reverb.
class SpectralFreeze : public FxAlgorithm
{
public:
    explicit SpectralFreeze (ExPlugin& p);
    void prepare (double sampleRate, int block) override;
    void configure() override;
    void midi (const juce::MidiMessage& m) override;
    void render (const float* inL, const float* inR, float* const* out, int n) override;
    void drawPage (Oled& o, int cur) override;
    void drawBottomLine (Oled& o, int cur) override;
    bool pushL() override { toggleReq.fetch_add (1); return true; }
    void addMenuItems (std::vector<MenuItem>& items) override;
    std::vector<BrowserSpec> browsers() override;
    std::vector<ActionSpec> actions() override;
    juce::String statusText() override;
    int numMeters() override { return kVoices; }
    Meter meter (int index) override;
    juce::String metersTitle() override { return "VOICES"; }
    void saveExtra (juce::XmlElement& x) const override { x.setAttribute ("visuals", visuals); }
    void loadExtra (const juce::XmlElement& x) override { visuals = x.getBoolAttribute ("visuals", false); }

    static constexpr int kVoices = 4;
    enum State { Idle = 0, Frozen, Live, Releasing };

private:
    static constexpr int kChunk = 256, kMaxOffset = 1023, kMaxDepth = 128;
    struct Voice
    {
        bool gate = false, live = false, active = false, fresh = true;
        float env = 0;
        std::vector<float> snap, ola, outBuf, phRe, phIm;
        double rp = 0;      // read position in outBuf
        double u = 0;       // position inside the depth window, in frames (0 = oldest)
        int dir = -1;
        double skipAcc = 0;
        uint32_t seed = 1;
        int64_t age = 0;
    };
    Voice voices[kVoices];

    // analysis / synthesis
    int fftSize = 2048, hop = 512, bins = 1025, histSize = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> hist, window, bufA, bufB, mags, peakScratch;
    std::vector<uint8_t> keep;
    int histW = 0;
    int64_t histCount = 0, ageCounter = 0;

    // per-block settings
    double speed = 0, winLen = 1, ratio[kVoices] { 1, 1, 1, 1 };
    int offsetFrames = 0, movement = 2, ether = 127;
    float gainL[kVoices] {}, gainR[kVoices] {}, attackInc = 1, releaseInc = 1, dryGain = 1;

    // control state (audio thread)
    int prevToggle = 0, prevGate = 0, prevVoice[kVoices] {}, prevLive = 0, prevUnfreeze = 0, prevTargetParam = -1;
    int autoHeld = -1, gateVoice = -1, liveVoice = -1, lastAuto = kVoices - 1;

    // requests from the UI, state for the UI
    std::atomic<int> toggleReq { 0 }, liveReq { 0 }, unfreezeReq { 0 }, clearFxReq { 0 }, targetOverride { -1 };
    std::atomic<int> voiceReq[kVoices];
    std::atomic<int> uiState[kVoices];
    std::atomic<float> uiEnv[kVoices], uiPos[kVoices];
    bool visuals = false;

    StereoDelay delay;
    ExReverb reverb;

    int target() const { int o = targetOverride.load(); return o >= 0 ? o : pv (9); }
    void startVoice (int i, bool live);
    void releaseVoice (int i);
    void toggleVoice (int i) { if (voices[i].gate) releaseVoice (i); else startVoice (i, false); }
    int nextVoice();
    void doToggle();
    void gateChange (bool on);
    void liveChange (bool on);
    void synthHop (Voice& v, int index);
    void fetch (const Voice& v, int64_t end, float* dst) const;
    void advance (Voice& v, double step);
    void chunk (const float* inL, const float* inR, float* const* out, int n);
    void applyReverb (float* L, float* R, int n, float mix);
};
} // namespace ex

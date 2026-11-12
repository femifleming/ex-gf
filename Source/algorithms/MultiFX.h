#pragma once
// 12 - Multi FX: EQ -> pitch effects / delay / reverb with free routing, on a variable sample rate clock.
#include "../core/FxAlgorithm.h"
#include "../core/FxBlocks.h"

namespace ex
{
class MultiFX : public FxAlgorithm
{
public:
    explicit MultiFX (ExPlugin& p);
    void prepare (double sampleRate, int block) override;
    void configure() override;
    void render (const float* inL, const float* inR, float* const* out, int n) override;

    void drawPage (Oled& o, int cur) override;
    void drawBottomLine (Oled& o, int cur) override;
    void addMenuItems (std::vector<MenuItem>& items) override;
    bool pushR (int cur) override;
    std::vector<BrowserSpec> browsers() override;
    std::vector<ActionSpec> actions() override;
    int numMeters() override { return 4; }
    Meter meter (int index) override;
    juce::String metersTitle() override { return "LEVELS"; }
    void saveExtra (juce::XmlElement& x) const override { x.setAttribute ("program", program); x.setAttribute ("bypass", bypass.load() ? 1 : 0); }
    void loadExtra (const juce::XmlElement& x) override { program = x.getIntAttribute ("program", 0); bypass = x.getIntAttribute ("bypass", 0) != 0; }

    static juce::StringArray programNames();
    void loadProgram (int index);

private:
    struct Line
    {
        std::vector<float> b;
        int mask = 0, w = 0;
        void init (int minSize) { int s = 16; while (s < minSize) s <<= 1; b.assign ((size_t) s, 0.0f); mask = s - 1; w = 0; }
        void clear() { std::fill (b.begin(), b.end(), 0.0f); }
        int size() const { return mask + 1; }
        float read (float d) const
        {
            float rp = (float) w - d;
            int i0 = (int) std::floor (rp);
            float fr = rp - (float) i0;
            float a = b[(size_t) (i0 & mask)], c = b[(size_t) ((i0 + 1) & mask)];
            return a + fr * (c - a);
        }
        void write (float x) { b[(size_t) w] = x; w = (w + 1) & mask; }
    };
    struct DelayTap { float cur = -1, next = 0, xf = 0, slew = -1; bool fading = false; };

    // values derived from the parameters once per block
    struct Config
    {
        float rate = 1;
        bool mono = false, dryPost = false, sat = true;
        float gDry = 1, gFx = 1, gPitch = 1, gDelay = 1, gReverb = 1;
        int pIn1 = 1, pIn2 = 3, dIn1 = 1, dIn2 = 2, rIn1 = 1, rIn2 = 3;
        float pMix = -1, dMix = -1, rMix = -1;
        int pitchFx = 0;
        float blend = 1, pitchFb = 0;
        float chorusDelay = 100, lfoInc = 0, depth = 0, spread = 0.5f, phaserHz = 800;
        float shiftWin = 1000, shiftInc = 0;
        int delayMode = 0;
        float delayL = 1000, delayR = 1000, delayFb = 0.5f, monoize = 1, panL = 1, panR = 0;
        float revLpA = 0;
        int aux = 1;
    };

    Config c;
    Biquad eq[2][3];
    Line pitchLine[2], delayLine[2];
    DelayTap tap[2];
    ExReverb reverb;
    float src[5][2] {};          // pre-EQ, post-EQ, pitch, delay, reverb outputs (the last three from the previous sample)
    float lfoPhase = 0, shiftPhase = 0, chorusSm = -1;
    float apZ[2][6] {}, phaserFb[2] {};
    float revLp[2] {};
    float gSm[5] { 1, 1, 1, 1, 1 };
    // variable sample rate clock
    float rateSm = 1, phase = 0, accL = 0, accR = 0;
    int accN = 0;
    float prevOut[4] {}, curOut[4] {};

    PeakMeter meters[4];
    std::atomic<bool> bypass { false }, clearRequest { false };
    std::atomic<int> clipSamples { 0 };
    int program = 0;
    double lastTap = 0;

    void tick (float l, float r, float* o);
    float readTap (Line& line, DelayTap& t, float target, bool slew);
    void flush();
    static void setFirstOrder (Biquad& b, bool high, float hz, double sr);
    static void setSecondOrder (Biquad& b, bool high, float hz, double sr);
    static void setPeak (Biquad& b, float hz, float q, float db, double sr);
    static juce::String blockName (int n);
};
} // namespace ex

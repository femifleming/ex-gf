#pragma once
#include "../core/PolyAlgorithm.h"

namespace ex
{
struct ExciterTable
{
    juce::String name;
    std::vector<std::vector<float>> exciters; // mono, <= 28672 frames each
    juce::StringArray names;
    double sampleRate = 48000;
    static std::shared_ptr<ExciterTable> load (const juce::File& entry);
};

class PolyExciter : public PolyAlgorithm
{
public:
    explicit PolyExciter (ExPlugin& p);
    ~PolyExciter() override { stopJobs(); }
    void prepare (double sampleRate, int block) override;
    void configure() override;
    void renderVoices (float* const* out, int start, int n) override;
    void postProcess (float* const* out, int n) override;
    int bendRangeParam() const override { return 36; }
    int sustainParam() const override { return 35; }
    void drawPage (Oled& o, int cur) override;
    void addMenuItems (std::vector<MenuItem>& items) override { addStandardPolyMenu (items, true, false); }
    void onSelected() override;
    bool pushR (int cur) override;
    std::vector<BrowserSpec> browsers() override;
    std::vector<ActionSpec> actions() override
    {
        return { { "Notes off", [this] { plugin.allNotesOffRequest = true; }, {} },
                 { "Rescan", [this] { onSelected(); plugin.message ("Rescanned exciters"); }, {} } };
    }

private:
    static constexpr int kDelay = 8192;
    struct Voice
    {
        bool active = false, gate = false;
        int note = 60, timbre = 0;
        float pitch = 60, vel = 1;
        std::vector<float> line;
        int w = 0;
        float lp = 0, dcX = 0, dcY = 0, outLp = 0;
        double exPos = 0; bool exPlaying = false;
        int exIndex = 0;
        uint32_t seed = 1;
        Envelope env;
        Lfo lfo;
        float energy = 0;
        int quiet = 0;
        float fade = 1; bool killing = false;
    };
    Voice voices[PolyNoteEngine::kMaxVoices];
    std::shared_ptr<ExciterTable> table;
    juce::Array<juce::File> entries;
    int loadedTable = -999;
    StereoDelay delay;
    JunoChorus chorus;
    int lastExciter = 0;
    double lastTap = 0;
    float exciterSample (const Voice& v, const ExciterTable* t, int idx, double pos, float lenSamples, uint32_t& seed) const;
    int exciterIndexFor (const Voice& v, float env, float lfo, float pressure, int count) const;
    float pressureFor (const Voice& v) const;
};
} // namespace ex

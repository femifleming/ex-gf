#pragma once
#include "../core/PolyAlgorithm.h"
#include <juce_dsp/juce_dsp.h>

namespace ex
{
// Band-limited wavetable: every wave resampled to 2048 samples with 11 octave mip levels.
struct WaveTable
{
    static constexpr int kLen = 2048, kLevels = 11;
    juce::String name;
    int numWaves = 0;
    std::vector<float> data; // [wave][level][kLen]
    const float* wave (int w, int level) const { return data.data() + ((size_t) w * kLevels + (size_t) level) * kLen; }
    static std::shared_ptr<WaveTable> load (const juce::File& entry, int wavelength);
    static std::shared_ptr<WaveTable> fallback();
};

class PolyWavetable : public PolyAlgorithm
{
public:
    explicit PolyWavetable (ExPlugin& p);
    ~PolyWavetable() override { stopJobs(); }
    void prepare (double sampleRate, int block) override;
    void configure() override;
    void renderVoices (float* const* out, int start, int n) override;
    void postProcess (float* const* out, int n) override;
    int bendRangeParam() const override { return 43; }
    int sustainParam() const override { return 42; }
    void drawPage (Oled& o, int cur) override;
    void drawBottomLine (Oled& o, int cur) override;
    void confirm (int n) override;
    void onSelected() override;
    void addMenuItems (std::vector<MenuItem>& items) override { addStandardPolyMenu (items, true, true); }
    bool pushR (int cur) override;
    std::vector<BrowserSpec> browsers() override;
    std::vector<ActionSpec> actions() override;
    bool acceptsFileImport() const override { return true; }
    bool importFiles (const juce::StringArray& paths) override;
    juce::String statusText() override { return "Drop WaveEdit banks / WAVs here, or IMPORT. WAVEEDIT = sync ~/Documents/WaveEdit/banks"; }
    static juce::Array<juce::File> importToCard (const juce::StringArray& paths);
    static juce::File waveEditFolder();
    std::unique_ptr<juce::FileChooser> chooser;

private:
    struct Voice
    {
        bool active = false;
        float pitch = 60, vel = 1, phase = 0, glideWave = -1;
        int note = 60, unison = 0;
        Envelope env1, env2;
        Lfo lfo;
        Svf svf;
        float pan = 0, posSmoothed = 0;
        float fade = 1.0f; bool killing = false;
    };
    Voice voices[PolyNoteEngine::kMaxVoices];
    std::shared_ptr<WaveTable> table;
    juce::Array<juce::File> entries;
    StereoDelay delay;
    JunoChorus chorus;
    float displayPos = 0;
    int loudestVoice = -1;
    double lastTap = 0;
    void loadTable (int index);
    float wavePosition (const Voice& v, int vi) const;
};
} // namespace ex

#pragma once
#include "../core/PolyAlgorithm.h"
#include "../core/FxBlocks.h"

namespace ex
{
// disting EX algorithm 11: granular synthesis from a live / loaded mono buffer, played by notes and three drones.
class Granulator : public PolyAlgorithm
{
public:
    explicit Granulator (ExPlugin& p);
    ~Granulator() override;
    void prepare (double sampleRate, int block) override;
    void configure() override;
    void renderVoices (float* const* out, int start, int n) override;
    void postProcess (float* const* out, int n) override;
    int bendRangeParam() const override { return 58; }
    int sustainParam() const override { return 67; }   // never read: sustain comes from CC64 only
    void allNotesOff() override { PolyAlgorithm::allNotesOff(); }
    void drawPage (Oled& o, int cur) override;
    void addMenuItems (std::vector<MenuItem>& items) override;
    void onSelected() override;
    bool pushL() override { set (11, pv (11) ? 0 : 1); return true; }
    bool pushR (int cur) override;
    std::vector<BrowserSpec> browsers() override;
    std::vector<ActionSpec> actions() override;
    juce::String statusText() override;
    bool acceptsFileImport() const override { return true; }
    bool importFiles (const juce::StringArray& paths) override;
    juce::String importHint() const override { return "into the granulator buffer"; }
    void saveExtra (juce::XmlElement& x) const override;
    void loadExtra (const juce::XmlElement& x) override;

    void loadFile (const juce::File& f, bool raw);   // message thread
    void saveBuffer();
    void clearBuffer();
    juce::String currentPath() const { return pathInvalid.load() ? juce::String() : savedPath; }
    int activeGrains() const { return dispCount.load(); }

private:
    static constexpr int kMaxGrains = 40, kNotes = PolyNoteEngine::kMaxVoices + 3, kWin = 256;
    struct Grain
    {
        bool active = false, midPending = false;
        double pos = 0, inc = 1;
        int len = 1, age = 0, owner = 0, shape = 0;
        float gl = 0, gr = 0;
    };
    struct Note
    {
        bool active = false, gate = false, first = false, chain = false;
        float pitch = 60, vel = 1, env = 0, level = 1, opacity = 1, extLevel = 1, extOpacity = 1;
        double countdown = 0;
        int refW = 0;
    };
    struct Loaded { std::vector<float> data; int ms = 0; double rate = 48000; };

    // audio thread
    std::vector<float> buf;
    int maxLen = 0, bufLen = 4800, w = 0, fileLen = 0, fileMs = -1;
    bool synced = false, droneWasOn[3] { false, false, false };
    Grain grains[kMaxGrains];
    Note notes[kNotes];
    float recG = 0, normG = 1, lfoPhase = 0;
    uint32_t seed = 0x9e3779b9u;
    StereoDelay delay;
    ExReverb reverb;

    // shared
    std::shared_ptr<Loaded> pending, keep;
    std::atomic<bool> clearRequest { false }, pathInvalid { false }, loading { false };
    std::atomic<int> dispW { 0 }, dispLen { 4800 }, dispCount { 0 };
    std::atomic<float> dispGrain[kMaxGrains];
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    // message thread
    juce::String savedPath;
    bool savedRaw = false, prepared = false;
    juce::Array<juce::File> folders, files;
    int folderIdx = 0;
    bool visuals = false, autoSuppressed = false, shown = false;
    int autoVisuals = 0, lastSig = -1;
    double lastChange = 0, lastTap = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) * (1.0f / 16777216.0f); } // 0..1
    float rnd2() { return rnd() * 2.0f - 1.0f; }
    bool spawn (int noteIndex, bool midPending, bool audible);
    float delayFraction (int param) const;
    void startLoad (const juce::File& f, bool raw, bool announce);
    void refreshFiles();
    void drawVisuals (Oled& o);
    void chooseFile (bool raw);
    static const float* window (int shape);
};
} // namespace ex

#pragma once
#include "../core/PolyAlgorithm.h"

namespace ex
{
// A folder of WAVs interpreted with the disting EX naming conventions / playlist.
struct SampleZone
{
    int natural = 48, switchNote = 0;
    bool explicitSwitch = false, hasNatural = false;
    int loopFlag = -1, endLoopOnRelease = 0;
    juce::String name;
    // layers[velocity][roundrobin]
    struct File { std::shared_ptr<WavData> wav; juce::String fileName; int index = 0; };
    std::vector<std::vector<File>> layers;
    // round robin state (audio thread)
    int rrCounter = 0, rrLast = -1; std::vector<int> rrPerm; int rrPos = 0;
};

struct SampleSet
{
    juce::String folderName;
    std::vector<SampleZone> zones;
    int numFiles = 0;
    static std::shared_ptr<SampleSet> load (const juce::File& folder);
    SampleZone* zoneFor (int note);
};

class SdMultisample : public PolyAlgorithm, private juce::Timer
{
public:
    explicit SdMultisample (ExPlugin& p);
    ~SdMultisample() override { stopTimer(); stopJobs(); }
    void prepare (double sampleRate, int block) override;
    void configure() override;
    void renderVoices (float* const* out, int start, int n) override;
    void postProcess (float* const* out, int n) override;
    int bendRangeParam() const override { return 19; }
    int sustainParam() const override { return 17; }
    bool timbreAcceptsNote (int t, int note) const override { return note >= pv (44 + 2 * t) && note <= pv (45 + 2 * t); }
    void drawPage (Oled& o, int cur) override;
    void addMenuItems (std::vector<MenuItem>& items) override;
    void onSelected() override;
    bool pushR (int cur) override;
    void saveExtra (juce::XmlElement& x) const override { x.setAttribute ("transposeLimit", transposeLimit); }
    void loadExtra (const juce::XmlElement& x) override { transposeLimit = x.getIntAttribute ("transposeLimit", 0); }

    int transposeLimit = 0; // 0 = off
    int uiTimbre = 0;
    std::vector<BrowserSpec> browsers() override;
    std::vector<ActionSpec> actions() override;
    std::shared_ptr<SampleSet> setFor (int timbre) const { return std::atomic_load (&sets[timbre]); }

private:
    struct Voice
    {
        bool active = false, released = false, killing = false;
        std::shared_ptr<SampleSet> set;
        SampleZone* zone = nullptr;
        const WavData* wav = nullptr;
        int fileIndex = -1, timbre = 0, note = 60;
        double pos = 0, step = 1;
        float pitch = 60, vel = 1, fade = 1;
        bool looping = false, endLoopOnRelease = false;
        Envelope env;
    };
    Voice voices[PolyNoteEngine::kMaxVoices];
    std::shared_ptr<SampleSet> sets[3];
    int loadedFolder[3] { -2, -2, -2 }, wantedFolder[3] { 0, -1, -1 };
    juce::Array<juce::File> folders;
    std::vector<std::shared_ptr<SampleSet>> graveyard;
    StereoDelay delay;
    Biquad bassL, bassR, trebL, trebR;
    int eqBass = 9999, eqTreb = 9999;
    double lastTap = 0;
    void timerCallback() override;
    void startVoice (const PolyNoteEngine::VoiceStart& s);
    juce::String folderName (int idx) const;
};
} // namespace ex

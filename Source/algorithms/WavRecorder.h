#pragma once
#include "../core/ExPlugin.h"

namespace ex
{
// '5 - WAV Recorder' with the auto-sampler: records the input to !RECORD/RECnnnnn.WAV, plays recordings,
// and auto-samples an external instrument (MIDI out + CV/gate on outputs 3/4) into AUTOnnnn folders.
class WavRecorder : public ExAlgorithm, private juce::Thread, private juce::Timer
{
public:
    explicit WavRecorder (ExPlugin& p);
    ~WavRecorder() override;
    void prepare (double sampleRate, int block) override;
    void process (juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, juce::MidiBuffer& midi, juce::MidiBuffer& midiOut) override;
    void allNotesOff() override {}
    void drawPage (Oled& o, int cur) override;
    bool pushL() override;
    bool pushR (int cur) override;
    void onSelected() override;
    void addMenuItems (std::vector<MenuItem>& items) override;

    std::vector<BrowserSpec> browsers() override;
    std::vector<ActionSpec> actions() override;
    juce::String statusText() override;

    // test helpers
    bool isBusy() const { return state.load() != Idle || recording.load(); }
    juce::File lastAutoFolder() const { return autoFolder; }

private:
    enum State { Idle, Preview, Sampling };
    std::atomic<int> state { Idle };
    std::atomic<bool> recording { false }, playing { false };
    std::atomic<bool> reqRecord { false }, reqStopRecord { false }, reqStartSampling { false }, reqStopSampling { false }, reqPreview { false };
    double sr = 48000;

    // --- ring buffer from the audio thread to the writer thread ---
    static constexpr int kRing = 1 << 22;
    std::vector<float> ring; // interleaved stereo
    std::atomic<int64_t> ringWrite { 0 };
    int64_t ringRead = 0;
    struct Command { int type = 0; int64_t frame = 0; char path[1024] {}; int channels = 2, bits = 16, rate = 48000; };
    std::vector<Command> cmds { 256 };
    juce::AbstractFifo cmdFifo { 256 };
    void pushCommand (int type, int64_t frame, const juce::String& path);
    void run() override; // writer thread
    std::unique_ptr<juce::AudioFormatWriter> writer;
    int writerChannels = 2; int writerRate = 48000;
    double rsPos = 0; float rsHist[2][4] {};

    // --- recording ---
    juce::File recordDir() const { return SdCard::root().getChildFile ("!RECORD"); }
    int lastRecording = 0;
    int64_t recStartFrame = 0;
    int scanLastRecording() const;

    // --- playback ---
    std::shared_ptr<WavData> playback;
    double playPos = 0;
    juce::Array<juce::File> recFiles;
    void loadPlayback();

    // --- auto-sampler ---
    struct Job { int note, vel; juce::String path; };
    std::vector<Job> jobs;
    int jobIndex = 0;
    int64_t jobStart = 0;   // sample clock at note on
    bool noteIsOn = false;
    int previewNote = 48;
    int64_t latencyMeasured = -1;
    juce::File autoFolder;
    juce::String currentFile;
    int64_t samplingStart = 0;
    void buildJobs();
    int jobsCount() const { int n = 0; for (int note = pv (17); note <= pv (18); note += juce::jmax (1, pv (19))) ++n; return n * pv (20) * pv (21); }
    void sendNote (juce::MidiBuffer& out, int pos, int note, int vel, bool on);
    int64_t noteLenSamples() const;
    int64_t gapSamples() const;

    // --- meters ---
    std::atomic<float> peak[2] { 0, 0 };
    float holdVal[2] { 0, 0 }; double holdUntil[2] { 0, 0 }, clipUntil[2] { 0, 0 };
    float meterDisp[2] { 0, 0 };
    juce::String lockedMsg;
    void timerCallback() override;
};
} // namespace ex

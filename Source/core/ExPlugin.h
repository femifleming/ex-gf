#pragma once
// Common plug-in shell: parameters, MIDI settings, menus, presets, display rendering and the P/V/L/R controls.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Oled.h"
#include "ExDsp.h"
#include "NoteEngine.h"
#include "SdCard.h"

namespace ex
{
class ExPlugin;

struct ParamDef
{
    juce::String name;
    int min = 0, max = 1, def = 0;
    juce::String unit;
    juce::StringArray labels;   // enum labels (index = value - min)
    bool confirm = false;       // needs 'PUSH V'
    std::function<juce::String (int)> fmt; // custom right-hand text
};

struct MenuItem
{
    juce::String label;
    std::vector<MenuItem> children;
    std::function<void()> action;
    bool isValue = false;
    int vmin = 0, vmax = 1;
    std::function<int()> get;
    std::function<void (int)> set;
    std::function<juce::String (int)> fmt;

    static MenuItem sub (juce::String l, std::vector<MenuItem> c) { MenuItem m; m.label = l; m.children = std::move (c); return m; }
    static MenuItem act (juce::String l, std::function<void()> a) { MenuItem m; m.label = l; m.action = std::move (a); return m; }
    static MenuItem val (juce::String l, int mn, int mx, std::function<int()> g, std::function<void (int)> s, std::function<juce::String (int)> f = {})
    {
        MenuItem m; m.label = l; m.isValue = true; m.vmin = mn; m.vmax = mx; m.get = std::move (g); m.set = std::move (s); m.fmt = std::move (f); return m;
    }
};

// UI description used by the editor
struct Section { juce::String name; std::vector<int> params; };
struct BrowserSpec
{
    juce::String title;
    std::function<juce::StringArray()> items;
    std::function<int()> selected;
    std::function<void (int)> choose;
    bool compact = false; // segmented buttons instead of a list
};
struct ActionSpec
{
    juce::String name;
    std::function<void()> run;
    std::function<bool()> lit;
    bool record = false; // red when lit
};

// MIDI note-range / per algorithm MIDI options shared by the poly algorithms
struct MidiNoteOptions { int enable = 1, lowest = 0, highest = 127; };

class ExAlgorithm
{
public:
    ExAlgorithm (ExPlugin& p, int num, juce::String nm) : plugin (p), number (num), name (std::move (nm)) {}
    virtual ~ExAlgorithm() = default;

    ExPlugin& plugin;
    const int number;
    const juce::String name;
    std::vector<ParamDef> defs;
    std::vector<juce::AudioParameterInt*> params;
    int defaultL = 0;                 // param number mapped to the L pot by default (0 = none)
    MidiNoteOptions midiNotes;
    bool delayUsesMidiClock = false;
    int paraphonicGateMode = 0;

    std::vector<Section> sections;
    virtual std::vector<BrowserSpec> browsers() { return {}; }
    virtual std::vector<ActionSpec> actions() { return {}; }
    virtual juce::String statusText() { return {}; }
    // drag & drop / import of files (e.g. wavetables); return true if handled
    virtual bool acceptsFileImport() const { return false; }
    virtual bool importFiles (const juce::StringArray& paths) { juce::ignoreUnused (paths); return false; }
    virtual juce::String importHint() const { return "into the SD card's wavetables folder"; }
    // activity cells for the top-left box (algorithms without the poly note engine)
    struct Meter { bool on = false; float level = 0; juce::String label; };
    virtual int numMeters() { return 0; }
    virtual Meter meter (int index) { juce::ignoreUnused (index); return {}; }
    virtual juce::String metersTitle() { return "VOICES"; }

    int numParams() const { return (int) defs.size(); }
    int pv (int n) const { return params[(size_t) n - 1]->get(); }   // 1-based parameter number
    const ParamDef& def (int n) const { return defs[(size_t) n - 1]; }
    void set (int n, int v);
    juce::AudioParameterInt* param (int n) { return params[(size_t) n - 1]; }

    // add a parameter (numbers are assigned in order: first call = parameter 1)
    int add (juce::String nm, int mn, int mx, int df, juce::String unit = {}, juce::StringArray labels = {})
    {
        ParamDef d; d.name = nm; d.min = mn; d.max = mx; d.def = df; d.unit = unit; d.labels = labels;
        defs.push_back (d);
        return (int) defs.size();
    }
    void addAttenuverters()
    {
        for (int i = 1; i <= 6; ++i) add ("Attenuverter " + juce::String (i), -200, 200, 100, "%");
    }

    // --- audio ---
    virtual void prepare (double sr, int maxBlock) = 0;
    // out: 4 channels (outputs 1-4), in: stereo input bus (may be silent)
    virtual void process (juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, juce::MidiBuffer& midi, juce::MidiBuffer& midiOut) = 0;
    virtual void allNotesOff() {}

    // --- display ---
    virtual void drawPage (Oled& o, int cur);
    virtual void drawBottomLine (Oled& o, int cur) { juce::ignoreUnused (o, cur); }
    virtual juce::String valueText (int n, int v) const;   // label/unit shown after the two values
    virtual juce::String headerText() const { return juce::String (number).paddedRight (' ', 3) + name; }

    // --- controls ---
    virtual bool pushL() { return false; }
    virtual bool pushR (int cur) { juce::ignoreUnused (cur); return false; }
    virtual void confirm (int n) { juce::ignoreUnused (n); }
    virtual void addMenuItems (std::vector<MenuItem>& items) { juce::ignoreUnused (items); }
    virtual void onSelected() {}

    // --- state ---
    virtual void saveExtra (juce::XmlElement& x) const { juce::ignoreUnused (x); }
    virtual void loadExtra (const juce::XmlElement& x) { juce::ignoreUnused (x); }

    // pending 'PUSH V' confirmations: applied value per parameter
    std::map<int, int> applied;
    int appliedValue (int n) const { auto it = applied.find (n); return it != applied.end() ? it->second : pv (n); }
    bool awaitingConfirm (int n) const { return def (n).confirm && appliedValue (n) != pv (n); }

    // common helper: menu items for the poly algorithms (MIDI notes, delay clock, all notes off)
    void addStandardPolyMenu (std::vector<MenuItem>& items, bool hasDelay, bool hasParaGate);
    bool acceptsNote (int note) const { return midiNotes.enable && note >= midiNotes.lowest && note <= midiNotes.highest; }
    double delayMs (int timeParamValue) const;
    int defaultR = 0;                 // param mapped to the R pot by default (0 = current parameter)

    // background loading (folders, wavetables, banks)
    void runAsync (std::function<void()> fn) { pool.addJob (std::move (fn)); }
    void stopJobs() { pool.removeAllJobs (true, 10000); }
private:
    juce::ThreadPool pool { juce::ThreadPoolOptions().withNumberOfThreads (1).withThreadName ("EX loader") };
};

class ExPlugin : public juce::AudioProcessor, private juce::Timer
{
public:
    struct Identity { juce::String product; bool midiOut = false; bool wantsInput = true; int numTimbreChannels = 1; bool midiIn = true; };
    explicit ExPlugin (Identity id);
    static inline bool rememberLastState = true;   // tests switch this off
    juce::File lastStateFile() const;
    void saveLastState();
    static BusesProperties makeBuses (bool wantsInput);
    ~ExPlugin() override;

    // subclasses: create algorithms, then call finishSetup()
    void addAlgorithm (std::unique_ptr<ExAlgorithm> a) { algs.push_back (std::move (a)); }
    void finishSetup();

    ExAlgorithm& alg() { return *algs[(size_t) current.load()]; }
    const ExAlgorithm& alg() const { return *algs[(size_t) current.load()]; }
    int numAlgorithms() const { return (int) algs.size(); }
    ExAlgorithm& algAt (int i) { return *algs[(size_t) i]; }
    void selectAlgorithm (int index);

    // ---- AudioProcessor ----
    const juce::String getName() const override { return identity.product; }
    void prepareToPlay (double sr, int bs) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return identity.midiIn; }
    bool producesMidi() const override { return identity.midiOut; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    std::unique_ptr<juce::XmlElement> stateXml() const;
    void restoreXml (const juce::XmlElement&);

    // ---- MIDI settings ----
    int rxChannel = 1;          // 0 = omni
    int txChannel = 1;
    int timbreChannel[4] { 0, 0, 0, 0 }; // 0 = default (RX + timbre index)
    // timbre for an incoming channel (1-16), or -1 if not received
    int timbreForChannel (int ch, int numTimbres) const;
    double hostBpm = 120.0;
    double sampleRate = 48000.0;
    int64_t sampleClock = 0;

    // ---- UI / controls (message thread) ----
    void render (Oled& o);
    void turnP (int d);
    void turnV (int d);
    void pushP();
    void pushV();
    void holdV();
    void setPot (int which, float v01); // 0 = L, 1 = R
    void pushPot (int which);
    float potValue[2] { 0.5f, 0.5f };
    int mapL = -1, mapR = 0;           // -1 = use algorithm default; 0 = none / current param for R
    int contrast = 255;
    int curParam[8] { 7, 7, 7, 7, 7, 7, 7, 7 };
    void message (const juce::String& text, double seconds = 1.5, bool big = false);
    std::atomic<int> midiInFlash { 0 }, midiOutFlash { 0 };
    std::atomic<bool> allNotesOffRequest { false };

    juce::MidiKeyboardState keyboardState;
    MenuItem& buildMenuTree() { buildMenu(); return menuRoot; }
    void selectParam (int n) { menuOpen = false; curParam[current.load()] = juce::jlimit (1, alg().numParams(), n); }
    int currentAlgorithmIndex() const { return current.load(); }
    void savePresetToSd();
    void loadPresetFile (const juce::File& f);
    juce::File presetDir() const;

    // file chooser for the SD card folder
    void chooseSdFolder();
    std::function<void()> onSdRootChanged;
    juce::String lastPresetName;

    const Identity identity;

protected:
    std::vector<std::unique_ptr<ExAlgorithm>> algs;
    std::atomic<int> current { 0 };

private:
    juce::AudioBuffer<float> outBuf, inBuf;
    juce::MidiBuffer midiOutBuf;
    std::vector<juce::AudioParameterInt*> allParams;

    // menu
    bool menuOpen = false;
    MenuItem menuRoot;
    std::vector<int> menuPath;   // indices into children
    bool valueEdit = false;
    MenuItem* menuNode (size_t depth);
    void buildMenu();
    void renderMenu (Oled& o);
    juce::String msgText; double msgUntil = 0; bool msgBig = false;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Array<juce::File> presetFiles;
    int loadPresetChoice = 0;
    void loadPresetFromSd (int index);
    double lastMidiClock = 0; double clockInterval = 0; int64_t clockSamples = 0;

    void timerCallback() override;
    juce::String lastSavedState;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExPlugin)
};
} // namespace ex

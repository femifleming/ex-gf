// Offline test harness: renders each algorithm, snapshots the editor / OLED, and runs the
// auto-sampler in a loopback (MIDI out -> test synth -> audio in) then plays the result back.
//   EXTest <outdir>          (set EX_SD_ROOT to choose the SD card folder)
#include "algorithms/SdMultisample.h"
#include "algorithms/WavRecorder.h"
#include "algorithms/PolyWavetable.h"
#include "algorithms/PolyFM.h"
#include "algorithms/PolyExciter.h"
#include "algorithms/Granulator.h"
#include "algorithms/MultiFX.h"
#include "algorithms/Resonator.h"
#include "algorithms/SpectralFreeze.h"
#include "algorithms/MacroOsc2.h"
#include "core/ExEditor.h"

using namespace ex;

static void setEnv (const char* k, const juce::String& v)
{
   #if JUCE_WINDOWS
    _putenv_s (k, v.toRawUTF8());
   #else
    setenv (k, v.toRawUTF8(), 1);
   #endif
}

static void writeTestWav (const juce::File& f, const std::vector<float>& d, double sr = 48000)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    juce::WavAudioFormat fmt;
    auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (1).withBitsPerSample (16));
    const float* ch[1] = { d.data() };
    if (w) w->writeFromFloatArrays (ch, 1, (int) d.size());
}

// a small synthetic SD card so the tests run anywhere (CI, Windows, Linux)
static juce::File makeSyntheticCard (juce::File root = {})
{
    if (root == juce::File()) root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("EXTest-SD");
    root.deleteRecursively();
    auto ms = root.getChildFile ("Test Sine");
    ms.createDirectory();
    for (int note : { 48, 60, 72 })
        for (int v = 1; v <= 2; ++v)
        {
            std::vector<float> d (48000);
            double hz = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            for (size_t i = 0; i < d.size(); ++i) d[i] = (float) (0.4 * v * std::sin (2 * juce::MathConstants<double>::pi * hz * (double) i / 48000.0) * std::exp (-(double) i / 30000.0));
            writeTestWav (ms.getChildFile ("sine_" + noteName (note) + "_V" + juce::String (v) + ".wav"), d);
        }
    auto wt = root.getChildFile ("wavetables").getChildFile ("Test Table");
    wt.createDirectory();
    for (int k = 0; k < 4; ++k)
    {
        std::vector<float> d (600);
        for (int i = 0; i < 600; ++i) { double ph = i / 600.0; d[(size_t) i] = (float) (k % 2 ? 2 * ph - 1 : std::sin (2 * juce::MathConstants<double>::pi * ph * (k + 1))); }
        writeTestWav (wt.getChildFile ("w" + juce::String (k) + ".wav"), d);
    }
    auto ex = root.getChildFile ("exciters").getChildFile ("Test Noise");
    ex.createDirectory();
    juce::Random r (3);
    for (int k = 0; k < 3; ++k)
    {
        std::vector<float> d ((size_t) (2000 * (k + 1)));
        for (auto& v : d) v = r.nextFloat() * 2 - 1;
        writeTestWav (ex.getChildFile ("n" + juce::String (k) + ".wav"), d);
    }
    return root;
}

struct TestPlugin : ExPlugin
{
    TestPlugin (Identity id, std::function<void (TestPlugin&)> fill) : ExPlugin (std::move (id)) { fill (*this); finishSetup(); }
};

static std::unique_ptr<TestPlugin> make (const juce::String& kind)
{
    if (kind == "ms") return std::make_unique<TestPlugin> (ExPlugin::Identity { "EX Multisample", true, true, 3 }, [] (TestPlugin& p) {
        p.addAlgorithm (std::make_unique<SdMultisample> (p)); p.addAlgorithm (std::make_unique<WavRecorder> (p)); });
    if (kind == "wt") return std::make_unique<TestPlugin> (ExPlugin::Identity { "EX Poly Wavetable", false, false, 1 }, [] (TestPlugin& p) {
        p.addAlgorithm (std::make_unique<PolyWavetable> (p)); });
    if (kind == "fm") return std::make_unique<TestPlugin> (ExPlugin::Identity { "EX Poly FM", true, false, 4 }, [] (TestPlugin& p) {
        p.addAlgorithm (std::make_unique<PolyFM> (p)); });
    if (kind == "gr") return std::make_unique<TestPlugin> (ExPlugin::Identity { "EX Granulator", false, true, 1 }, [] (TestPlugin& p) {
        p.addAlgorithm (std::make_unique<Granulator> (p)); });
    if (kind == "mx") return std::make_unique<TestPlugin> (ExPlugin::Identity { "EX Multi-FX", false, true, 1, false }, [] (TestPlugin& p) {
        p.addAlgorithm (std::make_unique<MultiFX> (p)); });
    if (kind == "rs") return std::make_unique<TestPlugin> (ExPlugin::Identity { "EX Resonator", false, true, 1 }, [] (TestPlugin& p) {
        p.addAlgorithm (std::make_unique<Resonator> (p)); });
    if (kind == "sf") return std::make_unique<TestPlugin> (ExPlugin::Identity { "EX Spectral Freeze", false, true, 1 }, [] (TestPlugin& p) {
        p.addAlgorithm (std::make_unique<SpectralFreeze> (p)); });
    if (kind == "mo") return std::make_unique<TestPlugin> (ExPlugin::Identity { "EX Macro Oscillator 2", false, false, 1 }, [] (TestPlugin& p) {
        p.addAlgorithm (std::make_unique<MacroOsc2> (p)); });
    return std::make_unique<TestPlugin> (ExPlugin::Identity { "EX Poly Exciter", false, true, 1 }, [] (TestPlugin& p) {
        p.addAlgorithm (std::make_unique<PolyExciter> (p)); });
}

// test signal for the effects: a plucked chord every half second (harmonics + a noise burst), so there are
// transients for Rings' onset detector and steady tones for the freeze
static std::function<void (const juce::MidiBuffer&, juce::AudioBuffer<float>&, int64_t)> testInput (double until = 1.0e9, double sr = 48000)
{
    auto rnd = std::make_shared<juce::Random> (7);
    return [=] (const juce::MidiBuffer&, juce::AudioBuffer<float>& in, int64_t pos) {
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const double t = (double) (pos + in.getNumSamples() + i) / sr; // the block that will be processed next
            if (t > until) break;
            const double ph = std::fmod (t, 0.5), env = std::exp (-ph * 5.0);
            double v = 0;
            for (double hz : { 110.0, 220.0, 277.2, 329.6, 440.0 }) v += std::sin (2 * juce::MathConstants<double>::pi * hz * t);
            v = 0.08 * v * env + (ph < 0.01 ? 0.3 * (rnd->nextDouble() * 2 - 1) : 0.0);
            in.setSample (0, i, (float) v); in.setSample (1, i, (float) v);
        }
    };
}

static void pump (int ms)
{
    auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

struct Stats { float peak = 0; double rms = 0; bool nan = false; };

// renders `seconds`, with MIDI events supplied by `events(blockStartSample, n, midi)`
static Stats render (ExPlugin& p, double seconds, std::function<void (int64_t, int, juce::MidiBuffer&)> events,
                     juce::AudioBuffer<float>* capture, std::function<void (const juce::MidiBuffer&, juce::AudioBuffer<float>&, int64_t)> loop = {})
{
    const int bs = 256; const double sr = 48000;
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> buf (p.getTotalNumInputChannels() > 0 ? juce::jmax (2, p.getTotalNumOutputChannels()) : p.getTotalNumOutputChannels(), bs);
    juce::AudioBuffer<float> nextInput (2, bs);
    nextInput.clear();
    Stats st; double sum = 0; int64_t cnt = 0;
    if (capture) capture->setSize (2, total);
    for (int pos = 0; pos < total; pos += bs)
    {
        int n = juce::jmin (bs, total - pos);
        juce::AudioBuffer<float> b (buf.getArrayOfWritePointers(), buf.getNumChannels(), n);
        b.clear();
        if (p.getTotalNumInputChannels() > 0) for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, nextInput, c, 0, n);
        juce::MidiBuffer midi;
        if (events) events (pos, n, midi);
        p.processBlock (b, midi);
        if (loop) { nextInput.clear(); loop (midi, nextInput, pos); }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                float v = b.getSample (c, i);
                if (! std::isfinite (v)) st.nan = true;
                st.peak = std::max (st.peak, std::abs (v)); sum += v * v; ++cnt;
                if (capture) capture->setSample (c, pos + i, v);
            }
    }
    st.rms = std::sqrt (sum / juce::jmax ((int64_t) 1, cnt));
    return st;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    juce::WavAudioFormat fmt;
    auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static void snapshot (ExPlugin& p, const juce::File& f)
{
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (ExEditor::baseW, ExEditor::baseH);
    pump (120);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
}

static std::function<void (int64_t, int, juce::MidiBuffer&)> chordEvents (std::vector<int> notes, double onAt, double offAt, int ch = 1)
{
    return [=] (int64_t pos, int n, juce::MidiBuffer& m) {
        int64_t on = (int64_t) (onAt * 48000), off = (int64_t) (offAt * 48000);
        for (int note : notes)
        {
            if (on >= pos && on < pos + n) m.addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) 100), (int) (on - pos));
            if (off >= pos && off < pos + n) m.addEvent (juce::MidiMessage::noteOff (ch, note), (int) (off - pos));
        }
    };
}

// Sets every parameter of every algorithm to a value that is not its default, and every other saved setting too.
// Different `variant`s give different values. Returns how many parameters now differ from their default.
static int setEverything (ExPlugin& p, int variant)
{
    int changed = 0, i = 0;
    for (int ai = 0; ai < p.numAlgorithms(); ++ai)
    {
        auto& a = p.algAt (ai);
        for (int n = 1; n <= a.numParams(); ++n, ++i)
        {
            const auto& d = a.def (n);
            const int range = d.max - d.min;
            if (range <= 0) continue;
            const int v = d.min + (d.def - d.min + variant + (i * 7 * variant) % range) % (range + 1);
            a.set (n, v);
            if (d.confirm) { a.applied[n] = a.pv (n); a.confirm (n); }
            if (a.pv (n) != d.def) ++changed;
        }
        a.midiNotes.enable = 1; a.midiNotes.lowest = 10 + variant + ai; a.midiNotes.highest = 100 + variant + ai;
        a.delayUsesMidiClock = true; a.paraphonicGateMode = 1;
        p.curParam[ai] = juce::jmin (a.numParams(), 9 + variant + ai);
        p.uiSection[ai] = 1 + variant;
    }
    p.rxChannel = 3 + variant; p.txChannel = 5 + variant; p.contrast = 200 - variant;
    p.mapL = 8 + variant; p.mapR = 9 + variant; p.potValue[0] = 0.25f * (float) variant; p.potValue[1] = 0.125f * (float) variant;
    for (int t = 0; t < 4; ++t) p.timbreChannel[t] = 4 + t + variant;
    p.editorW = 540 * (1 + variant); p.editorH = 300 * (1 + variant);
    return changed;
}

static juce::String auditKind (const juce::String& name);
static juce::String auditHostName (ExPlugin& p, int ai, int n);
static juce::AudioProcessorParameter* auditFind (juce::AudioPluginInstance& inst, const juce::String& name);

// every parameter of a hosted instance against the defaults in the source; returns the ones that differ
static juce::StringArray hostedNonDefaults (juce::AudioPluginInstance& inst, ExPlugin& ref)
{
    juce::StringArray wrong;
    for (int ai = 0; ai < ref.numAlgorithms(); ++ai)
        for (int n = 1; n <= ref.algAt (ai).numParams(); ++n)
        {
            const auto& d = ref.algAt (ai).def (n);
            const auto name = auditHostName (ref, ai, n);
            auto* hp = auditFind (inst, name);
            const float norm = d.max > d.min ? (float) ((double) (d.def - d.min) / (double) (d.max - d.min)) : 0.0f;
            if (hp == nullptr || std::abs (hp->getValue() - norm) > 1.0e-5f || std::abs (hp->getDefaultValue() - norm) > 1.0e-5f) wrong.add (name);
        }
    return wrong;
}

// Loads built plug-ins exactly like a DAW does: plays notes, opens the editor, checks that a new instance is on its
// defaults, and saves a state with every parameter changed and loads it into a second instance.
static int hostTest (const juce::StringArray& paths)
{
    int failures = 0;
    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);
    for (auto& path : paths)
    {
        juce::OwnedArray<juce::PluginDescription> descs;
        for (auto* f : fm.getFormats())
            if (f->fileMightContainThisPluginType (path)) f->findAllTypesForFile (descs, path);
        if (descs.isEmpty()) { std::cout << "FAIL host: no plug-in found in " << path << std::endl; ++failures; continue; }
        juce::String err;
        auto inst = fm.createPluginInstance (*descs[0], 48000, 256, err);
        if (! inst) { std::cout << "FAIL host: " << path << " " << err << std::endl; ++failures; continue; }
        inst->enableAllBuses();
        inst->prepareToPlay (48000, 256);
        juce::AudioBuffer<float> buf (juce::jmax (inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels()), 256);
        float peak = 0; bool nan = false;
        for (int b = 0; b < 400; ++b)
        {
            buf.clear();
            juce::MidiBuffer m;
            // effects get something to chew on (instruments overwrite it)
            if (inst->getTotalNumInputChannels() > 0)
                for (int c = 0; c < juce::jmin (2, buf.getNumChannels()); ++c)
                    for (int i = 0; i < 256; ++i) buf.setSample (c, i, 0.2f * (float) std::sin (0.03 * (double) (b * 256 + i)) * ((b / 20) % 2 ? 0.2f : 1.0f));
            if (b == 2) for (int n : { 48, 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 10);
            if (b == 200) for (int n : { 48, 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            inst->processBlock (buf, m);
            for (int c = 0; c < buf.getNumChannels(); ++c)
                for (int i = 0; i < 256; ++i) { float v = buf.getSample (c, i); nan |= ! std::isfinite (v); peak = std::max (peak, std::abs (v)); }
            if (b == 1) pump (1500); // let sample folders / tables load
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        bool edOk = ed != nullptr && ed->getWidth() > 100;
        pump (200);
        juce::MemoryBlock st; inst->getStateInformation (st);
        inst->setStateInformation (st.getData(), (int) st.getSize());
        juce::MemoryBlock st2; inst->getStateInformation (st2);
        bool stateOk = st.getSize() > 100 && st == st2;
        ed.reset();

        // defaults, then: change every parameter -> save -> a second instance loads it -> a third is on defaults again
        auto ref = make (auditKind (descs[0]->name));
        auto another = [&] { juce::String e; auto i = fm.createPluginInstance (*descs[0], 48000, 256, e); if (i) i->prepareToPlay (48000, 256); return i; };
        auto run = [&] (juce::AudioPluginInstance& i, int blocks) {
            juce::AudioBuffer<float> b2 (juce::jmax (2, i.getTotalNumInputChannels(), i.getTotalNumOutputChannels()), 256);
            for (int b = 0; b < blocks; ++b) { b2.clear(); juce::MidiBuffer m; i.processBlock (b2, m); } };
        auto second = another();
        juce::StringArray notDefault { "no second instance" }, lost { "no second instance" }, notClean { "no third instance" };
        int total = 0, moved = 0;
        if (second)
        {
            notDefault = hostedNonDefaults (*second, *ref);
            int k = 0;
            for (int ai = 0; ai < ref->numAlgorithms(); ++ai)
                for (int n = 1; n <= ref->algAt (ai).numParams(); ++n, ++k)
                {
                    const auto& d = ref->algAt (ai).def (n);
                    const int range = d.max - d.min;
                    auto* hp = auditFind (*inst, auditHostName (*ref, ai, n));
                    if (range <= 0 || hp == nullptr) continue;
                    const int v = d.min + (d.def - d.min + 1 + (k * 7) % range) % (range + 1);
                    hp->setValue ((float) ((double) (v - d.min) / (double) range));
                }
            run (*inst, 8); pump (300); run (*inst, 2);
            juce::MemoryBlock changed;
            inst->getStateInformation (changed);
            second->setStateInformation (changed.getData(), (int) changed.getSize());
            run (*second, 4); pump (300); run (*second, 2);
            lost.clear();
            for (int ai = 0; ai < ref->numAlgorithms(); ++ai)
                for (int n = 1; n <= ref->algAt (ai).numParams(); ++n)
                {
                    const auto& d = ref->algAt (ai).def (n);
                    const auto name = auditHostName (*ref, ai, n);
                    auto* a = auditFind (*inst, name); auto* b2 = auditFind (*second, name);
                    if (a == nullptr || b2 == nullptr) { lost.add (name); continue; }
                    ++total;
                    const float norm = d.max > d.min ? (float) ((double) (d.def - d.min) / (double) (d.max - d.min)) : 0.0f;
                    if (std::abs (a->getValue() - norm) > 1.0e-5f) ++moved;
                    if (std::abs (a->getValue() - b2->getValue()) > 1.0e-5f) lost.add (name);
                }
            second->releaseResources();
            second.reset();
        }
        inst->releaseResources();
        inst.reset();
        pump (100);
        if (auto third = another()) { notClean = hostedNonDefaults (*third, *ref); third->releaseResources(); }
        const bool defaultsOk = notDefault.isEmpty(), roundTripOk = lost.isEmpty() && moved == total && total > 0, cleanOk = notClean.isEmpty();
        bool ok = peak > 0.01f && ! nan && edOk && stateOk && defaultsOk && roundTripOk && cleanOk;
        if (! ok) ++failures;
        std::cout << (ok ? "PASS" : "FAIL") << " host " << descs[0]->pluginFormatName << " " << descs[0]->name << "  peak=" << peak
                  << " editor=" << (edOk ? "ok" : "missing") << " state=" << (stateOk ? "ok" : "mismatch")
                  << " defaults=" << (defaultsOk ? juce::String ("ok") : "NOT " + notDefault.joinIntoString (", ").substring (0, 120))
                  << " preset=" << (roundTripOk ? "ok (" + juce::String (moved) + " parameters)" : "LOST " + juce::String (lost.size()) + " of " + juce::String (total) + " (" + juce::String (moved) + " changed) " + lost.joinIntoString (", ").substring (0, 120))
                  << " next-instance=" << (cleanOk ? juce::String ("defaults") : "NOT DEFAULT " + notClean.joinIntoString (", ").substring (0, 120)) << std::endl;
    }
    std::cout << (failures == 0 ? "HOST TESTS PASSED" : juce::String (failures) + " HOST FAILURES") << std::endl;
    return failures;
}

// ------------------------------------------------------------------------------------------------
// Audit of one built plug-in in one format, one process per call (so every instance really is fresh):
//   EXTest --audit <plug-in> <out.json> [--state-in <file>] [--set-all] [--set n=v,n=v] [--state-out <file>] [--render <file.wav>] [--aux] [--midi]
// Reports every parameter (value, default, text) against the defaults in the source, the bus layouts,
// and a render with different signals on the left and right inputs. tools/audit.py drives it.
static juce::String auditKind (const juce::String& name)
{
    if (name.contains ("Multisample")) return "ms";
    if (name.contains ("Wavetable")) return "wt";
    if (name.contains ("Poly FM")) return "fm";
    if (name.contains ("Exciter")) return "ex";
    if (name.contains ("Granulator")) return "gr";
    if (name.contains ("Multi-FX")) return "mx";
    if (name.contains ("Resonator")) return "rs";
    if (name.contains ("Spectral")) return "sf";
    if (name.contains ("Macro")) return "mo";
    return {};
}

static void auditRun (juce::AudioPluginInstance& inst, juce::AudioBuffer<float>& buf, int blocks)
{
    for (int b = 0; b < blocks; ++b) { buf.clear(); juce::MidiBuffer m; inst.processBlock (buf, m); }
}

// the plug-in's own parameters: JUCE's VST3 wrapper adds a bypass and 2080 hidden MIDI CC parameters after them
static bool auditOwnParam (juce::AudioProcessorParameter& p)
{
    const auto name = p.getName (64);
    return ! name.startsWith ("MIDI CC ") && name != "Bypass";
}

// the name a host shows for parameter n of algorithm ai (see ExPlugin::finishSetup)
static juce::String auditHostName (ExPlugin& p, int ai, int n)
{
    auto& a = p.algAt (ai);
    return (p.numAlgorithms() > 1 ? juce::String (a.number) + "." : juce::String()) + juce::String (n) + " " + a.def (n).name;
}

// hosts list Audio Unit parameters in their own order, so parameters are always looked up by name
static juce::AudioProcessorParameter* auditFind (juce::AudioPluginInstance& inst, const juce::String& name)
{
    for (auto* p : inst.getParameters()) if (p->getName (64) == name) return p;
    return nullptr;
}

static juce::var auditParams (juce::AudioPluginInstance& inst)
{
    juce::Array<juce::var> out;
    for (auto* p : inst.getParameters())
    {
        if (! auditOwnParam (*p)) continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", p->getName (64));
        o->setProperty ("value", (double) p->getValue());
        o->setProperty ("default", (double) p->getDefaultValue());
        o->setProperty ("text", p->getCurrentValueAsText());
        o->setProperty ("steps", p->getNumSteps());
        out.add (juce::var (o));
    }
    return out;
}

static int audit (const juce::StringArray& args)
{
    const juce::String path = juce::File (args[0]).getFullPathName();
    const juce::File outFile (args[1]);
    auto opt = [&] (const char* name) { int i = args.indexOf (name); return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String(); };
    auto* root = new juce::DynamicObject();
    juce::var rootVar (root);
    auto finish = [&] (const juce::String& error) {
        if (error.isNotEmpty()) root->setProperty ("error", error);
        outFile.replaceWithText (juce::JSON::toString (rootVar));
        return error.isEmpty() ? 0 : 1;
    };

    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);
    juce::OwnedArray<juce::PluginDescription> descs;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType (path)) f->findAllTypesForFile (descs, path);
    if (descs.isEmpty()) return finish ("no plug-in found in " + path);
    juce::String err;
    auto inst = fm.createPluginInstance (*descs[0], 48000, 256, err);
    if (! inst) return finish ("could not create: " + err);
    const bool instrument = descs[0]->isInstrument;
    root->setProperty ("format", descs[0]->pluginFormatName);
    root->setProperty ("name", descs[0]->name);
    root->setProperty ("version", descs[0]->version);
    root->setProperty ("instrument", instrument);
    root->setProperty ("acceptsMidi", inst->acceptsMidi());
    root->setProperty ("programs", inst->getNumPrograms());

    // buses and the layouts a host may ask for
    {
        juce::Array<juce::var> buses;
        for (int dir = 0; dir < 2; ++dir)
            for (int i = 0; i < inst->getBusCount (dir == 0); ++i)
            {
                auto* b = inst->getBus (dir == 0, i);
                auto* o = new juce::DynamicObject();
                o->setProperty ("dir", dir == 0 ? "in" : "out"); o->setProperty ("name", b->getName());
                o->setProperty ("channels", b->getNumberOfChannels()); o->setProperty ("enabled", b->isEnabled());
                o->setProperty ("default", b->getDefaultLayout().getDescription());
                buses.add (juce::var (o));
            }
        root->setProperty ("buses", buses);
        auto supports = [&] (juce::AudioChannelSet in, juce::AudioChannelSet out) {
            auto l = inst->getBusesLayout();
            if (! l.inputBuses.isEmpty()) l.inputBuses.getReference (0) = in;
            l.outputBuses.getReference (0) = out;
            return inst->checkBusesLayoutSupported (l);
        };
        auto* lo = new juce::DynamicObject();
        lo->setProperty ("stereo>stereo", supports (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo()));
        lo->setProperty ("mono>stereo", supports (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo()));
        lo->setProperty ("mono>mono", supports (juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono()));
        lo->setProperty ("stereo>mono", supports (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono()));
        root->setProperty ("layouts", juce::var (lo));
    }
    root->setProperty ("inChannels", inst->getTotalNumInputChannels());
    root->setProperty ("outChannels", inst->getTotalNumOutputChannels());
    root->setProperty ("mainOutChannels", inst->getBusCount (false) > 0 ? inst->getBus (false, 0)->getNumberOfChannels() : 0);

    // the defaults the source defines, as normalised values, in host parameter order
    {
        juce::Array<juce::var> expected;
        if (auto kind = auditKind (descs[0]->name); kind.isNotEmpty())
        {
            auto ref = make (kind);
            for (int ai = 0; ai < ref->numAlgorithms(); ++ai)
            {
                auto& a = ref->algAt (ai);
                for (int n = 1; n <= a.numParams(); ++n)
                {
                    const auto& d = a.def (n);
                    auto* o = new juce::DynamicObject();
                    o->setProperty ("alg", a.number); o->setProperty ("n", n); o->setProperty ("name", auditHostName (*ref, ai, n));
                    o->setProperty ("min", d.min); o->setProperty ("max", d.max); o->setProperty ("def", d.def);
                    o->setProperty ("norm", d.max > d.min ? (double) (d.def - d.min) / (double) (d.max - d.min) : 0.0);
                    expected.add (juce::var (o));
                }
            }
        }
        root->setProperty ("expected", expected);
    }

    const bool aux = args.contains ("--aux") && inst->enableAllBuses();   // also render Out 3/4
    root->setProperty ("aux", aux);
    inst->prepareToPlay (48000, 256);
    juce::AudioBuffer<float> buf (juce::jmax (2, inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels()), 256);
    auditRun (*inst, buf, 4);
    pump (1500);                         // sample folders, tables and banks load in the background
    auditRun (*inst, buf, 4);

    if (auto f = opt ("--state-in"); f.isNotEmpty())
    {
        juce::MemoryBlock mb;
        if (! juce::File (f).loadFileAsData (mb)) return finish ("cannot read " + f);
        inst->setStateInformation (mb.getData(), (int) mb.getSize());
        auditRun (*inst, buf, 4);
        pump (1500);
        auditRun (*inst, buf, 4);
    }
    root->setProperty ("params", auditParams (*inst));

    auto ref = make (auditKind (descs[0]->name));
    if (args.contains ("--set-all"))
    {
        // every parameter to a value that is not its default (and differs from one parameter to the next)
        int i = 0;
        for (int ai = 0; ai < ref->numAlgorithms(); ++ai)
            for (int n = 1; n <= ref->algAt (ai).numParams(); ++n, ++i)
            {
                const auto& d = ref->algAt (ai).def (n);
                const int range = d.max - d.min;
                auto* hp = auditFind (*inst, auditHostName (*ref, ai, n));
                if (range <= 0 || hp == nullptr) continue;
                const int v = d.min + (d.def - d.min + 1 + (i * 7) % range) % (range + 1);
                hp->setValue ((float) ((double) (v - d.min) / (double) range));
            }
    }
    if (auto setList = opt ("--set"); setList.isNotEmpty())
    {
        // --set n=v,...: parameter numbers and plain values of the first algorithm
        for (auto& item : juce::StringArray::fromTokens (setList, ",", {}))
        {
            const int n = item.upToFirstOccurrenceOf ("=", false, false).getIntValue(), v = item.fromFirstOccurrenceOf ("=", false, false).getIntValue();
            auto* hp = n >= 1 && n <= ref->algAt (0).numParams() ? auditFind (*inst, auditHostName (*ref, 0, n)) : nullptr;
            if (hp == nullptr) return finish ("bad --set " + item);
            const auto& d = ref->algAt (0).def (n);
            hp->setValue ((float) ((double) (v - d.min) / (double) (d.max - d.min)));
        }
    }
    if (args.contains ("--set-all") || opt ("--set").isNotEmpty())
    {
        auditRun (*inst, buf, 8);
        pump (800);
        auditRun (*inst, buf, 4);
        root->setProperty ("paramsAfterSet", auditParams (*inst));
    }
    if (auto f = opt ("--state-out"); f.isNotEmpty())
    {
        juce::MemoryBlock mb;
        inst->getStateInformation (mb);
        juce::File (f).replaceWithData (mb.getData(), mb.getSize());
        root->setProperty ("stateBytes", (int) mb.getSize());
    }

    if (auto wav = opt ("--render"); wav.isNotEmpty())
    {
        // instruments: a chord for 2 s of 3.  effects: 2 s left input only, 2 s right only, 2 s both (different
        // material on each side), and with --midi a chord in the last part
        const double sr = 48000; const int bs = 256;
        const int total = (int) ((instrument ? 3.0 : 6.0) * sr);
        const double chordOn = instrument ? 0.1 : 4.1, chordOff = instrument ? 2.0 : 5.5;
        const int numIn = inst->getTotalNumInputChannels();
        const int auxCh = aux && inst->getTotalNumOutputChannels() >= 4 ? 2 : -1;   // first channel of Out 3/4
        double auxSum[2] { 0, 0 }; bool auxNan = false;
        juce::AudioBuffer<float> cap (2, total);
        cap.clear();
        uint32_t seedL = 1234567u, seedR = 7654321u;
        auto noise = [] (uint32_t& s) { s = s * 1664525u + 1013904223u; return (float) (s >> 8) * (2.0f / 16777216.0f) - 1.0f; };
        bool nan = false;
        for (int pos = 0; pos < total; pos += bs)
        {
            const int n = juce::jmin (bs, total - pos);
            juce::AudioBuffer<float> b (buf.getArrayOfWritePointers(), buf.getNumChannels(), n);
            b.clear();
            if (numIn > 0 && ! instrument)
                for (int i = 0; i < n; ++i)
                {
                    const double t = (double) (pos + i) / sr;
                    const double pl = std::fmod (t, 0.5), pr = std::fmod (t, 0.37);
                    const float nl = noise (seedL), nr = noise (seedR);
                    float l = (float) (0.25 * std::exp (-pl * 5.0) * (std::sin (2 * juce::MathConstants<double>::pi * 220.0 * t) + std::sin (2 * juce::MathConstants<double>::pi * 277.2 * t))) + (pl < 0.01 ? 0.2f * nl : 0.0f);
                    float r = (float) (0.25 * std::exp (-pr * 5.0) * (std::sin (2 * juce::MathConstants<double>::pi * 330.0 * t) + std::sin (2 * juce::MathConstants<double>::pi * 415.3 * t))) + (pr < 0.01 ? 0.2f * nr : 0.0f);
                    if (t < 2.0) r = 0; else if (t < 4.0) l = 0;
                    b.setSample (0, i, l);
                    if (numIn > 1) b.setSample (1, i, r);
                }
            juce::MidiBuffer midi;
            const int64_t on = (int64_t) (chordOn * sr), off = (int64_t) (chordOff * sr);
            if (instrument || args.contains ("--midi"))   // effects only get the notes when asked: not every format passes them MIDI
              for (int note : { 48, 55, 60, 64 })
              {
                if (on >= pos && on < pos + n) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), (int) (on - pos));
                if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, note), (int) (off - pos));
              }
            inst->processBlock (b, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float v = b.getSample (c, i);
                    if (! std::isfinite (v)) nan = true;
                    cap.setSample (c, pos + i, std::isfinite (v) ? v : 0.0f);
                    if (auxCh >= 0)
                    {
                        const float x = b.getSample (auxCh + c, i);
                        if (std::isfinite (x)) auxSum[c] += (double) x * x; else auxNan = true;
                    }
                }
        }
        auto stats = [&] (int from, int to) {
            double sl = 0, sr2 = 0, slr = 0, diff = 0; float pk = 0;
            for (int i = from; i < to; ++i)
            {
                const double l = cap.getSample (0, i), r = cap.getSample (1, i);
                sl += l * l; sr2 += r * r; slr += l * r; diff = std::max (diff, std::abs (l - r));
                pk = std::max (pk, (float) std::max (std::abs (l), std::abs (r)));
            }
            auto* o = new juce::DynamicObject();
            const double nn = juce::jmax (1, to - from);
            o->setProperty ("rmsL", std::sqrt (sl / nn)); o->setProperty ("rmsR", std::sqrt (sr2 / nn));
            o->setProperty ("corr", sl > 0 && sr2 > 0 ? slr / std::sqrt (sl * sr2) : 0.0);
            o->setProperty ("maxLRdiff", diff); o->setProperty ("peak", pk);
            return juce::var (o);
        };
        auto* ro = new juce::DynamicObject();
        ro->setProperty ("nan", nan);
        if (auxCh >= 0)
        {
            ro->setProperty ("auxRms3", std::sqrt (auxSum[0] / total)); ro->setProperty ("auxRms4", std::sqrt (auxSum[1] / total));
            ro->setProperty ("auxNan", auxNan);
        }
        ro->setProperty ("all", stats (0, total));
        if (! instrument)
        {
            ro->setProperty ("leftOnly", stats (0, (int) (2 * sr)));
            ro->setProperty ("rightOnly", stats ((int) (2 * sr), (int) (4 * sr)));
            ro->setProperty ("both", stats ((int) (4 * sr), total));
        }
        uint64_t hash = 1469598103934665603ull;   // FNV-1a over both channels
        for (int c = 0; c < 2; ++c)
        {
            auto* bytes = reinterpret_cast<const uint8_t*> (cap.getReadPointer (c));
            for (size_t i = 0; i < sizeof (float) * (size_t) total; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
        }
        ro->setProperty ("hash", juce::String::toHexString ((juce::int64) hash));
        root->setProperty ("render", juce::var (ro));
        juce::File wf (wav);
        wf.deleteFile();
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (wf);
        juce::WavAudioFormat fmt;
        if (auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (32)
                                                  .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)))
            w->writeFromAudioSampleBuffer (cap, 0, total);
    }
    inst->releaseResources();
    inst.reset();
    return finish ({});
}

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[1]) == "--make-card")
    {
        // writes the small test SD card to a folder (for tools/reaper_audit.py)
        juce::ScopedJuceInitialiser_GUI gui;
        std::cout << makeSyntheticCard (juce::File (juce::String (argv[2]))).getFullPathName() << std::endl;
        return 0;
    }
    if (argc > 3 && juce::String (argv[1]) == "--audit")
    {
        juce::ScopedJuceInitialiser_GUI gui;
        if (juce::SystemStats::getEnvironmentVariable ("EX_SD_ROOT", {}).isEmpty())
            setEnv ("EX_SD_ROOT", makeSyntheticCard().getFullPathName());
        juce::StringArray args;
        for (int i = 2; i < argc; ++i) args.add (juce::String (argv[i]));
        return audit (args);
    }
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
    {
        // README screenshots: play a chord in each plug-in and snapshot the editor (nothing is written to the card)
        juce::ScopedJuceInitialiser_GUI gui;
        juce::File out { juce::String (argv[2]) };
        out.createDirectory();
        for (juce::String kind : { "ms", "rec", "wt", "fm", "ex", "gr", "mx", "rs", "sf", "mo" })
        {
            auto p = make (kind == "rec" ? juce::String ("ms") : kind);
            p->prepareToPlay (48000, 256);
            if (kind == "rec") p->selectAlgorithm (1);
            if (kind == "fm") { p->alg().set (8, 5); }
            if (kind == "wt") { p->alg().set (7, 6); p->alg().applied[7] = 6; p->alg().confirm (7); p->alg().set (8, 35); }
            if (kind == "gr") { p->alg().set (11, 1); p->alg().set (42, 1); p->alg().set (13, 30); }
            if (kind == "mx") { dynamic_cast<MultiFX&> (p->alg()).loadProgram (4); }
            if (kind == "rs") { p->alg().set (7, 1); p->alg().set (9, 2); }
            if (kind == "mo") { p->alg().set (7, 18); p->alg().set (10, 20); }
            pump (2500);
            render (*p, 1.6, chordEvents ({ 48, 55, 60, 64 }, 0.05, 5.0), nullptr, testInput());
            if (kind == "sf")
            {
                p->alg().set (10, 1); p->alg().set (12, 1); p->alg().set (16, 24); p->alg().set (15, 60);
                render (*p, 0.8, {}, nullptr, testInput());
            }
            snapshot (*p, out.getChildFile (kind + ".png"));
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "--dump-params")
    {
        // writes every algorithm's parameters (number, name, range, default, unit, value names, section) as JSON
        juce::ScopedJuceInitialiser_GUI gui;
        juce::Array<juce::var> algsOut;
        for (juce::String kind : { "ms", "wt", "fm", "ex", "gr", "mx", "rs", "sf", "mo" })
        {
            auto p = make (kind);
            for (int ai = 0; ai < p->numAlgorithms(); ++ai)
            {
                auto& a = p->algAt (ai);
                auto* ao = new juce::DynamicObject();
                ao->setProperty ("plugin", p->identity.product);
                ao->setProperty ("number", a.number);
                ao->setProperty ("name", a.name);
                juce::Array<juce::var> params;
                for (int n = 1; n <= a.numParams(); ++n)
                {
                    const auto& d = a.def (n);
                    auto* po = new juce::DynamicObject();
                    po->setProperty ("n", n); po->setProperty ("name", d.name);
                    po->setProperty ("min", d.min); po->setProperty ("max", d.max); po->setProperty ("def", d.def);
                    po->setProperty ("unit", d.unit);
                    juce::String section;
                    for (auto& sec : a.sections) for (int q : sec.params) if (q == n) section = sec.name;
                    po->setProperty ("section", section);
                    juce::StringArray vals;
                    if (! d.labels.isEmpty()) vals = d.labels;
                    else if (d.fmt && d.max - d.min <= 20)
                        for (int v = d.min; v <= d.max; ++v) { auto t = d.fmt (v); if (t.isNotEmpty() && t != "ms" && t != juce::String (v)) vals.add (t); }
                    if (vals.size() == d.max - d.min + 1) po->setProperty ("values", vals.joinIntoString ("|"));
                    params.add (juce::var (po));
                }
                ao->setProperty ("params", params);
                algsOut.add (juce::var (ao));
            }
        }
        juce::File (juce::String (argv[2])).replaceWithText (juce::JSON::toString (juce::var (algsOut)));
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--check-card")
    {
        // loads every multisample folder and wavetable on the SD card (EX_SD_ROOT or the default) and reports problems
        juce::ScopedJuceInitialiser_GUI gui;
        int bad = 0;
        for (auto& d : SdCard::sampleFolders())
        {
            auto set = SampleSet::load (d);
            int files = 0; for (auto& z : set->zones) for (auto& l : z.layers) files += (int) l.size();
            bool ok = ! set->zones.empty() && files > 0;
            if (! ok) ++bad;
            std::cout << (ok ? "ok   " : "BAD  ") << d.getFileName() << ": " << set->zones.size() << " zones, " << files << " files, "
                      << (set->zones.empty() ? juce::String() : noteName (set->zones.front().natural) + ".." + noteName (set->zones.back().natural)) << std::endl;
        }
        for (auto& e : SdCard::tableEntries ("wavetables", "playlist-wavetable.txt"))
        {
            auto t = WaveTable::load (e, -1);
            if (! t) { ++bad; std::cout << "BAD  wavetable " << e.getFileName() << std::endl; }
        }
        std::cout << (bad == 0 ? "CARD OK" : juce::String (bad) + " PROBLEMS") << std::endl;
        return bad;
    }
    if (argc > 2 && juce::String (argv[1]) == "--host")
    {
        juce::ScopedJuceInitialiser_GUI gui;
        if (juce::SystemStats::getEnvironmentVariable ("EX_SD_ROOT", {}).isEmpty())
            setEnv ("EX_SD_ROOT", makeSyntheticCard().getFullPathName());
        juce::StringArray paths;
        for (int i = 2; i < argc; ++i) paths.add (juce::File (juce::String (argv[i])).getFullPathName());
        return hostTest (paths);
    }

    juce::ScopedJuceInitialiser_GUI gui;
    if (juce::SystemStats::getEnvironmentVariable ("EX_SD_ROOT", {}).isEmpty())
        setEnv ("EX_SD_ROOT", makeSyntheticCard().getFullPathName());
    juce::File out (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    out.createDirectory();
    int failures = 0;
    auto report = [&] (const juce::String& name, Stats s, bool expectSound = true) {
        bool ok = ! s.nan && (! expectSound || s.peak > 0.01f) && s.peak < 4.0f;
        if (! ok) ++failures;
        std::cout << (ok ? "PASS " : "FAIL ") << name << "  peak=" << s.peak << " rms=" << s.rms << (s.nan ? " NaN!" : "") << std::endl;
    };
    std::cout << "SD root: " << SdCard::root().getFullPathName() << std::endl;

    for (juce::String kind : { "wt", "fm", "ex", "ms" })
    {
        auto p = make (kind);
        p->prepareToPlay (48000, 256);
        pump (kind == juce::String ("ms") ? 6000 : 1500); // let folders/tables load
        juce::AudioBuffer<float> cap;
        auto st = render (*p, 4.0, chordEvents ({ 48, 55, 60, 64 }, 0.1, 2.0), &cap);
        writeWav (out.getChildFile (juce::String (kind) + "_chord.wav"), cap);
        report (juce::String (kind) + " chord", st);
        // chord generator + arpeggiator via MIDI (gate 3)
        auto& a = p->alg();
        int chordEnable = kind == juce::String ("ms") ? 29 : kind == juce::String ("wt") ? 44 : kind == juce::String ("fm") ? 39 : 37;
        a.set (chordEnable, 1); a.set (chordEnable + 3, 6); // triad + 8ve
        st = render (*p, 2.5, chordEvents ({ 60 }, 0.05, 1.5), &cap);
        writeWav (out.getChildFile (juce::String (kind) + "_triad.wav"), cap);
        report (juce::String (kind) + " chord generator", st);
        a.set (chordEnable, 0);
        snapshot (*p, out.getChildFile (juce::String (kind) + "_ui.png"));
        // display pages
        Oled o;
        for (int cur : { 7, 8 })
        {
            p->turnP (cur - p->curParam[0]);
            p->render (o);
            juce::String txt;
            for (int y = 0; y < 32; ++y) { for (int x = 0; x < 128; ++x) txt << (o.px[y][x] ? "#" : "."); txt << "\n"; }
            out.getChildFile (juce::String (kind) + "_oled_" + juce::String (cur) + ".txt").replaceWithText (txt);
        }
    }

    // ---------- the effects and the Plaits / Rings ports ----------
    {
        juce::AudioBuffer<float> cap;
        // Macro Oscillator 2: a chord through the round-robin group, then every model
        {
            auto p = make ("mo");
            p->prepareToPlay (48000, 256);
            auto st = render (*p, 2.0, chordEvents ({ 48, 55, 60, 64 }, 0.1, 1.0), &cap);
            writeWav (out.getChildFile ("mo_chord.wav"), cap);
            report ("mo chord", st);
            int silent = 0; bool bad = false; float worst = 0;
            for (int model = 0; model < 24; ++model)
            {
                p->alg().set (7, model);
                auto s2 = render (*p, 0.4, chordEvents ({ 48 + model % 12 }, 0.02, 0.3), nullptr);
                if (s2.nan || s2.peak > 4.0f) bad = true;
                // some FM voices fade in slowly: give those a longer note before calling them silent
                if (s2.peak < 0.003f) s2 = render (*p, 2.5, chordEvents ({ 60 }, 0.02, 2.4), nullptr);
                if (s2.peak < 0.003f) { ++silent; std::cout << "     silent model " << model << " " << MacroOsc2::modelName (model) << std::endl; }
                worst = std::max (worst, s2.peak);
            }
            bool ok = ! bad && silent == 0;
            if (! ok) ++failures;
            std::cout << (ok ? "PASS" : "FAIL") << " mo all 24 models sound (max peak " << worst << ")" << std::endl;
            snapshot (*p, out.getChildFile ("mo_ui.png"));
        }
        // Resonator: every mode, wet only, driven by the test input and by MIDI strums
        {
            auto p = make ("rs");
            p->prepareToPlay (48000, 256);
            p->alg().set (20, -40);
            for (int mode = 0; mode <= 6; ++mode)
            {
                p->alg().set (7, mode);
                auto st = render (*p, 1.5, chordEvents ({ 48, 55 }, 0.1, 0.6), mode == 0 ? &cap : nullptr, testInput());
                if (mode == 0) writeWav (out.getChildFile ("rs_modal.wav"), cap);
                report ("rs mode " + juce::String (mode), st);
            }
            p->alg().set (7, 0); p->alg().set (26, 0); // no audio input: the internal exciter is strummed from MIDI
            report ("rs internal exciter", render (*p, 1.5, chordEvents ({ 60 }, 0.1, 0.6), nullptr));
            snapshot (*p, out.getChildFile ("rs_ui.png"));
        }
        // Multi-FX: wet only for every pitch effect, every program, and the variable sample rate
        {
            auto p = make ("mx");
            p->prepareToPlay (48000, 256);
            p->alg().set (11, -40);
            for (int fx = 0; fx < 3; ++fx)
            {
                p->alg().set (29, fx);
                report ("mx wet, pitch effect " + juce::String (fx), render (*p, 1.5, {}, fx == 0 ? &cap : nullptr, testInput()));
            }
            writeWav (out.getChildFile ("mx_wet.wav"), cap);
            auto& mx = dynamic_cast<MultiFX&> (p->alg());
            bool bad = false; float worst = 0;
            for (int prog = 0; prog < MultiFX::programNames().size(); ++prog)
            {
                mx.loadProgram (prog);
                auto st = render (*p, 2.0, {}, nullptr, testInput());
                if (st.nan || st.peak > 4.0f || st.peak < 0.01f) { bad = true; std::cout << "     program " << prog << " peak " << st.peak << std::endl; }
                worst = std::max (worst, st.peak);
            }
            if (bad) ++failures;
            std::cout << (bad ? "FAIL" : "PASS") << " mx all programs (max peak " << worst << ")" << std::endl;
            mx.loadProgram (0); p->alg().set (7, -24);
            report ("mx variable sample rate -24", render (*p, 1.5, {}, nullptr, testInput()));
            snapshot (*p, out.getChildFile ("mx_ui.png"));
        }
        // Granulator: record live input and play it with a drone and with MIDI notes (wet only)
        {
            auto p = make ("gr");
            p->prepareToPlay (48000, 256);
            p->alg().set (9, -40); p->alg().set (11, 1); p->alg().set (42, 1);
            auto st = render (*p, 3.0, {}, &cap, testInput());
            writeWav (out.getChildFile ("gr_drone.wav"), cap);
            report ("gr drone on live input", st);
            p->alg().set (42, 0); p->alg().set (11, 0);
            report ("gr MIDI notes on the buffer", render (*p, 2.0, chordEvents ({ 48, 55, 60 }, 0.1, 1.2), nullptr));
            for (int mode = 1; mode <= 4; ++mode)
            {
                p->alg().set (12, mode);
                report ("gr spawn mode " + juce::String (mode), render (*p, 1.0, chordEvents ({ 50 }, 0.05, 0.8), nullptr));
            }
            snapshot (*p, out.getChildFile ("gr_ui.png"));
        }
        // Spectral Freeze: freeze the input, then it must keep sounding with silence at the input (wet only)
        {
            auto p = make ("sf");
            p->prepareToPlay (48000, 256);
            p->alg().set (21, -40);
            render (*p, 1.2, {}, nullptr, testInput());
            p->alg().set (10, 1);
            auto st = render (*p, 2.0, {}, &cap);
            writeWav (out.getChildFile ("sf_freeze.wav"), cap);
            report ("sf frozen voice sustains", st);
            float tail = 0;
            for (int i = cap.getNumSamples() - 4800; i < cap.getNumSamples(); ++i) tail = std::max (tail, std::abs (cap.getSample (0, i)));
            bool ok = tail > 0.003f;
            if (! ok) ++failures;
            std::cout << (ok ? "PASS" : "FAIL") << " sf still sounding after 2 s (" << tail << ")" << std::endl;
            p->alg().set (10, 0); p->alg().set (20, 0);
            auto rel = render (*p, 1.5, {}, &cap);
            tail = 0;
            for (int i = cap.getNumSamples() - 4800; i < cap.getNumSamples(); ++i) tail = std::max (tail, std::abs (cap.getSample (0, i)));
            ok = ! rel.nan && tail < 0.002f;
            if (! ok) ++failures;
            std::cout << (ok ? "PASS" : "FAIL") << " sf released voice fades out (" << tail << ")" << std::endl;
            snapshot (*p, out.getChildFile ("sf_ui.png"));
        }
        // random settings must never produce NaNs, infinities or a crash
        for (juce::String kind : { "gr", "mx", "rs", "sf", "mo" })
        {
            auto p = make (kind);
            p->prepareToPlay (48000, 256);
            juce::Random r (42);
            bool bad = false; float worst = 0;
            for (int round = 0; round < 30 && ! bad; ++round)
            {
                auto& a = p->alg();
                for (int n = 7; n <= a.numParams(); ++n)
                    if (r.nextFloat() < 0.5f) a.set (n, a.def (n).min + r.nextInt (a.def (n).max - a.def (n).min + 1));
                auto st = render (*p, 0.3, chordEvents ({ 36 + r.nextInt (48), 60 }, 0.02, 0.2), nullptr, testInput());
                worst = std::max (worst, st.peak);
                if (st.nan || st.peak > 20.0f) { bad = true; std::cout << "     round " << round << " peak " << st.peak << (st.nan ? " NaN" : "") << std::endl; }
            }
            if (bad) ++failures;
            std::cout << (bad ? "FAIL " : "PASS ") << kind << " random settings (max peak " << worst << ")" << std::endl;
        }
        // the fixed-rate engines (Plaits, Rings) at other host sample rates
        for (double rate : { 44100.0, 96000.0 })
            for (juce::String kind : { "mo", "rs" })
            {
                auto p = make (kind);
                p->prepareToPlay (rate, 256);
                if (kind == "rs") p->alg().set (20, -40);
                report (kind + " at " + juce::String ((int) rate) + " Hz", render (*p, 1.5, chordEvents ({ 48, 60 }, 0.1, 0.8), nullptr, testInput (1.0e9, rate)));
            }
    }

    // ---------- auto-sampler loopback ----------
    {
        auto p = make ("ms");
        p->prepareToPlay (48000, 256);
        pump (1500);
        p->selectAlgorithm (1);
        auto& rec = p->alg();
        rec.set (16, 1); rec.set (17, 60); rec.set (18, 64); rec.set (19, 2); rec.set (20, 2); rec.set (21, 1);
        rec.set (22, 0); rec.set (23, 400); rec.set (24, 3); rec.set (25, 0);
        p->txChannel = 1;
        // test synth: sine at the note's pitch while the note is held
        struct { int note = -1; float vel = 0; double ph = 0; } syn;
        int notesSeen = 0;
        auto loop = [&] (const juce::MidiBuffer& m, juce::AudioBuffer<float>& in, int64_t) {
            for (const auto ev : m)
            {
                auto msg = ev.getMessage();
                if (msg.isNoteOn()) { syn.note = msg.getNoteNumber(); syn.vel = msg.getFloatVelocity(); ++notesSeen; }
                else if (msg.isNoteOff()) syn.note = -1;
            }
            if (syn.note < 0) return;
            double hz = 440.0 * std::pow (2.0, (syn.note - 69) / 12.0);
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                float v = 0.5f * syn.vel * (float) std::sin (syn.ph);
                syn.ph += 2 * juce::MathConstants<double>::pi * hz / 48000.0;
                in.setSample (0, i, v); in.setSample (1, i, v);
            }
        };
        p->pushPot (0); // L push -> start auto-sampling
        render (*p, 5.0, {}, nullptr, loop);
        pump (800);
        auto folder = dynamic_cast<WavRecorder&> (p->alg()).lastAutoFolder();
        auto files = SdCard::sortedChildren (folder, juce::File::findFiles, "*.wav");
        std::cout << (files.size() == 6 ? "PASS" : "FAIL") << " auto-sampler wrote " << files.size() << " files to " << folder.getFileName()
                  << " (notes sent " << notesSeen << ")" << std::endl;
        if (files.size() != 6) ++failures;
        for (auto& f : files)
        {
            WavData w; loadWav (f, w);
            float pk = 0; for (auto v : w.data) pk = std::max (pk, std::abs (v));
            std::cout << "     " << f.getFileName() << "  " << w.frames << " frames @" << w.sampleRate << " peak " << pk << std::endl;
        }
        snapshot (*p, out.getChildFile ("rec_ui.png"));
        // play the new multisample
        p->selectAlgorithm (0);
        dynamic_cast<SdMultisample&> (p->alg()).onSelected();
        auto folders = SdCard::sampleFolders();
        int idx = folders.indexOf (folder);
        p->alg().set (7, idx);
        pump (1500);
        juce::AudioBuffer<float> cap;
        auto st = render (*p, 2.0, chordEvents ({ 60, 61, 64 }, 0.05, 1.0), &cap);
        writeWav (out.getChildFile ("autosample_playback.wav"), cap);
        report ("auto-sampled folder playback", st);
        if (argc > 2 && juce::String (argv[2]) == "--keep") {}
        else folder.deleteRecursively();
    }
    // ---------- an offline render straight after loading (a bounce, a command line render): nothing may be missed ----------
    for (juce::String kind : { "ms", "wt", "ex", "fm", "mo" })
    {
        auto p = make (kind);
        p->setNonRealtime (true);
        p->prepareToPlay (48000, 256);
        report (kind + " offline render right after loading, no waiting", render (*p, 1.0, chordEvents ({ 60 }, 0.0, 0.8), nullptr));
    }
    // ---------- defaults: a new instance starts on the defaults of the source, whatever earlier instances did ----------
    for (juce::String kind : { "wt", "fm", "ex", "ms", "gr", "mx", "rs", "sf", "mo" })
    {
        {
            auto dirty = make (kind);
            dirty->prepareToPlay (48000, 256);
            setEverything (*dirty, 1);
            pump (50);
        }   // closes, as a host does when the plug-in is removed
        auto a = make (kind), b = make (kind);
        juce::StringArray wrong;
        for (int ai = 0; ai < a->numAlgorithms(); ++ai)
            for (int n = 1; n <= a->algAt (ai).numParams(); ++n)
                if (a->algAt (ai).pv (n) != a->algAt (ai).def (n).def || a->algAt (ai).appliedValue (n) != a->algAt (ai).def (n).def)
                    wrong.add (juce::String (n) + " " + a->algAt (ai).def (n).name);
        const bool same = a->stateXml()->toString() == b->stateXml()->toString();
        const bool ok = wrong.isEmpty() && same && a->currentAlgorithmIndex() == 0 && a->getNumPrograms() == 1;
        if (! ok) ++failures;
        std::cout << (ok ? "PASS" : "FAIL") << " " << kind << " new instance starts on the defaults"
                  << (wrong.isEmpty() ? juce::String() : " - not default: " + wrong.joinIntoString (", ").substring (0, 200))
                  << (same ? "" : " - two new instances differ") << std::endl;
    }
    // ---------- state: every parameter and setting changed -> saved -> loaded into a new instance ----------
    for (juce::String kind : { "wt", "fm", "ex", "ms", "gr", "mx", "rs", "sf", "mo" })
    {
        auto a = make (kind);
        a->prepareToPlay (48000, 256);
        const int changed = setEverything (*a, 1);
        if (kind == "ms") a->selectAlgorithm (1);
        juce::MemoryBlock mb;
        a->getStateInformation (mb);
        const auto want = a->stateXml()->toString();

        int total = 0;
        for (int ai = 0; ai < a->numAlgorithms(); ++ai) total += a->algAt (ai).numParams();
        // into a new instance, and into one that has other values everywhere
        auto fresh = make (kind), used = make (kind);
        fresh->prepareToPlay (48000, 256); used->prepareToPlay (48000, 256);
        setEverything (*used, 2);
        fresh->setStateInformation (mb.getData(), (int) mb.getSize());
        used->setStateInformation (mb.getData(), (int) mb.getSize());
        juce::StringArray wrong;
        for (int ai = 0; ai < a->numAlgorithms(); ++ai)
            for (int n = 1; n <= a->algAt (ai).numParams(); ++n)
                for (auto* q : { fresh.get(), used.get() })
                    if (q->algAt (ai).pv (n) != a->algAt (ai).pv (n) || q->algAt (ai).appliedValue (n) != a->algAt (ai).pv (n))
                        wrong.add (juce::String (n) + " " + a->algAt (ai).def (n).name);
        const bool same = fresh->stateXml()->toString() == want && used->stateXml()->toString() == want;
        const bool ok = changed == total && wrong.isEmpty() && same;
        if (! ok) ++failures;
        std::cout << (ok ? "PASS" : "FAIL") << " " << kind << " state save/restore: " << changed << " of " << total << " parameters changed, "
                  << mb.getSize() << " bytes" << (wrong.isEmpty() ? juce::String() : " - lost: " + wrong.joinIntoString (", ").substring (0, 200))
                  << (same ? "" : " - settings differ") << std::endl;

        // a state from another plug-in of the suite is refused, and one that leaves parameters out resets them
        auto other = make (kind == "wt" ? "ex" : "wt");
        juce::MemoryBlock foreign;
        other->getStateInformation (foreign);
        fresh->setStateInformation (foreign.getData(), (int) foreign.getSize());
        const bool refused = fresh->stateXml()->toString() == want;
        auto partial = a->stateXml();
        for (auto* ax : partial->getChildWithTagNameIterator ("ALG"))
            while (auto* px = ax->getChildByName ("P")) ax->removeChildElement (px, true);
        used->restoreXml (*partial);
        bool reset = true;
        for (int ai = 0; ai < used->numAlgorithms(); ++ai)
            for (int n = 1; n <= used->algAt (ai).numParams(); ++n) reset &= used->algAt (ai).pv (n) == used->algAt (ai).def (n).def;
        if (! refused || ! reset) ++failures;
        std::cout << (refused && reset ? "PASS" : "FAIL") << " " << kind << " foreign state refused, missing parameters reset" << std::endl;
    }
    // ---------- state: the things that are not parameters ----------
    {
        // Granulator: the WAV in the buffer comes back
        auto wav = SdCard::root().getChildFile ("Test Sine").findChildFiles (juce::File::findFiles, false, "*.wav")[0];
        auto a = make ("gr");
        a->prepareToPlay (48000, 256);
        auto& ga = dynamic_cast<Granulator&> (a->alg());
        ga.loadFile (wav, false);
        pump (800);
        render (*a, 0.1, {}, nullptr);
        juce::MemoryBlock mb;
        a->getStateInformation (mb);
        auto b = make ("gr");
        b->prepareToPlay (48000, 256);
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        pump (800);
        auto& gb = dynamic_cast<Granulator&> (b->alg());
        b->alg().load (9, -40);
        auto st = render (*b, 1.5, chordEvents ({ 48 }, 0.05, 1.2), nullptr);
        const bool ok = gb.currentPath() == wav.getFullPathName() && b->alg().pv (7) == a->alg().pv (7) && a->alg().pv (7) == 1000 && st.peak > 0.01f;
        if (! ok) ++failures;
        std::cout << (ok ? "PASS" : "FAIL") << " gr loaded WAV restored with the state (" << juce::File (gb.currentPath()).getFileName()
                  << ", " << b->alg().pv (7) << " ms, peak " << st.peak << ")" << std::endl;
    }
    {
        // a Folder parameter follows its folder by name when the card's folder list has changed since the state was saved
        auto a = make ("ms");
        a->prepareToPlay (48000, 256);
        pump (300);
        const auto before = SdCard::sampleFolders();
        const int idx = before.size() - 1;
        a->alg().set (7, idx);
        juce::MemoryBlock mb;
        a->getStateInformation (mb);
        auto added = SdCard::root().getChildFile ("AAA Sorts First");
        added.createDirectory();
        before[idx].findChildFiles (juce::File::findFiles, false, "*.wav")[0].copyFileTo (added.getChildFile ("x_C4.wav"));
        auto b = make ("ms");
        b->prepareToPlay (48000, 256);
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        const auto after = SdCard::sampleFolders();
        const int now = b->alg().pv (7);
        const bool ok = after.size() == before.size() + 1 && now == idx + 1 && after[now].getFileName() == before[idx].getFileName();
        added.deleteRecursively();
        if (! ok) ++failures;
        std::cout << (ok ? "PASS" : "FAIL") << " ms folder followed by name after the card changed (" << idx << " -> " << now << ")" << std::endl;
    }
    {
        // Poly FM: a voice edited from a DX7 editor (SysEx) is saved and comes back
        auto a = make ("fm");
        a->prepareToPlay (48000, 256);
        render (*a, 0.1, {}, nullptr);
        uint8_t msg[163] {};
        msg[0] = 0xF0; msg[1] = 0x43; msg[2] = 0; msg[3] = 0; msg[4] = 0x01; msg[5] = 0x1B;
        for (int i = 0; i < 155; ++i) msg[6 + i] = (uint8_t) (i < 145 ? 50 : 'A' + i % 20);
        msg[162] = 0xF7;
        render (*a, 0.1, [&] (int64_t pos, int, juce::MidiBuffer& m) { if (pos == 0) m.addEvent (juce::MidiMessage (msg, 163), 0); }, nullptr);
        pump (400);
        auto xa = a->stateXml();
        const auto edit = xa->getChildByName ("ALG")->getStringAttribute ("edit1");
        juce::MemoryBlock mb;
        a->getStateInformation (mb);
        auto b = make ("fm");
        b->prepareToPlay (48000, 256);
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        render (*b, 0.1, {}, nullptr);
        pump (400);
        const auto back = b->stateXml()->getChildByName ("ALG")->getStringAttribute ("edit1");
        const bool ok = edit.isNotEmpty() && back == edit;
        if (! ok) ++failures;
        std::cout << (ok ? "PASS" : "FAIL") << " fm edited voice restored with the state" << std::endl;
    }
    {
        // Multi-FX: the chosen program is remembered, and nothing loads a program by itself
        auto a = make ("mx");
        a->prepareToPlay (48000, 256);
        auto before = a->stateXml()->toString();
        dynamic_cast<MultiFX&> (a->alg()).loadProgram (4);
        juce::MemoryBlock mb;
        a->getStateInformation (mb);
        auto b = make ("mx");
        const bool startsInit = b->stateXml()->toString() == before;
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        const bool ok = startsInit && b->stateXml()->toString() == a->stateXml()->toString() && b->stateXml()->toString() != before;
        if (! ok) ++failures;
        std::cout << (ok ? "PASS" : "FAIL") << " mx program restored with the state" << std::endl;
    }

    // ---------- WaveEdit import ----------
    {
        auto bank = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("WaveEdit/banks/ROM A.wav");
        if (bank.existsAsFile())
        {
            auto tmpSd = out.getChildFile ("sd_import");
            tmpSd.deleteRecursively(); tmpSd.createDirectory();
            auto old = juce::SystemStats::getEnvironmentVariable ("EX_SD_ROOT", {});
            setEnv ("EX_SD_ROOT", tmpSd.getFullPathName());
            auto made = PolyWavetable::importToCard ({ bank.getFullPathName() });
            int n = made.isEmpty() ? 0 : made[0].getNumberOfChildFiles (juce::File::findFiles, "*.wav");
            WavData w; if (n > 0) loadWav (made[0].findChildFiles (juce::File::findFiles, false, "*.wav")[0], w);
            bool ok = n == 64 && w.frames == 256;
            if (! ok) ++failures;
            std::cout << (ok ? "PASS" : "FAIL") << " WaveEdit import: " << n << " waves of " << w.frames << " samples" << std::endl;
            auto t = WaveTable::load (bank, -1);
            std::cout << ((t && t->numWaves == 64) ? "PASS" : "FAIL") << " WaveEdit bank direct load: " << (t ? t->numWaves : 0) << " waves" << std::endl;
            if (! t || t->numWaves != 64) ++failures;
            setEnv ("EX_SD_ROOT", old);
        }
    }
    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILURES") << std::endl;
    return failures;
}

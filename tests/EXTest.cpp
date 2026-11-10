// Offline test harness: renders each algorithm, snapshots the editor / OLED, and runs the
// auto-sampler in a loopback (MIDI out -> test synth -> audio in) then plays the result back.
//   EXTest <outdir>          (set EX_SD_ROOT to choose the SD card folder)
#include "algorithms/SdMultisample.h"
#include "algorithms/WavRecorder.h"
#include "algorithms/PolyWavetable.h"
#include "algorithms/PolyFM.h"
#include "algorithms/PolyExciter.h"
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
static juce::File makeSyntheticCard()
{
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("EXTest-SD");
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
    return std::make_unique<TestPlugin> (ExPlugin::Identity { "EX Poly Exciter", false, true, 1 }, [] (TestPlugin& p) {
        p.addAlgorithm (std::make_unique<PolyExciter> (p)); });
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

// Loads built plug-ins exactly like a DAW does, plays notes, opens the editor and round-trips state.
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
        inst->releaseResources();
        bool ok = peak > 0.01f && ! nan && edOk && stateOk;
        if (! ok) ++failures;
        std::cout << (ok ? "PASS" : "FAIL") << " host " << descs[0]->pluginFormatName << " " << descs[0]->name << "  peak=" << peak
                  << " editor=" << (edOk ? "ok" : "missing") << " state=" << (stateOk ? "ok" : "mismatch") << std::endl;
    }
    std::cout << (failures == 0 ? "HOST TESTS PASSED" : juce::String (failures) + " HOST FAILURES") << std::endl;
    return failures;
}

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
    {
        // README screenshots: play a chord in each plug-in and snapshot the editor (nothing is written to the card)
        juce::ScopedJuceInitialiser_GUI gui;
        ExPlugin::rememberLastState = false;
        juce::File out { juce::String (argv[2]) };
        out.createDirectory();
        for (juce::String kind : { "ms", "rec", "wt", "fm", "ex" })
        {
            auto p = make (kind == "rec" ? juce::String ("ms") : kind);
            p->prepareToPlay (48000, 256);
            if (kind == "rec") p->selectAlgorithm (1);
            if (kind == "fm") { p->alg().set (8, 5); }
            if (kind == "wt") { p->alg().set (7, 6); p->alg().applied[7] = 6; p->alg().confirm (7); p->alg().set (8, 35); }
            pump (2500);
            render (*p, 0.6, chordEvents ({ 48, 55, 60, 64 }, 0.05, 5.0), nullptr);
            snapshot (*p, out.getChildFile (kind + ".png"));
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "--dump-params")
    {
        // writes every algorithm's parameters (number, name, range, default, unit, value names, section) as JSON
        juce::ScopedJuceInitialiser_GUI gui;
        ExPlugin::rememberLastState = false;
        juce::Array<juce::var> algsOut;
        for (juce::String kind : { "ms", "wt", "fm", "ex" })
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
    ExPlugin::rememberLastState = false;
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
        int maxN = 0; for (int v = 0; v < 8; ++v) maxN += p->alg().params.empty() ? 0 : 0;
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
    // ---------- state save / restore for every plug-in ----------
    for (juce::String kind : { "wt", "fm", "ex", "ms" })
    {
        auto a = make (kind);
        a->prepareToPlay (48000, 256);
        auto& alg = a->alg();
        for (int n = 7; n <= alg.numParams(); n += 3)
        {
            const auto& d = alg.def (n);
            if (d.confirm) continue;
            alg.set (n, d.min + (d.max - d.min) / 3);
        }
        a->rxChannel = 3; a->txChannel = 5; a->contrast = 200;
        if (kind == "ms") a->selectAlgorithm (1);
        juce::MemoryBlock mb;
        a->getStateInformation (mb);
        auto b = make (kind);
        b->prepareToPlay (48000, 256);
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = a->stateXml()->toString() == b->stateXml()->toString();
        if (! same) ++failures;
        std::cout << (same ? "PASS" : "FAIL") << " " << kind << " state save/restore (" << mb.getSize() << " bytes)" << std::endl;
    }
    // ---------- last state is remembered between sessions ----------
    {
        ExPlugin::rememberLastState = true;
        auto makeTemp = [] { return std::make_unique<TestPlugin> (ExPlugin::Identity { "EXTest LastState", false, false, 1 }, [] (TestPlugin& p) {
            p.addAlgorithm (std::make_unique<PolyWavetable> (p)); }); };
        { auto p = makeTemp(); p->lastStateFile().deleteFile(); }
        { auto p = makeTemp(); p->alg().set (8, 42); p->alg().set (40, -17); p->rxChannel = 9; } // closes -> saves
        int v8, v40, rx; juce::File f;
        { auto p = makeTemp(); v8 = p->alg().pv (8); v40 = p->alg().pv (40); rx = p->rxChannel; f = p->lastStateFile(); }
        bool ok = v8 == 42 && v40 == -17 && rx == 9;
        if (! ok) ++failures;
        std::cout << (ok ? "PASS" : "FAIL") << " last state remembered (" << v8 << ", " << v40 << ", rx " << rx << ")" << std::endl;
        f.deleteFile();
        ExPlugin::rememberLastState = false;
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

#include "SdMultisample.h"

namespace ex
{
// ------------------------------------------------------------------------------------------------ naming
static int parseNoteName (const juce::String& t) // "A2", "C#5", "C-1" -> MIDI (C4 = 60); -1 if not a note
{
    if (t.length() < 2) return -1;
    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    juce::juce_wchar c = t[0];
    if (c < 'A' || c > 'G') return -1;
    int i = 1, semis = base[c - 'A'];
    if (t[i] == '#') { ++semis; ++i; }
    auto rest = t.substring (i);
    if (rest.isEmpty() || ! (rest.containsOnly ("-0123456789")) || rest == "-") return -1;
    int oct = rest.getIntValue();
    int n = (oct + 1) * 12 + semis;
    return n >= 0 && n <= 127 ? n : -1;
}

struct ParsedName { juce::String key; int natural = -1, sw = -1, vel = 0, rr = 0; };
static ParsedName parseName (const juce::String& fileName)
{
    ParsedName p;
    auto tokens = juce::StringArray::fromTokens (fileName.upToLastOccurrenceOf (".", false, false), "_", "");
    juce::StringArray keep;
    for (int i = 0; i < tokens.size(); ++i)
    {
        auto t = tokens[i];
        if (i > 0)
        {
            if (t.startsWith ("SW") && t.substring (2).containsOnly ("0123456789") && t.length() > 2) { p.sw = t.substring (2).getIntValue(); continue; }
            if (t.startsWith ("RR") && t.substring (2).containsOnly ("0123456789") && t.length() > 2) { p.rr = t.substring (2).getIntValue(); continue; }
            if (t.startsWith ("V") && t.substring (1).containsOnly ("0123456789") && t.length() > 1) { p.vel = t.substring (1).getIntValue(); continue; }
            int nn = parseNoteName (t);
            if (nn >= 0) { p.natural = nn; keep.add (t); continue; }
        }
        keep.add (t);
    }
    p.key = keep.joinIntoString ("_");
    return p;
}

std::shared_ptr<SampleSet> SampleSet::load (const juce::File& folder)
{
    auto set = std::make_shared<SampleSet>();
    set->folderName = folder.getFileName();
    auto files = SdCard::sortedChildren (folder, juce::File::findFiles, "*.wav");
    files.removeIf ([] (const juce::File& f) { return ! SdCard::isWav (f); });
    // optional playlist
    std::map<juce::String, juce::StringPairArray> flags;
    juce::StringPairArray global;
    auto pl = folder.getChildFile ("playlist.txt");
    if (pl.existsAsFile())
    {
        auto entries = readPlaylist (pl, global);
        if (! entries.empty())
        {
            juce::Array<juce::File> ordered;
            for (auto& e : entries) { auto f = folder.getChildFile (e.file); if (f.existsAsFile()) { ordered.add (f); flags[e.file] = e.flags; } }
            if (! ordered.isEmpty()) files = ordered;
        }
    }
    std::map<juce::String, int> zoneIndex;
    struct Tmp { int vel, rr; SampleZone::File file; };
    std::vector<std::vector<Tmp>> tmp;
    int idx = 0;
    for (auto& f : files)
    {
        auto pn = parseName (f.getFileName());
        auto wav = std::make_shared<WavData>();
        if (! loadWav (f, *wav, -1, true) || wav->frames < 2) continue;
        auto fl = global;
        if (flags.count (f.getFileName())) fl.addArray (flags[f.getFileName()]);
        auto it = zoneIndex.find (pn.key);
        int zi;
        if (it == zoneIndex.end())
        {
            zi = (int) set->zones.size(); zoneIndex[pn.key] = zi;
            SampleZone z; z.name = pn.key;
            z.hasNatural = pn.natural >= 0; z.natural = pn.natural;
            if (fl.containsKey ("natural")) { z.hasNatural = true; z.natural = fl["natural"].getIntValue(); }
            if (pn.sw >= 0) { z.explicitSwitch = true; z.switchNote = pn.sw; }
            if (fl.containsKey ("switch")) { z.explicitSwitch = true; z.switchNote = fl["switch"].getIntValue(); }
            if (fl.containsKey ("loop")) z.loopFlag = fl["loop"].getIntValue();
            if (fl.containsKey ("endLoopOnRelease")) z.endLoopOnRelease = fl["endLoopOnRelease"].getIntValue();
            set->zones.push_back (z);
            tmp.emplace_back();
        }
        else zi = it->second;
        tmp[(size_t) zi].push_back ({ pn.vel, pn.rr, { wav, f.getFileName(), idx } });
        ++idx;
    }
    set->numFiles = idx;
    // velocity layers sorted by _V, round robins by _RR
    for (size_t zi = 0; zi < set->zones.size(); ++zi)
    {
        auto& t = tmp[zi];
        std::sort (t.begin(), t.end(), [] (const Tmp& a, const Tmp& b) { return a.vel != b.vel ? a.vel < b.vel : a.rr < b.rr; });
        auto& z = set->zones[zi];
        int lastVel = -9999;
        for (auto& e : t)
        {
            if (e.vel != lastVel) { z.layers.emplace_back(); lastVel = e.vel; }
            z.layers.back().push_back (e.file);
        }
    }
    // automatic naturals from 48 upwards for files without a pitch
    int next = 48;
    for (auto& z : set->zones) if (! z.hasNatural) z.natural = juce::jmin (127, next++);
    std::stable_sort (set->zones.begin(), set->zones.end(), [] (const SampleZone& a, const SampleZone& b) { return a.natural < b.natural; });
    // automatic switch points
    for (size_t i = 0; i < set->zones.size(); ++i)
    {
        auto& z = set->zones[i];
        if (z.explicitSwitch) continue;
        if (i == 0) { z.switchNote = 0; continue; }
        int lower = set->zones[i - 1].natural;
        int gap = z.natural - lower - 1; // notes strictly between
        int lowerTake = gap <= 3 ? 0 : (gap - 3 + 1) / 2;
        z.switchNote = lower + 1 + lowerTake;
    }
    return set;
}

SampleZone* SampleSet::zoneFor (int note)
{
    if (zones.empty()) return nullptr;
    SampleZone* z = &zones[0];
    for (auto& zz : zones) if (zz.switchNote <= note) z = &zz;
    return z;
}

// ------------------------------------------------------------------------------------------------ algorithm
SdMultisample::SdMultisample (ExPlugin& p) : PolyAlgorithm (p, 3, "SD Multisample")
{
    numTimbres = 3;
    midiIsGate3 = false;
    addAttenuverters();
    add ("Folder", 0, 999, 0);                                          // 7
    defs.back().fmt = [] (int) { return juce::String(); };
    add ("Attack time", 0, 127, 0); add ("Decay time", 0, 127, 60);      // 8, 9
    add ("Sustain level", 0, 127, 127); add ("Release time", 0, 127, 77); // 10, 11
    defs.back().fmt = [] (int v) { return v >= 127 ? juce::String ("Infinite") : juce::String(); };
    add ("Octave", -8, 8, 0); add ("Transpose", -60, 60, 0); add ("Fine tune", -100, 100, 0, "cents"); // 12-14
    add ("Gain", -40, 24, 0, "dB");                                     // 15
    add ("Saturation", 0, 1, 1, {}, { "Off", "On" });                   // 16
    add ("Sustain", 0, 1, 0, {}, { "Off", "On" });                      // 17
    add ("Max voices", 1, 16, 16);                                        // 18
    add ("Bend range", 0, 48, 2);                                       // 19
    add ("Pitch bend input", 0, 6, 0);                                  // 20
    for (int i = 1; i <= 8; ++i) add ("Voice " + juce::String (i) + " detune", -100, 100, 0, "cents"); // 21-28
    add ("Chord enable", 0, 7, 0); defs.back().fmt = [] (int v) { return chordEnableName (v); };        // 29
    add ("Chord key", -12, 12, 0); defs.back().fmt = [] (int v) { return keyName (v); };               // 30
    add ("Chord scale", 0, 8, 0); defs.back().fmt = [] (int v) { return chordScaleName (v); };         // 31
    add ("Chord shape", 0, 13, 0); defs.back().fmt = [] (int v) { return chordShapeName (v); };        // 32
    add ("Chord inversion", 0, 3, 0);                                                                  // 33
    for (int i = 1; i <= 3; ++i) { add ("Arpeggio " + juce::String (i) + " mode", 0, 14, 0); defs.back().fmt = [] (int v) { return arpModeName (v); }; } // 34-36
    for (int i = 1; i <= 3; ++i) add ("Arpeggio " + juce::String (i) + " range", 1, 3, 1);           // 37-39
    add ("Scala/MTS", -16, 99, 0); defs.back().fmt = [this] (int v) { return scalaName (v); };         // 40
    add ("Scala KBM", 0, 99, 0); defs.back().fmt = [this] (int v) { return kbmName (v); };             // 41
    add ("Folder 2", -1, 999, -1); defs.back().fmt = [] (int) { return juce::String(); };              // 42
    add ("Folder 3", -1, 999, -1); defs.back().fmt = [] (int) { return juce::String(); };              // 43
    for (int t = 1; t <= 3; ++t)
    {
        add ("Min note " + juce::String (t), 0, 127, 0); defs.back().fmt = [] (int v) { return noteName (v); };
        add ("Max note " + juce::String (t), 0, 127, 127); defs.back().fmt = [] (int v) { return noteName (v); };
    }                                                                                                  // 44-49
    add ("Output spread", -100, 100, 0, "%");                                                          // 50
    add ("Delay mode", 0, 2, 0, {}, { "Off", "Stereo", "Ping-pong" });                                 // 51
    add ("Delay level", -40, 0, -3, "dB");                                                             // 52
    add ("Delay time", 1, 1365, 500, "ms");                                                            // 53
    defs.back().fmt = [this] (int v) { return delayUsesMidiClock ? "MIDI " + delayDivisionName (v) : juce::String ("ms"); };
    add ("Delay feedback", 0, 100, 50, "%");                                                           // 54
    add ("Tone bass", -240, 240, 0); defs.back().fmt = [] (int v) { return juce::String (v / 10.0, 1) + "dB"; };   // 55
    add ("Tone treble", -240, 240, 0); defs.back().fmt = [] (int v) { return juce::String (v / 10.0, 1) + "dB"; }; // 56
    add ("Break time", 0, 1000, 0, "ms");                                                              // 57
    add ("Break direction", 0, 2, 0, {}, { "Up", "Down", "Alternate" });                               // 58
    for (int i = 1; i <= 8; ++i) add ("Voice " + juce::String (i) + " bend input", 0, 6, 0);          // 59-66
    add ("Output mode", 0, 4, 0, {}, { "All summed 1", "All summed 2", "Per gate 12-34", "Per gate 12-34 2", "Per gate 1-2-3" }); // 67
    add ("Input mode", 0, 9, 1);                                                                       // 68
    add ("Sustain mode", 0, 1, 0, {}, { "Synth", "Piano" });                                           // 69
    add ("MIDI vel curve", 0, 3, 0, {}, { "Linear", "Soft", "Hard", "Fixed" });                        // 70
    add ("Arp reset input", 0, 6, 0);                                                                  // 71
    add ("Gate offset", 0, 496, 0);                                                                    // 72
    add ("Round robin mode", 0, 3, 0, {}, { "Sequential", "Random", "Random2", "Random3" });            // 73
    for (int i = 9; i <= 16; ++i) add ("Voice " + juce::String (i) + " detune", -100, 100, 0, "cents"); // 74-81

    engine.startVoice = [this] (const PolyNoteEngine::VoiceStart& s) { startVoice (s); };
    engine.releaseVoice = [this] (int vi)
    {
        auto& v = voices[vi];
        v.released = true; v.env.noteOff();
        if (v.endLoopOnRelease) v.looping = false;
    };
    engine.killVoice = [this] (int vi) { if (voices[vi].active) voices[vi].killing = true; };
    engine.voiceSounding = [this] (int vi) { return voices[vi].active; };
    startTimerHz (8);
    sections = {
        { "SOUND", { 15, 16, 18, 73, 67, 50 } },
        { "ENV", { 8, 9, 10, 11 } },
        { "TUNE", { 12, 13, 14, 19, 40, 41, 21, 22, 23, 24, 25, 26, 27, 28, 74, 75, 76, 77, 78, 79, 80, 81 } },
        { "CHORD", { 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 57, 58 } },
        { "FX", { 51, 53, 54, 52, 55, 56 } },
        { "KEYS", { 17, 69, 70, 44, 45, 46, 47, 48, 49 } },
    };
}

std::vector<BrowserSpec> SdMultisample::browsers()
{
    BrowserSpec t;
    t.title = "Timbre"; t.compact = true;
    t.items = [] { return juce::StringArray { "1", "2", "3" }; };
    t.selected = [this] { return uiTimbre; };
    t.choose = [this] (int i) { uiTimbre = i; };
    BrowserSpec f;
    f.title = "Sample folders";
    f.items = [this] { juce::StringArray a; if (uiTimbre > 0) a.add ("(same as timbre 1)"); for (auto& d : folders) a.add (d.getFileName()); return a; };
    f.selected = [this] {
        if (uiTimbre == 0) return pv (7);
        int v = pv (uiTimbre == 1 ? 42 : 43);
        return v < 0 ? 0 : v + 1; };
    f.choose = [this] (int i) {
        if (uiTimbre == 0) set (7, i);
        else set (uiTimbre == 1 ? 42 : 43, i - 1); };
    BrowserSpec z;
    z.title = "Samples";
    z.items = [this] {
        juce::StringArray a;
        if (auto s = setFor (uiTimbre))
            for (auto& zz : s->zones)
            {
                int files = 0; for (auto& l : zz.layers) files += (int) l.size();
                a.add (noteName (zz.natural).paddedRight (' ', 4) + zz.name + (zz.layers.size() > 1 ? " V" + juce::String ((int) zz.layers.size()) : juce::String()));
            }
        return a; };
    z.selected = [] { return -1; };
    z.choose = [this] (int i) {
        if (auto s = setFor (uiTimbre))
            if (i >= 0 && i < (int) s->zones.size())
            {
                int note = juce::jlimit (0, 127, s->zones[(size_t) i].natural - pv (12) * 12 - pv (13));
                int ch = plugin.timbreChannel[uiTimbre] > 0 ? plugin.timbreChannel[uiTimbre] : juce::jmax (1, plugin.rxChannel) + uiTimbre;
                plugin.keyboardState.noteOn (ch, note, 0.8f);
                auto* ks = &plugin.keyboardState;
                juce::Timer::callAfterDelay (700, [ks, ch, note] { ks->noteOff (ch, note, 0.0f); });
            } };
    return { t, f, z };
}

std::vector<ActionSpec> SdMultisample::actions()
{
    return {
        { "Notes off", [this] { plugin.allNotesOffRequest = true; }, {} },
        { "Rescan", [this] { onSelected(); plugin.message ("Rescanned SD card"); }, {} },
    };
}

void SdMultisample::prepare (double sampleRate, int block)
{
    preparePoly (sampleRate, block);
    delay.prepare (sampleRate, 1.5f);
    for (auto& v : voices) { v.active = false; v.set.reset(); v.env.sr = sampleRate; }
    eqBass = eqTreb = 9999;
    if (folders.isEmpty()) onSelected();
}

void SdMultisample::onSelected()
{
    folders = SdCard::sampleFolders();
    for (auto& l : loadedFolder) l = -2;
    timerCallback();
}

juce::String SdMultisample::folderName (int idx) const
{
    if (folders.isEmpty()) return "(no folders)";
    return folders[juce::jlimit (0, folders.size() - 1, idx)].getFileName();
}

void SdMultisample::timerCallback()
{
    // (re)load folders for the three timbres when the Folder parameters change
    if (folders.isEmpty()) folders = SdCard::sampleFolders();
    const int f1 = pv (7);
    int want[3] = { f1, pv (42) < 0 ? f1 : pv (42), pv (43) < 0 ? f1 : pv (43) };
    for (int t = 0; t < 3; ++t)
    {
        int idx = folders.isEmpty() ? -1 : juce::jlimit (0, folders.size() - 1, want[t]);
        if (idx == loadedFolder[t]) continue;
        loadedFolder[t] = idx;
        // share an already loaded set between timbres
        bool shared = false;
        for (int o = 0; o < 3; ++o)
            if (o != t && loadedFolder[o] == idx && setFor (o) && setFor (o)->folderName == folders[idx].getFileName())
            { std::atomic_store (&sets[t], setFor (o)); shared = true; break; }
        if (shared || idx < 0) { if (idx < 0) std::atomic_store (&sets[t], std::shared_ptr<SampleSet>()); continue; }
        auto folder = folders[idx];
        runAsync ([this, t, idx, folder] {
            auto s = SampleSet::load (folder);
            juce::MessageManager::callAsync ([this, t, idx, s] {
                if (loadedFolder[t] != idx) return;
                if (auto old = setFor (t)) graveyard.push_back (old);
                std::atomic_store (&sets[t], s);
            });
        });
    }
    // free sample sets that no voice uses any more
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(), [] (const std::shared_ptr<SampleSet>& s) { return s.use_count() <= 1; }), graveyard.end());
}

void SdMultisample::configure()
{
    updateSustain();
    engine.maxVoices = pv (18);
    engine.unison = 1;
    engine.transpose = pv (12) * 12 + pv (13);
    engine.fineCents = (float) pv (14);
    engine.sustainMode = pv (69);
    engine.triggerMode = 0;
    engine.velCurve = pv (70);
    auto& c = engine.chords;
    c.enable = pv (29); c.key = pv (30); c.scale = pv (31); c.shape = pv (32); c.inversion = pv (33);
    for (int i = 0; i < 3; ++i) { c.arpMode[i] = pv (34 + i); c.arpRange[i] = pv (37 + i); }
    c.breakMs = pv (57); c.breakDir = pv (58);
    updateTuning (40, 41);
    engine.tuning = &tuning;
    for (auto& v : voices)
    {
        v.env.aT = attackSeconds (pv (8), true); v.env.dT = decaySeconds (pv (9)); v.env.sustain = pv (10) / 127.0f;
        v.env.rT = releaseSeconds (pv (11)); v.env.infiniteRelease = pv (11) >= 127;
        v.env.aK = 0.02f; v.env.dK = 4.0f;
    }
    if (eqBass != pv (55)) { eqBass = pv (55); bassL.shelf (false, 120.0f, eqBass / 10.0f, sr); bassR = bassL; }
    if (eqTreb != pv (56)) { eqTreb = pv (56); trebL.shelf (true, 6000.0f, eqTreb / 10.0f, sr); trebR = trebL; }
}

void SdMultisample::startVoice (const PolyNoteEngine::VoiceStart& s)
{
    auto& v = voices[s.voice];
    auto set = setFor (juce::jlimit (0, 2, s.timbre));
    if (! set) { engine.voiceFinished (s.voice); return; }
    float pitch = s.pitch;
    auto* z = set->zoneFor ((int) std::round (pitch));
    if (! z || z->layers.empty()) { engine.voiceFinished (s.voice); return; }
    int nl = (int) z->layers.size();
    auto& layer = z->layers[(size_t) juce::jlimit (0, nl - 1, (int) (s.velocity * (float) nl))];
    int nr = (int) layer.size();
    int rr = 0;
    if (nr > 1)
    {
        static std::minstd_rand rng (99);
        switch (pv (73))
        {
            case 1: rr = (int) (rng() % (unsigned) nr); break;
            case 2: rr = (int) (rng() % (unsigned) nr); if (rr == z->rrLast) rr = (rr + 1 + (int) (rng() % (unsigned) (nr - 1))) % nr; break;
            case 3:
                if ((int) z->rrPerm.size() != nr || z->rrPos >= nr)
                {
                    z->rrPerm.resize ((size_t) nr); for (int i = 0; i < nr; ++i) z->rrPerm[(size_t) i] = i;
                    std::shuffle (z->rrPerm.begin(), z->rrPerm.end(), rng); z->rrPos = 0;
                }
                rr = z->rrPerm[(size_t) z->rrPos++]; break;
            default: rr = z->rrCounter++ % nr; break;
        }
        z->rrLast = rr;
    }
    const auto& file = layer[(size_t) rr];
    const bool wasActive = v.active;
    v.set = set; v.zone = z; v.wav = file.wav.get(); v.fileIndex = file.index;
    v.timbre = s.timbre; v.note = s.note; v.vel = s.velocity;
    // transpose limit: drop octaves while transposed up too far
    float semis = pitch - (float) z->natural;
    if (transposeLimit > 0) while (semis > (float) transposeLimit) semis -= 12.0f;
    v.pitch = (float) z->natural + semis;
    v.pos = 0;
    v.looping = z->loopFlag >= 0 ? (z->loopFlag != 0 && v.wav->frames > 4) : v.wav->hasLoop();
    v.endLoopOnRelease = z->endLoopOnRelease != 0;
    v.released = false; v.killing = false; v.fade = 1.0f;
    v.active = true;
    v.env.noteOn (wasActive && false);
}

void SdMultisample::renderVoices (float* const* out, int start, int n)
{
    juce::ignoreUnused (start);
    const int outMode = pv (67);
    const float spread = pv (50) / 100.0f;
    for (int vi = 0; vi < PolyNoteEngine::kMaxVoices; ++vi)
    {
        auto& v = voices[vi];
        if (! v.active) { voiceLevel[vi] = 0.0f; continue; }
        const WavData& w = *v.wav;
        const float detune = (float) pv (vi < 8 ? 21 + vi : 74 + vi - 8) * 0.01f;
        const double ratio = std::pow (2.0, (v.pitch + bendSemis + detune - (float) v.zone->natural) / 12.0) * w.sampleRate / sr;
        int64_t ls = 0, le = w.frames;
        if (w.hasLoop()) { ls = w.loopStart; le = w.loopEnd; }
        float gl, gr;
        int o0 = 0, o1 = 1;
        if (outMode <= 1) panGains (spreadPan (outMode, vi, v.pitch, spread), gl, gr);
        else if (outMode <= 3) { panGains (spreadPan (outMode - 2, vi, v.pitch, spread), gl, gr); if (v.timbre == 1) { o0 = 2; o1 = 3; } }
        else { gl = 1.0f; gr = 0.0f; o0 = o1 = juce::jlimit (0, 2, v.timbre); }
        const float velAmp = 0.15f + 0.85f * v.vel;
        float e = 0;
        bool ended = false;
        for (int i = 0; i < n; ++i)
        {
            e = v.env.next();
            if (! v.env.active()) { ended = true; break; }
            int64_t ip = (int64_t) v.pos;
            if (ip >= w.frames - 1 && ! v.looping) { ended = true; break; }
            float fr = (float) (v.pos - (double) ip);
            auto at = [&] (int64_t k, int c) {
                if (v.looping && k >= le) k = ls + (k - le) % juce::jmax ((int64_t) 1, le - ls);
                k = juce::jlimit ((int64_t) 0, w.frames - 1, k);
                return w.at ((size_t) (k * w.channels + c));
            };
            float sL = 0, sR = 0;
            for (int c = 0; c < 2; ++c)
            {
                int cc = juce::jmin (c, w.channels - 1);
                float y0 = at (ip - 1, cc), y1 = at (ip, cc), y2 = at (ip + 1, cc), y3 = at (ip + 2, cc);
                float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                float y = ((c3 * fr + c2) * fr + c1) * fr + y1;
                if (c == 0) sL = y; else sR = y;
            }
            v.pos += ratio;
            if (v.looping && v.pos >= (double) le) v.pos -= (double) (le - ls);
            if (v.killing) { v.fade -= 1.0f / (0.003f * (float) sr); if (v.fade <= 0) { ended = true; break; } }
            float g = e * velAmp * v.fade * 0.5f;
            if (outMode == 4) { out[o0][i] += 0.5f * (sL + sR) * g; continue; }
            out[o0][i] += sL * g * gl * 1.41421356f;
            out[o1][i] += sR * g * gr * 1.41421356f;
        }
        voiceLevel[vi] = e;
        if (ended)
        {
            v.active = false; v.set.reset(); voiceLevel[vi] = 0.0f;
            engine.voiceFinished (vi);
        }
    }
}

void SdMultisample::postProcess (float* const* out, int n)
{
    const int outMode = pv (67);
    const float g = dbToGain ((float) pv (15));
    const bool sat = pv (16) != 0;
    for (int i = 0; i < n; ++i)
    {
        out[0][i] = trebL.process (bassL.process (out[0][i]));
        out[1][i] = trebR.process (bassR.process (out[1][i]));
    }
    if (outMode <= 1) delay.process (out[0], out[1], n, pv (51), (float) pv (52), (float) delayMs (pv (53)), pv (54) / 100.0f);
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < n; ++i) { float x = out[c][i] * g; out[c][i] = sat ? softSat (x) : x; }
    if (outMode <= 1)
    {
        bool gate = false;
        for (int v = 0; v < PolyNoteEngine::kMaxVoices; ++v) gate |= paraphonicGateMode == 0 ? voices[v].active : engine.voiceGate[v];
        for (int i = 0; i < n; ++i) { out[2][i] = gate ? 0.5f : 0.0f; out[3][i] = 0.5f * (out[0][i] + out[1][i]); }
    }
    else if (outMode == 4)
    {
        bool gate = false;
        for (int v = 0; v < PolyNoteEngine::kMaxVoices; ++v) gate |= voices[v].active;
        for (int i = 0; i < n; ++i) out[3][i] = gate ? 0.5f : 0.0f;
    }
}

bool SdMultisample::pushR (int cur)
{
    if (cur != 53) return false;
    double now = juce::Time::getMillisecondCounterHiRes(), dt = now - lastTap; lastTap = now;
    if (dt > 30 && dt < 1365 && ! delayUsesMidiClock) set (53, (int) dt);
    return true;
}

void SdMultisample::addMenuItems (std::vector<MenuItem>& items)
{
    addStandardPolyMenu (items, true, true);
    auto& opts = items.back().children;
    opts.push_back (MenuItem::val ("Transpose limit", 0, 48, [this] { return transposeLimit; }, [this] (int v) { transposeLimit = v; },
                                   [] (int v) { return v == 0 ? juce::String ("Off") : juce::String (v) + " semitones"; }));
}

void SdMultisample::drawPage (Oled& o, int cur)
{
    PolyAlgorithm::drawPage (o, cur);
    // top line: the highest active voice's file, natural pitch and switch
    int top = -1; float topPitch = -1;
    for (int v = 0; v < PolyNoteEngine::kMaxVoices; ++v)
        if (voices[v].active && voices[v].pitch > topPitch && voices[v].zone) { top = v; topPitch = voices[v].pitch; }
    if (top >= 0)
    {
        o.fillRect (0, 0, 128, 8, false);
        auto& v = voices[top];
        auto* z = v.zone;
        juce::String fn;
        for (auto& l : z->layers) for (auto& f : l) if (f.index == v.fileIndex) fn = f.fileName;
        o.at (0, 0, fn.substring (0, 12));
        o.right (16, 0, juce::String (z->natural));
        o.right (20, 0, juce::String (z->switchNote));
    }
    o.fillRect (0, 24, 128, 8, false);
    if (cur == 7 || cur == 42 || cur == 43)
    {
        int f = pv (cur) < 0 ? pv (7) : pv (cur);
        o.at (0, 3, folderName (f).substring (0, 21));
        return;
    }
    if (cur == 40 || cur == 41)
    {
        if (tuning.error.isNotEmpty()) o.at (0, 3, tuning.error);
        else if (cur == 40) o.at (0, 3, tuning.description.substring (0, 21));
        return;
    }
    for (int v = 0; v < PolyNoteEngine::kMaxVoices; ++v)
    {
        juce::String s = voices[v].active ? juce::String (voices[v].fileIndex % 100).paddedLeft ('0', 2) : juce::String ("--");
        o.text (v * 16, 24, s);
    }
}
} // namespace ex

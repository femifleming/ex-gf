#include "Granulator.h"

namespace ex
{
static constexpr double kMaxBufferSeconds = 31.25;

static float envSeconds (int v) { return 0.1f * std::pow (300.0f, (float) juce::jlimit (0, 127, v) / 127.0f); } // 100ms - 30s

// the six grain windows, kWin + 2 points each
const float* Granulator::window (int shape)
{
    static const auto tables = []
    {
        std::array<std::array<float, kWin + 2>, 6> t {};
        for (int i = 0; i < kWin + 2; ++i)
        {
            const float p = juce::jlimit (0.0f, 1.0f, (float) i / (float) kWin);
            const float g = (p - 0.5f) / 0.15f;
            t[0][(size_t) i] = std::exp (-0.5f * g * g);
            t[1][(size_t) i] = p < 0.25f ? 0.5f * (1.0f - std::cos (kPi * p * 4.0f)) : p > 0.75f ? 0.5f * (1.0f - std::cos (kPi * (1.0f - p) * 4.0f)) : 1.0f;
            t[2][(size_t) i] = 1.0f - std::abs (2.0f * p - 1.0f);
            t[3][(size_t) i] = std::exp (-6.0f * p) * juce::jmin (1.0f, p * 100.0f);
            t[4][(size_t) i] = std::exp (-6.0f * (1.0f - p)) * juce::jmin (1.0f, (1.0f - p) * 100.0f);
            t[5][(size_t) i] = std::sqrt (juce::jmax (0.0f, p < 0.5f ? 2.0f * p : 2.0f * (1.0f - p)));
        }
        return t;
    }();
    return tables[(size_t) juce::jlimit (0, 5, shape)].data();
}

static float quantizeDeviation (float semis, int mode)
{
    switch (mode)
    {
        case 1: return std::round (semis / 12.0f) * 12.0f;
        case 2: return std::round (semis / 5.0f) * 5.0f;
        case 3: return std::round (semis / 7.0f) * 7.0f;
        case 4: case 5:
        {
            const float third = mode == 4 ? 4.0f : 3.0f;
            const float base = std::floor (semis / 12.0f) * 12.0f;
            float best = base, bd = 1.0e9f;
            for (float c : { base, base + third, base + 7.0f, base + 12.0f })
                if (std::abs (semis - c) < bd) { bd = std::abs (semis - c); best = c; }
            return best;
        }
        default: return semis;
    }
}

Granulator::Granulator (ExPlugin& p) : PolyAlgorithm (p, 11, "Granulator")
{
    for (auto& g : dispGrain) g = -1.0f;
    addAttenuverters();
    add ("Buffer size", 100, 31250, 5000, "ms");                         // 7
    add ("Input gain", -34, 12, 0, "dB");                                // 8
    add ("Dry gain", -40, 6, 0, "dB");                                   // 9
    add ("Granulator gain", -40, 6, 0, "dB");                            // 10
    add ("Record", 0, 1, 0, {}, { "Off", "On" });                        // 11
    add ("Spawn mode", 0, 4, 0, {}, { "Stochastic", "Mid-grain", "Stochastic (fixed)", "Mid-grain (fixed)", "Single" }); // 12
    add ("Rate mean", 1, 1000, 5, "ms");                                 // 13
    add ("Rate spread", 0, 200, 10, "%");                                // 14
    add ("Size mean", 0, 1000, 100, "ms");                               // 15
    defs.back().fmt = [] (int v) { return v == 0 ? juce::String ("1 cycle") : juce::String ("ms"); };
    add ("Size spread", 0, 200, 10, "%");                                // 16
    add ("Pitch mean", -24, 24, 0, "ST");                                // 17
    add ("Pitch spread", 0, 1200, 0, "cents");                           // 18
    add ("Pan mean", -100, 100, 0, "%");                                 // 19
    add ("Pan spread", 0, 100, 10, "%");                                 // 20
    // 21/22 are stored as 0..5000: milliseconds, or 1/50 % steps (so 2500 = 50% = the manual's defaults in both units)
    auto delayFmt = [this] (int v) { return pv (77) ? juce::String ("ms") : juce::String (v / 50.0, 1) + "%"; };
    add ("Delay mean", 0, 5000, 2500); defs.back().fmt = delayFmt;       // 21
    add ("Delay spread", 0, 5000, 250); defs.back().fmt = delayFmt;      // 22
    add ("Shape", 0, 5, 0, {}, { "Gaussian", "Tukey", "Triangle", "Expodec", "Rexpodec", "Equal power" }); // 23
    add ("Opacity", 0, 100, 100, "%");                                   // 24
    add ("LFO depth", -100, 100, 0, "%");                                // 25
    add ("LFO speed", 0, 255, 196);                                      // 26
    add ("Input feedback", 0, 100, 0, "%");                              // 27
    add ("Main feedback", 0, 100, 0, "%");                               // 28
    add ("Attack time", 0, 127, 64);                                     // 29
    add ("Release time", 0, 127, 64);                                    // 30
    add ("Env -> opacity", 0, 100, 0, "%");                              // 31
    add ("Env -> level", 0, 100, 100, "%");                              // 32
    add ("Veloc -> level", 0, 100, 100, "%");                            // 33
    add ("Veloc -> delay", -100, 100, 0, "%");                           // 34
    add ("Pitch -> pitch", -100, 100, 100, "%");                         // 35
    add ("Pitch -> delay", -100, 100, 0, "%");                           // 36
    add ("Normalize", 0, 1, 1, {}, { "Off", "On" });                     // 37
    auto noteFmt = [] (int v) { return noteName (v); };
    add ("Natural pitch", 0, 127, 48, "ST"); defs.back().fmt = noteFmt;  // 38
    for (int i = 1; i <= 3; ++i) { add ("Drone " + juce::String (i) + " pitch", 0, 127, i == 1 ? 48 : i == 2 ? 36 : 60, "ST"); defs.back().fmt = noteFmt; } // 39-41
    for (int i = 1; i <= 3; ++i) add ("Drone " + juce::String (i) + " enable", 0, 1, 0, {}, { "Off", "On" });   // 42-44
    for (int i = 1; i <= 3; ++i) add ("Drone " + juce::String (i) + " opacity", 0, 100, 100, "%");              // 45-47
    for (int i = 1; i <= 3; ++i) add ("Drone " + juce::String (i) + " level", -40, 6, 0, "dB");                 // 48-50
    add ("Out delay mode", 0, 2, 0, {}, { "Off", "Stereo", "Ping-pong" });   // 51
    add ("Out delay level", -40, 0, -3, "dB");                           // 52
    add ("Out delay time", 1, 2730, 500, "ms");                          // 53
    defs.back().fmt = [this] (int v) { return delayUsesMidiClock ? "MIDI " + delayDivisionName (v) : juce::String ("ms"); };
    add ("Out delay feedback", 0, 100, 50, "%");                         // 54
    add ("Reverb mix", 0, 100, 50, "%");                                 // 55
    add ("Reverb time", 400, 30000, 1700, "ms");                         // 56
    add ("Grain limit", 1, 40, 40);                                      // 57
    add ("Bend range", 0, 48, 2, "ST");                                  // 58
    add ("Reverse", 0, 100, 0, "%");                                     // 59
    add ("Pitch quantize", 0, 5, 0, {}, { "Off", "Octaves", "Fourths", "Fifths", "Major Triad", "Minor Triad" }); // 60
    add ("LFO shape", 0, 2, 0, {}, { "Triangle", "Ramp up", "Ramp down" });  // 61
    add ("Record fade", 0, 1000, 5, "ms");                               // 62
    add ("Reserved 63", 0, 1, 0);                                        // 63
    add ("Reserved 64", 0, 1, 0);                                        // 64
    add ("Input mode", 0, 3, 1);                                         // 65
    add ("Mono input", 0, 1, 0, {}, { "Stereo", "Mono (L)" });           // 66
    add ("Sustain mode", 0, 1, 0, {}, { "Synth", "Piano" });             // 67
    add ("Delay mean input", 0, 12, 1);                                  // 68
    add ("Effects routing", 0, 2, 0, {}, { "Dly>Rev>Mix", "Dly>Mix>Rev", "Mix>Dly>Rev" }); // 69
    add ("Reverb model", 0, 3, 1); defs.back().fmt = [] (int v) { return juce::String (ExReverb::modelName (v)); }; // 70
    add ("Reverb size", 1, 100, 100, "%");                               // 71
    add ("Reverb high damp", 0, 100, 60);                                // 72
    add ("Reverb mod speed", 1, 500, 250);                               // 73
    defs.back().fmt = [] (int v) { return juce::String (v / 100.0, 2) + "Hz"; };
    add ("Reverb mod depth", 0, 100, 25);                                // 74
    add ("Reverb early gain", -40, 6, -12, "dB");                        // 75
    add ("Reverb diff gain", -40, 6, -6, "dB");                          // 76
    add ("Delay unit", 0, 1, 0, {}, { "%", "ms" });                      // 77

    defaultL = 21;
    sections = {
        { "BUFFER", { 7, 8, 11, 62, 27, 28, 66 } },
        { "GRAINS", { 12, 13, 14, 15, 16, 23, 24, 57, 59, 37 } },
        { "PITCH", { 17, 18, 60, 19, 20, 38, 58 } },
        { "POS", { 21, 22, 77, 25, 26, 61 } },
        { "MOD", { 29, 30, 31, 32, 33, 34, 35, 36, 67 } },
        { "DRONES", { 42, 39, 45, 48, 43, 40, 46, 49, 44, 41, 47, 50 } },
        { "FX", { 9, 10, 69, 51, 53, 54, 52, 55, 56 } },
        { "REVERB", { 70, 71, 72, 73, 74, 75, 76 } },
    };

    engine.startVoice = [this] (const PolyNoteEngine::VoiceStart& s)
    {
        auto& nt = notes[s.voice];
        nt.active = true; nt.gate = true; nt.first = true;
        nt.pitch = s.pitch; nt.vel = s.velocity; nt.countdown = 0; nt.refW = w;
        nt.extLevel = 1.0f; nt.extOpacity = 1.0f;
    };
    engine.releaseVoice = [this] (int vi) { notes[vi].gate = false; };
    engine.voiceSounding = [this] (int vi) { return notes[vi].active; };
}

Granulator::~Granulator()
{
    alive->store (false);
    stopJobs();
}

void Granulator::prepare (double sampleRate, int block)
{
    preparePoly (sampleRate, block);
    maxLen = (int) (kMaxBufferSeconds * sampleRate) + 1;
    bool fresh = false;
    if ((int) buf.size() != maxLen + 4) { buf.assign ((size_t) maxLen + 4, 0.0f); fresh = true; fileLen = 0; synced = false; }
    bufLen = juce::jlimit (64, maxLen, (int) (pv (7) * 0.001 * sampleRate));
    w = juce::jlimit (0, bufLen - 1, w);
    for (auto& g : grains) g = Grain();
    for (auto& nt : notes) nt = Note();
    for (auto& d : droneWasOn) d = false;
    recG = 0; normG = 1; lfoPhase = 0;
    delay.prepare (sampleRate, 10.5f);
    reverb.prepare (sampleRate);
    if (folders.isEmpty()) onSelected();
    prepared = true;
    // a buffer that came from a file is reloaded (resampled for this rate) when the buffer had to be rebuilt
    if (fresh && currentPath().isNotEmpty() && juce::File (savedPath).existsAsFile()) startLoad (juce::File (savedPath), savedRaw, false);
}

// ------------------------------------------------------------------------------------------ files
void Granulator::startLoad (const juce::File& f, bool raw, bool announce)
{
    loading = true;
    const double rate = sr;
    const int limit = (int) (kMaxBufferSeconds * rate);
    auto tok = alive;
    runAsync ([this, f, raw, announce, rate, limit, tok]
    {
        WavData wv;
        const bool ok = loadWav (f, wv, 6200000) && wv.frames > 1;
        int ms = 0;
        if (ok)
        {
            auto ld = std::make_shared<Loaded>();
            const double step = raw ? 1.0 : wv.sampleRate / rate;
            const int frames = (int) juce::jmin ((int64_t) limit, (int64_t) ((double) (wv.frames - 1) / step));
            ld->data.resize ((size_t) juce::jmax (1, frames));
            for (int i = 0; i < frames; ++i)
            {
                const double p = i * step;
                const int64_t i0 = (int64_t) p;
                const float fr = (float) (p - (double) i0);
                const float a = wv.sample (i0, 0), b = wv.sample (juce::jmin (wv.frames - 1, i0 + 1), 0);
                ld->data[(size_t) i] = a + fr * (b - a);
            }
            ms = juce::jlimit (100, 31250, juce::roundToInt (frames * 1000.0 / rate));
            ld->ms = ms; ld->rate = rate;
            std::atomic_store (&keep, ld);      // keeps the block alive so the audio thread never frees it
            std::atomic_store (&pending, ld);
        }
        loading = false;
        juce::MessageManager::callAsync ([this, tok, ok, ms, announce, name = f.getFileName()]
        {
            if (! tok->load()) return;
            if (ok) set (7, ms);
            else { savedPath = {}; }
            if (announce) plugin.message (ok ? "Loaded " + name : "Can't load " + name);
        });
    });
}

void Granulator::loadFile (const juce::File& f, bool raw)
{
    if (! f.existsAsFile()) return;
    savedPath = f.getFullPathName(); savedRaw = raw; pathInvalid = false;
    startLoad (f, raw, true);
}

void Granulator::saveBuffer()
{
    const int len = dispLen.load(), wp = dispW.load();
    if (len <= 0 || (int) buf.size() < len) return;
    auto data = std::make_shared<std::vector<float>> ((size_t) len);
    for (int i = 0; i < len; ++i) (*data)[(size_t) i] = buf[(size_t) ((wp + i) % len)];   // oldest audio first
    const double rate = sr;
    auto dir = SdCard::root().getChildFile ("!GRANUL8");
    auto tok = alive;
    runAsync ([this, data, rate, dir, tok]
    {
        dir.createDirectory();
        int n = 1;
        auto fileFor = [&] (int k) { return dir.getChildFile ("GRAN" + juce::String (k).paddedLeft ('0', 4) + ".WAV"); };
        while (fileFor (n).exists()) ++n;
        auto f = fileFor (n);
        bool ok = false;
        {
            std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
            juce::WavAudioFormat fmt;
            auto wr = fmt.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
            const float* ch[1] = { data->data() };
            if (wr) ok = wr->writeFromFloatArrays (ch, 1, (int) data->size());
        }
        juce::MessageManager::callAsync ([this, tok, ok, f]
        {
            if (! tok->load()) return;
            if (ok) { savedPath = f.getFullPathName(); savedRaw = false; pathInvalid = false; onSelected(); }
            plugin.message (ok ? "Saved " + f.getFileName() : juce::String ("Save failed"), 2.5);
        });
    });
}

void Granulator::clearBuffer()
{
    clearRequest = true;
    savedPath = {};
    plugin.message ("Buffer cleared");
}

bool Granulator::importFiles (const juce::StringArray& paths)
{
    for (auto& p : paths)
    {
        juce::File f (p);
        if (SdCard::isWav (f) && f.existsAsFile()) { loadFile (f, false); return true; }
    }
    return false;
}

void Granulator::chooseFile (bool raw)
{
    chooser = std::make_unique<juce::FileChooser> (raw ? "Load WAV (raw)" : "Load WAV", SdCard::root(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, raw] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.existsAsFile()) loadFile (f, raw);
    });
}

void Granulator::refreshFiles()
{
    files.clear();
    if (folders.isEmpty()) return;
    folderIdx = juce::jlimit (0, folders.size() - 1, folderIdx);
    for (auto& f : SdCard::sortedChildren (folders[folderIdx], juce::File::findFiles, "*.wav"))
        if (SdCard::isWav (f)) files.add (f);
}

void Granulator::onSelected()
{
    folders = SdCard::sampleFolders();
    for (auto* name : { "!GRANUL8", "!RECORD" })
    {
        auto d = SdCard::root().getChildFile (name);
        if (! d.isDirectory() || folders.contains (d)) continue;
        for (auto& f : d.findChildFiles (juce::File::findFiles, false, "*.wav"))
            if (SdCard::isWav (f)) { folders.add (d); break; }
    }
    refreshFiles();
}

void Granulator::saveExtra (juce::XmlElement& x) const
{
    x.setAttribute ("granPath", currentPath());
    x.setAttribute ("granRaw", savedRaw);
    x.setAttribute ("granFolder", folderIdx);
    x.setAttribute ("autoVisuals", autoVisuals);
}

void Granulator::loadExtra (const juce::XmlElement& x)
{
    savedPath = x.getStringAttribute ("granPath");
    savedRaw = x.getBoolAttribute ("granRaw", false);
    pathInvalid = false;
    folderIdx = x.getIntAttribute ("granFolder", 0);
    autoVisuals = x.getIntAttribute ("autoVisuals", 0);
    refreshFiles();
    if (prepared && savedPath.isNotEmpty() && juce::File (savedPath).existsAsFile()) startLoad (juce::File (savedPath), savedRaw, false);
}

// ------------------------------------------------------------------------------------------ audio
float Granulator::delayFraction (int param) const
{
    const float v = (float) pv (param);
    return pv (77) ? v * 0.001f * (float) sr / (float) bufLen : v / 5000.0f;
}

void Granulator::configure()
{
    engine.setSustain (ccSustain);
    engine.maxVoices = PolyNoteEngine::kMaxVoices;
    engine.unison = 1;
    engine.transpose = 0; engine.fineCents = 0;
    engine.sustainMode = pv (67);
    engine.triggerMode = 0;
    engine.velCurve = 0;
    engine.tuning = nullptr;

    if (auto ld = std::atomic_exchange (&pending, std::shared_ptr<Loaded>()))
    {
        if (std::abs (ld->rate - sr) < 1.0 && ! buf.empty())
        {
            const int n = juce::jmin ((int) ld->data.size(), maxLen);
            std::memcpy (buf.data(), ld->data.data(), sizeof (float) * (size_t) n);
            std::fill (buf.begin() + n, buf.end(), 0.0f);
            fileLen = n; fileMs = ld->ms; synced = false; w = 0;
            for (auto& g : grains) g.active = false;
        }
    }
    if (clearRequest.exchange (false))
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        fileLen = 0; synced = false;
        for (auto& g : grains) g.active = false;
    }
    // a loaded file sets the exact length until the Buffer size parameter is moved away from the file's value
    if (fileLen > 0)
    {
        if (pv (7) == fileMs) synced = true;
        else if (synced) { fileLen = 0; synced = false; pathInvalid = true; }
    }
    const int len = fileLen > 0 ? fileLen : juce::jlimit (64, juce::jmax (64, maxLen), (int) (pv (7) * 0.001 * sr));
    if (len != bufLen)
    {
        bufLen = len;
        w %= bufLen;
        for (auto& nt : notes) nt.refW %= bufLen;
    }
    dispLen = bufLen;
    reverb.set (pv (70), (float) pv (56), (float) pv (71), (float) pv (72), (float) pv (73), (float) pv (74), (float) pv (75), (float) pv (76));
}

bool Granulator::spawn (int ni, bool midPending, bool audible)
{
    const int limit = pv (57);
    int slot = -1, count = 0;
    for (int i = 0; i < kMaxGrains; ++i) { if (grains[i].active) ++count; else if (slot < 0) slot = i; }
    if (slot < 0 || count >= limit) return false;
    auto& nt = notes[ni];
    auto& g = grains[slot];
    const float len = (float) bufLen, natural = (float) pv (38);
    const float p = nt.pitch + (ni < PolyNoteEngine::kMaxVoices ? bendSemis : 0.0f);
    const float dev = quantizeDeviation ((float) pv (17) + (float) pv (18) * 0.01f * rnd2(), pv (60));
    const float semis = (p - natural) * (float) pv (35) * 0.01f + dev;
    const float ratio = juce::jlimit (1.0f / 64.0f, 64.0f, std::pow (2.0f, semis / 12.0f));
    // size 0: one cycle of the note's pitch
    float size = pv (15) <= 0 ? (float) sr / juce::jmax (8.0f, noteHz (p)) : (float) pv (15) * 0.001f * (float) sr;
    size *= 1.0f + (float) pv (16) * 0.01f * rnd2();
    g.len = juce::jlimit (8, juce::jmax (8, bufLen - 2), (int) size);
    const bool reverse = rnd() * 100.0f < (float) pv (59);
    const int mode = pv (12);
    const bool fixed = (mode == 2 || mode == 3) && pv (11) != 0;
    const int ls = pv (61);
    const float lfo = ls == 0 ? (lfoPhase < 0.5f ? 4.0f * lfoPhase - 1.0f : 3.0f - 4.0f * lfoPhase) : ls == 1 ? 2.0f * lfoPhase - 1.0f : 1.0f - 2.0f * lfoPhase;
    float frac = delayFraction (21) + delayFraction (22) * rnd2() - (float) pv (25) * 0.01f * lfo
               + (float) pv (34) * 0.01f * nt.vel + (float) pv (36) * 0.01f * (p - natural) / 48.0f;
    frac -= std::floor (frac);
    // the grain covers [delay + size, delay] behind the reference point (at normal pitch)
    double pos = (double) (fixed ? nt.refW : w) - (double) (frac * len) - (reverse ? 0.0 : (double) g.len);
    pos = std::fmod (pos, (double) bufLen);
    if (pos < 0) pos += bufLen;
    g.pos = pos;
    g.inc = reverse ? -(double) ratio : (double) ratio;
    g.age = 0; g.owner = ni; g.shape = pv (23); g.midPending = midPending;
    if (audible) panGains (juce::jlimit (-1.0f, 1.0f, (float) pv (19) * 0.01f + (float) pv (20) * 0.01f * rnd2()), g.gl, g.gr);
    else g.gl = g.gr = 0.0f;
    g.active = true;
    return true;
}

void Granulator::renderVoices (float* const* out, int start, int n)
{
    if (buf.empty() || n > 32) return;
    float gl[32] {}, gr[32] {};
    const int mode = pv (12);
    const bool midMode = mode == 1 || mode == 3;

    // drones are notes 17-19
    for (int d = 0; d < 3; ++d)
    {
        auto& nt = notes[PolyNoteEngine::kMaxVoices + d];
        const bool on = pv (42 + d) != 0;
        if (on && ! droneWasOn[d]) { nt.active = true; nt.first = true; nt.countdown = 0; nt.refW = w; }
        droneWasOn[d] = on;
        nt.gate = on;
        nt.pitch = (float) pv (39 + d); nt.vel = 1.0f;
        nt.extOpacity = (float) pv (45 + d) * 0.01f;
        nt.extLevel = pv (48 + d) <= -40 ? 0.0f : dbToGain ((float) pv (48 + d));
    }

    // mid-grain chains: a note keeps exactly one grain that has not reached its middle yet
    for (auto& nt : notes) nt.chain = false;
    for (auto& g : grains)
    {
        if (! g.active || ! g.midPending) continue;
        if (g.age >= g.len / 2) g.midPending = false;
        else notes[g.owner].chain = true;
    }

    const float aStep = (float) n / (envSeconds (pv (29)) * (float) sr), rStep = (float) n / (envSeconds (pv (30)) * (float) sr);
    const float envOp = (float) pv (31) * 0.01f, envLv = (float) pv (32) * 0.01f, velLv = (float) pv (33) * 0.01f;
    const float opacity = (float) pv (24) * 0.01f;
    const float rateS = (float) pv (13) * 0.001f * (float) sr, rateSpread = (float) pv (14) * 0.01f;
    for (int ni = 0; ni < kNotes; ++ni)
    {
        auto& nt = notes[ni];
        const bool isVoice = ni < PolyNoteEngine::kMaxVoices;
        if (! nt.active) { if (isVoice) voiceLevel[ni] = 0.0f; continue; }
        if (nt.gate) nt.env = juce::jmin (1.0f, nt.env + aStep);
        else
        {
            nt.env -= rStep;
            if (nt.env <= 0.0f)
            {
                nt.env = 0; nt.active = false;
                if (isVoice) { voiceLevel[ni] = 0.0f; if (! engine.voiceGate[ni]) engine.voiceFinished (ni); }
                continue;
            }
        }
        if (isVoice) voiceLevel[ni] = nt.env;
        nt.level = (1.0f - envLv * (1.0f - nt.env)) * (1.0f - velLv * (1.0f - nt.vel)) * nt.extLevel;
        nt.opacity = opacity * nt.extOpacity * (1.0f - envOp * (1.0f - nt.env));
        if (mode == 4)
        {
            if (nt.first && rnd() < nt.opacity) spawn (ni, false, true);
        }
        else if (midMode)
        {
            if (! nt.chain) spawn (ni, true, rnd() < nt.opacity);   // a silent grain keeps the chain running
        }
        else
        {
            nt.countdown -= n;
            for (int k = 0; k < 8 && nt.countdown <= 0; ++k)
            {
                if (rnd() < nt.opacity) spawn (ni, false, true);
                nt.countdown += juce::jmax (1.0f, rateS * (1.0f + rateSpread * rnd2()));
            }
            if (nt.countdown < 0) nt.countdown = 0;
        }
        nt.first = false;
    }

    // grains
    const float* b = buf.data();
    const int len = bufLen;
    const double dlen = (double) len;
    int active = 0;
    for (int gi = 0; gi < kMaxGrains; ++gi)
    {
        auto& g = grains[gi];
        if (! g.active) { dispGrain[gi].store (-1.0f, std::memory_order_relaxed); continue; }
        ++active;
        if (g.pos >= dlen) g.pos = std::fmod (g.pos, dlen);
        const float* win = window (g.shape);
        const float lvl = notes[g.owner].level, scale = (float) kWin / (float) g.len;
        const float al = g.gl * lvl, ar = g.gr * lvl;
        for (int i = 0; i < n; ++i)
        {
            const int i0 = (int) g.pos;
            const float fr = (float) (g.pos - i0);
            const int i1 = i0 + 1 >= len ? 0 : i0 + 1;
            float s = b[i0] + fr * (b[i1] - b[i0]);
            const float wp = (float) g.age * scale;
            const int w0 = (int) wp;
            s *= win[w0] + (wp - (float) w0) * (win[w0 + 1] - win[w0]);
            gl[i] += s * al; gr[i] += s * ar;
            g.pos += g.inc;
            if (g.pos >= dlen) g.pos -= dlen; else if (g.pos < 0) g.pos += dlen;
            if (++g.age >= g.len) { g.active = false; break; }
        }
        double behind = (double) w - g.pos;
        if (behind < 0) behind += dlen;
        dispGrain[gi].store (g.active ? (float) (behind / dlen) : -1.0f, std::memory_order_relaxed);
    }
    dispCount = active;
    const float normTarget = pv (37) ? 1.0f / std::sqrt ((float) juce::jmax (1, active)) : 1.0f;

    // record + output
    const float inGain = pv (8) <= -34 ? 0.0f : dbToGain ((float) pv (8));
    const float inFb = (float) pv (27) * 0.01f, mainFb = (float) pv (28) * 0.01f;
    const float recTarget = pv (11) ? 1.0f : 0.0f;
    const float recStep = pv (62) <= 0 ? 1.0f : 1.0f / ((float) pv (62) * 0.001f * (float) sr);
    const bool mono = pv (66) != 0;
    const float* inL = inBuffer != nullptr ? inBuffer->getReadPointer (0) + start : nullptr;
    const float* inR = inBuffer != nullptr ? inBuffer->getReadPointer (1) + start : nullptr;
    for (int i = 0; i < n; ++i)
    {
        normG += (normTarget - normG) * 0.002f;
        const float l = gl[i] * normG, r = gr[i] * normG;
        out[2][i] += l; out[3][i] += r;
        if (recG < recTarget) recG = juce::jmin (recTarget, recG + recStep);
        else if (recG > recTarget) recG = juce::jmax (recTarget, recG - recStep);
        if (recG > 0.0f)
        {
            const float x = inL == nullptr ? 0.0f : mono ? inL[i] : 0.5f * (inL[i] + inR[i]);
            const float old = buf[(size_t) w];
            const float nw = juce::jlimit (-4.0f, 4.0f, x * inGain + inFb * old + mainFb * 0.5f * (l + r));
            buf[(size_t) w] = old + recG * (nw - old);
            if (++w >= len) w = 0;
        }
    }
    dispW = w;

    // delay LFO: at speed 196 and full depth the play head moves at 1x
    const int speed = pv (26);
    if (speed > 0)
    {
        const float inc = std::pow (2.0f, (float) (speed - 196) / 24.0f) / ((float) len * (pv (61) == 0 ? 4.0f : 2.0f));
        lfoPhase += inc * (float) n;
        lfoPhase -= std::floor (lfoPhase);
    }
}

void Granulator::postProcess (float* const* out, int n)
{
    const float dryG = pv (9) <= -40 ? 0.0f : dbToGain ((float) pv (9));
    const float granG = pv (10) <= -40 ? 0.0f : dbToGain ((float) pv (10));
    const int routing = pv (69), dMode = pv (51);
    const float dLevel = (float) pv (52), dTime = (float) delayMs (pv (53)), dFb = (float) pv (54) * 0.01f;
    const float mix = (float) pv (55) * 0.01f;
    const bool mono = pv (66) != 0;
    for (int pos = 0; pos < n; pos += 256)
    {
        const int m = juce::jmin (256, n - pos);
        float xl[256], xr[256], dl[256], dr[256];
        const float* inL = inBuffer != nullptr ? inBuffer->getReadPointer (0) + pos : nullptr;
        const float* inR = inBuffer != nullptr ? inBuffer->getReadPointer (1) + pos : nullptr;
        for (int i = 0; i < m; ++i)
        {
            dl[i] = inL != nullptr ? inL[i] * dryG : 0.0f;
            dr[i] = inL != nullptr ? (mono ? inL[i] : inR[i]) * dryG : 0.0f;
            xl[i] = out[2][pos + i] * granG; xr[i] = out[3][pos + i] * granG;
            if (routing == 2) { xl[i] += dl[i]; xr[i] += dr[i]; }
        }
        delay.process (xl, xr, m, dMode, dLevel, dTime, dFb);
        if (routing == 1) for (int i = 0; i < m; ++i) { xl[i] += dl[i]; xr[i] += dr[i]; }
        for (int i = 0; i < m; ++i)
        {
            float wl = 0, wr = 0;
            reverb.process (xl[i], xr[i], wl, wr);
            float l = xl[i] + mix * (wl - xl[i]), r = xr[i] + mix * (wr - xr[i]);
            if (routing == 0) { l += dl[i]; r += dr[i]; }
            out[0][pos + i] = l; out[1][pos + i] = r;
        }
    }
}

// ------------------------------------------------------------------------------------------ UI
std::vector<BrowserSpec> Granulator::browsers()
{
    BrowserSpec a;
    a.title = "Folders";
    a.items = [this] { juce::StringArray s; for (auto& f : folders) s.add (f.getFileName()); return s; };
    a.selected = [this] { return folderIdx; };
    a.choose = [this] (int i) { folderIdx = i; refreshFiles(); };
    BrowserSpec b;
    b.title = "WAV files";
    b.items = [this] { juce::StringArray s; for (auto& f : files) s.add (f.getFileNameWithoutExtension()); return s; };
    b.selected = [this] { return files.indexOf (juce::File (currentPath())); };
    b.choose = [this] (int i) { if (i >= 0 && i < files.size()) loadFile (files[i], false); };
    return { a, b };
}

std::vector<ActionSpec> Granulator::actions()
{
    std::vector<ActionSpec> a;
    a.push_back ({ "Record", [this] { pushL(); }, [this] { return pv (11) != 0; }, true });
    a.push_back ({ "Visuals", [this] { if (shown) { visuals = false; autoSuppressed = true; } else { visuals = true; autoSuppressed = false; } },
                   [this] { return shown; } });
    for (int d = 0; d < 3; ++d)
        a.push_back ({ "Drone " + juce::String (d + 1), [this, d] { set (42 + d, pv (42 + d) ? 0 : 1); }, [this, d] { return pv (42 + d) != 0; } });
    a.push_back ({ "Save", [this] { saveBuffer(); }, {} });
    a.push_back ({ "Clear", [this] { clearBuffer(); }, {} });
    a.push_back ({ "Notes off", [this] { plugin.allNotesOffRequest = true; }, {} });
    return a;
}

juce::String Granulator::statusText()
{
    if (loading.load()) return "Loading WAV...";
    auto p = currentPath();
    return p.isEmpty() ? juce::String() : "Buffer: " + juce::File (p).getFileName();
}

void Granulator::addMenuItems (std::vector<MenuItem>& items)
{
    items.push_back (MenuItem::sub ("Load", {
        MenuItem::act ("Load WAV", [this] { chooseFile (false); }),
        MenuItem::act ("Load WAV (raw)", [this] { chooseFile (true); }),
    }));
    items.push_back (MenuItem::act ("Save", [this] { saveBuffer(); }));
    items.push_back (MenuItem::act ("View saved path", [this] {
        auto p = currentPath();
        plugin.message (p.isEmpty() ? juce::String ("<none>") : juce::File (p).getParentDirectory().getFileName() + "/" + juce::File (p).getFileName(), 3.0); }));
    items.push_back (MenuItem::act ("Clear buffer", [this] { clearBuffer(); }));
    items.push_back (MenuItem::val ("Auto visuals", 0, 60, [this] { return autoVisuals; }, [this] (int v) { autoVisuals = v; autoSuppressed = false; },
                                    [] (int v) { return v == 0 ? juce::String ("Off") : juce::String (v) + " s"; }));
    addStandardPolyMenu (items, true, false);
}

bool Granulator::pushR (int cur)
{
    if (cur != 53) return false;
    double now = juce::Time::getMillisecondCounterHiRes(), dt = now - lastTap; lastTap = now;
    if (dt > 30 && dt < 2730 && ! delayUsesMidiClock) set (53, (int) dt);
    return true;
}

void Granulator::drawVisuals (Oled& o)
{
    const int len = dispLen.load(), wp = dispW.load();
    if (len <= 0 || (int) buf.size() < len) return;
    // newest audio (the write position) is at the right edge
    for (int x = 0; x < 128; ++x)
    {
        const int d0 = (int) ((int64_t) (127 - x) * len / 128), d1 = (int) ((int64_t) (128 - x) * len / 128);
        const int step = juce::jmax (1, (d1 - d0) / 12);
        float pk = 0;
        for (int d = d0; d < d1; d += step)
        {
            int idx = wp - 1 - d;
            while (idx < 0) idx += len;
            pk = std::max (pk, std::abs (buf[(size_t) idx]));
        }
        const int h = juce::jlimit (0, 7, (int) (std::sqrt (pk) * 8.0f));
        o.vline (x, 16 - h, 15 + h);
        if (h == 0 && (x & 1)) o.set (x, 16);   // silence: dotted centre line
    }
    for (int i = 0; i < kMaxGrains; ++i)
    {
        const float f = dispGrain[i].load (std::memory_order_relaxed);
        if (f < 0) continue;
        const int x = 127 - (int) (f * 127.0f), y = 9 + (i * 5) % 13;
        o.flip (x, y); o.flip (x, y + 1);
    }
    const float mean = delayFraction (21), size = (float) pv (15) * 0.001f * (float) sr / (float) len;
    for (float f : { mean, mean + size })
    {
        const int x = 127 - (int) (juce::jlimit (0.0f, 1.0f, f) * 127.0f);
        for (int y = 8; y < 24; y += 2) o.set (x, y);
    }
    o.at (0, 0, juce::String (dispCount.load()));
    if (pv (11)) o.right (21, 0, "REC");
    o.at (0, 3, juce::String (juce::roundToInt (mean * (float) len * 1000.0f / (float) sr)));
    o.right (21, 3, juce::String (juce::roundToInt ((float) len * 1000.0f / (float) sr)));
}

void Granulator::drawPage (Oled& o, int cur)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const int sig = cur * 100003 + pv (cur);
    if (sig != lastSig) { lastSig = sig; lastChange = now; }
    shown = visuals || (autoVisuals > 0 && ! autoSuppressed && now - lastChange >= autoVisuals * 1000.0);
    if (shown) { drawVisuals (o); return; }
    PolyAlgorithm::drawPage (o, cur);
    drawVoiceLine (o);
}
} // namespace ex

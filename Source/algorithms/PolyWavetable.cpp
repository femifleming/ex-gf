#include "PolyWavetable.h"

namespace ex
{
// ------------------------------------------------------------------------------------------------ tables
static void buildMips (const std::vector<float>& cycle, float* dst)
{
    // resample the single cycle to kLen (periodic cubic), then band-limit per level via FFT
    const int L = (int) cycle.size();
    std::vector<float> base ((size_t) WaveTable::kLen);
    for (int i = 0; i < WaveTable::kLen; ++i)
    {
        double p = (double) i * L / WaveTable::kLen;
        int i1 = (int) p; float f = (float) (p - i1);
        auto s = [&] (int k) { return cycle[(size_t) (((k % L) + L) % L)]; };
        float y0 = s (i1 - 1), y1 = s (i1), y2 = s (i1 + 1), y3 = s (i1 + 2);
        float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        base[(size_t) i] = ((c3 * f + c2) * f + c1) * f + y1;
    }
    // remove DC, normalise
    float mean = 0; for (float v : base) mean += v; mean /= WaveTable::kLen;
    float peak = 0; for (auto& v : base) { v -= mean; peak = std::max (peak, std::abs (v)); }
    if (peak > 1e-6f) for (auto& v : base) v /= peak;
    juce::dsp::FFT fft (11);
    std::vector<float> spec ((size_t) WaveTable::kLen * 2, 0.0f);
    std::copy (base.begin(), base.end(), spec.begin());
    fft.performRealOnlyForwardTransform (spec.data(), true);
    float norm = 1.0f;
    for (int level = 0; level < WaveTable::kLevels; ++level)
    {
        int maxH = 1024 >> level;
        std::vector<float> s2 = spec;
        for (int h = maxH + 1; h <= WaveTable::kLen / 2; ++h) { s2[(size_t) h * 2] = 0; s2[(size_t) h * 2 + 1] = 0; }
        // mirror part (performRealOnlyInverseTransform uses the first half)
        fft.performRealOnlyInverseTransform (s2.data());
        if (level == 0)
        {
            float pk = 0; for (int i = 0; i < WaveTable::kLen; ++i) pk = std::max (pk, std::abs (s2[(size_t) i]));
            norm = pk > 1e-9f ? 1.0f / pk : 1.0f;
        }
        for (int i = 0; i < WaveTable::kLen; ++i) dst[(size_t) level * WaveTable::kLen + (size_t) i] = s2[(size_t) i] * norm;
    }
}

std::shared_ptr<WaveTable> WaveTable::load (const juce::File& entry, int wavelength)
{
    std::vector<std::vector<float>> cycles;
    if (entry.isDirectory())
    {
        for (auto& f : SdCard::sortedChildren (entry, juce::File::findFiles, "*.wav"))
        {
            if (! SdCard::isWav (f)) continue;
            WavData w;
            if (loadWav (f, w) && w.frames > 1)
            {
                std::vector<float> c ((size_t) w.frames);
                for (int64_t i = 0; i < w.frames; ++i) c[(size_t) i] = w.sample (i, 0);
                cycles.push_back (std::move (c));
            }
            if (cycles.size() >= 256) break;
        }
    }
    else
    {
        WavData w;
        if (loadWav (entry, w) && wavelength <= 0)
            wavelength = (w.frames % 600 == 0) ? 600 : (w.frames % 256 == 0) ? 256 : (w.frames % 2048 == 0 ? 2048 : 600);
        wavelength = juce::jmax (8, wavelength);
        if (w.frames >= wavelength)
            for (int64_t s = 0; s + wavelength <= w.frames && cycles.size() < 256; s += wavelength)
            {
                std::vector<float> c ((size_t) wavelength);
                for (int i = 0; i < wavelength; ++i) c[(size_t) i] = w.sample (s + i, 0);
                cycles.push_back (std::move (c));
            }
    }
    if (cycles.empty()) return nullptr;
    auto t = std::make_shared<WaveTable>();
    t->name = entry.getFileNameWithoutExtension();
    t->numWaves = (int) cycles.size();
    t->data.resize ((size_t) t->numWaves * kLevels * kLen);
    for (int i = 0; i < t->numWaves; ++i) buildMips (cycles[(size_t) i], t->data.data() + (size_t) i * kLevels * kLen);
    return t;
}

std::shared_ptr<WaveTable> WaveTable::fallback()
{
    // sine -> triangle -> saw -> square morph, used when the card has no wavetables
    std::vector<std::vector<float>> cycles;
    const int L = 600;
    for (int k = 0; k < 4; ++k)
    {
        std::vector<float> c (L);
        for (int i = 0; i < L; ++i)
        {
            float ph = (float) i / L;
            float v = k == 0 ? std::sin (2 * kPi * ph) : k == 1 ? (ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph) : k == 2 ? 2 * ph - 1 : (ph < 0.5f ? 1.0f : -1.0f);
            c[(size_t) i] = v;
        }
        cycles.push_back (c);
    }
    auto t = std::make_shared<WaveTable>();
    t->name = "(built-in)"; t->numWaves = 4;
    t->data.resize ((size_t) 4 * kLevels * kLen);
    for (int i = 0; i < 4; ++i) buildMips (cycles[(size_t) i], t->data.data() + (size_t) i * kLevels * kLen);
    return t;
}

// ------------------------------------------------------------------------------------------------ algorithm
PolyWavetable::PolyWavetable (ExPlugin& p) : PolyAlgorithm (p, 10, "Poly Wavetable")
{
    addAttenuverters();
    int wt = add ("Wavetable", 0, 999, 0);
    defs[(size_t) wt - 1].confirm = true;
    add ("Wave offset", -100, 100, 0);
    add ("Wave spread", -100, 100, 0);
    add ("Coarse tune", -60, 60, 0);
    add ("Fine tune", -100, 100, 0, "cents");
    add ("Attack time", 0, 127, 20); add ("Decay time", 0, 127, 60); add ("Sustain level", 0, 127, 80); add ("Release time", 0, 127, 60);
    add ("Attack shape", 0, 127, 64); add ("Decay shape", 0, 127, 64);
    add ("Attack time 2", 0, 127, 60); add ("Decay time 2", 0, 127, 70); add ("Sustain level 2", -127, 127, 64); add ("Release time 2", 0, 127, 50);
    add ("Attack shape 2", 0, 127, 64); add ("Decay shape 2", 0, 127, 64);
    add ("Filter type", 0, 3, 0, {}, { "Off", "Lowpass", "Bandpass", "Highpass" });
    add ("Filter freq", 0, 127, 64);
    add ("Filter Q", 0, 100, 50);
    add ("Veloc -> volume", 0, 100, 100, "%"); add ("Veloc -> wave", -100, 100, 0, "%"); add ("Veloc -> filter", -127, 127, 0);
    add ("Pitch -> wave", -100, 100, 0, "%"); add ("Pitch -> filter", -100, 100, 0, "%");
    add ("Env -> wave", -100, 100, 0, "%"); add ("Env -> filter", -127, 127, 0);
    add ("Env 2 -> wave", -100, 100, 0, "%"); add ("Env 2 -> filter", -127, 127, 0); add ("Env 2 -> pitch", -120, 120, 0);
    add ("LFO -> wave", -100, 100, 0, "%"); add ("LFO -> filter", -127, 127, 0); add ("LFO -> pitch", -120, 120, 0);
    add ("LFO speed", -100, 100, 90);
    add ("Gain", -40, 24, 0, "dB");
    add ("Sustain", 0, 1, 0, {}, { "Off", "On" });
    add ("Bend range", 0, 48, 2);
    add ("Chord enable", 0, 7, 0); defs.back().fmt = [] (int v) { return chordEnableName (v); };
    add ("Chord key", -12, 12, 0); defs.back().fmt = [] (int v) { return keyName (v); };
    add ("Chord scale", 0, 7, 0); defs.back().fmt = [] (int v) { return chordScaleName (v); };
    add ("Chord shape", 0, 13, 0); defs.back().fmt = [] (int v) { return chordShapeName (v); };
    add ("Chord inversion", 0, 3, 0);
    for (int i = 1; i <= 3; ++i) { add ("Arpeggio " + juce::String (i) + " mode", 0, 14, 0); defs.back().fmt = [] (int v) { return arpModeName (v); }; }
    for (int i = 1; i <= 3; ++i) add ("Arpeggio " + juce::String (i) + " range", 1, 3, 1);
    add ("Scala/MTS", -16, 99, 0); defs.back().fmt = [this] (int v) { return scalaName (v); };
    add ("Scala KBM", 0, 99, 0); defs.back().fmt = [this] (int v) { return kbmName (v); };
    add ("Chorus mode", 0, 2, 0, {}, { "Off", "I", "II" });
    add ("Delay mode", 0, 2, 0, {}, { "Off", "Stereo", "Ping-pong" });
    add ("Delay level", -40, 0, -3, "dB");
    add ("Delay time", 1, 10000, 500, "ms");
    defs.back().fmt = [this] (int v) { return delayUsesMidiClock ? "MIDI " + delayDivisionName (v) : juce::String ("ms"); };
    add ("Delay feedback", 0, 100, 50, "%");
    add ("Unison", 1, 8, 1);
    add ("Unison detune", 0, 100, 10, "cents");
    add ("Output spread", -100, 100, 0, "%");
    add ("Output mode", 0, 3, 0, {}, { "Spread voice", "Spread voice 2", "Spread pitch", "Per voice" });
    add ("Input mode", 0, 18, 1);
    add ("Sustain mode", 0, 1, 0, {}, { "Synth", "Piano" });
    add ("MIDI vel curve", 0, 3, 0, {}, { "Linear", "Soft", "Hard", "Fixed" });
    add ("Wave input", 0, 6, 5);
    add ("Pitch bend input", 0, 6, 0);
    add ("Arp reset input", 0, 6, 0);
    add ("Max voices", 1, 8, 8);
    add ("LFO retrigger", 0, 2, 0, {}, { "Poly", "Mono", "Off" });
    add ("LFO spread", 0, 90, 0, "deg");
    add ("Gate offset", 0, 496, 0);
    add ("Trigger mode", 0, 2, 0, {}, { "Gated", "Drone", "Triggered" });
    add ("Break time", 0, 1000, 0, "ms");
    add ("Break direction", 0, 2, 0, {}, { "Up", "Down", "Alternate" });
    defaultL = 8; // L knob -> Wave offset
    sections = {
        { "OSC", { 8, 9, 62, 63, 72, 41 } },
        { "ENV 1", { 12, 13, 14, 15, 16, 17 } },
        { "ENV 2", { 18, 19, 20, 21, 22, 23 } },
        { "FILTER", { 24, 25, 26, 29, 31, 33, 35, 38 } },
        { "MOD", { 27, 28, 30, 32, 34, 36, 37, 39, 40, 73, 74 } },
        { "CHORD", { 44, 45, 46, 47, 48, 51, 54, 77, 78 } },
        { "FX", { 57, 58, 60, 61, 59, 64, 65 } },
        { "KEYS", { 10, 11, 43, 42, 67, 68, 76, 55, 56 } },
    };

    engine.startVoice = [this] (const PolyNoteEngine::VoiceStart& s)
    {
        auto& v = voices[s.voice];
        bool wasActive = v.active && v.env1.active();
        v.active = true; v.killing = false; v.fade = 1.0f;
        v.pitch = s.pitch; v.note = s.note; v.vel = s.velocity; v.unison = s.unisonIndex;
        v.env1.oneShot = v.env2.oneShot = s.oneShot;
        v.env1.noteOn (wasActive); v.env2.noteOn (wasActive);
        const int retrig = pv (73);
        const float spreadPhase = (float) pv (74) / 360.0f * (float) s.voice;
        if (retrig == 0) v.lfo.phase = spreadPhase;
        else if (retrig == 1 && engine.keysHeld <= 1) for (int i = 0; i < PolyNoteEngine::kMaxVoices; ++i) voices[i].lfo.phase = (float) pv (74) / 360.0f * (float) i;
        if (! wasActive) { v.svf.reset(); v.phase = 0; v.posSmoothed = -1; }
    };
    engine.releaseVoice = [this] (int vi) { voices[vi].env1.noteOff(); voices[vi].env2.noteOff(); };
    engine.killVoice = [this] (int vi) { voices[vi].killing = true; };
    engine.voiceSounding = [this] (int vi) { return voices[vi].active; };
}

void PolyWavetable::prepare (double sampleRate, int block)
{
    preparePoly (sampleRate, block);
    delay.prepare (sampleRate, 10.5f);
    chorus.prepare (sampleRate);
    for (auto& v : voices) { v = Voice(); v.env1.sr = v.env2.sr = sampleRate; }
    if (! table) onSelected();
}

void PolyWavetable::onSelected()
{
    entries = SdCard::tableEntries ("wavetables", "playlist-wavetable.txt");
    loadTable (appliedValue (7));
}

void PolyWavetable::confirm (int n)
{
    if (n == 7) loadTable (appliedValue (7));
}

void PolyWavetable::loadTable (int index)
{
    if (entries.isEmpty()) { std::atomic_store (&table, WaveTable::fallback()); return; }
    auto entry = entries[juce::jlimit (0, entries.size() - 1, index)];
    int wavelength = -1; // auto: 600 (disting) or 256 (WaveEdit banks)
    auto pl = SdCard::root().getChildFile ("wavetables").getChildFile ("playlist-wavetable.txt");
    if (pl.existsAsFile())
    {
        juce::StringPairArray g;
        for (auto& e : readPlaylist (pl, g))
            if (e.file == entry.getFileName() && e.flags.containsKey ("wavelength")) wavelength = e.flags["wavelength"].getIntValue();
        if (g.containsKey ("wavelength")) wavelength = g["wavelength"].getIntValue();
    }
    runAsync ([this, entry, wavelength] {
        auto t = WaveTable::load (entry, wavelength);
        if (! t) t = WaveTable::fallback();
        std::atomic_store (&table, t);
    });
}

float PolyWavetable::wavePosition (const Voice& v, int vi) const
{
    float pos = (float) pv (8) / 100.0f;
    pos += (float) pv (9) / 100.0f * (float) vi / 7.0f;
    pos += (float) pv (28) / 100.0f * v.vel;
    pos += (float) pv (30) / 100.0f * (v.pitch - 48.0f) / 60.0f;
    pos += (float) pv (32) / 100.0f * v.env1.level;
    pos += (float) pv (34) / 100.0f * v.env2.level;
    pos += (float) pv (37) / 100.0f * v.lfo.value();
    return juce::jlimit (0.0f, 1.0f, pos);
}

void PolyWavetable::configure()
{
    updateSustain();
    engine.maxVoices = pv (72);
    engine.unison = pv (62);
    engine.unisonDetuneCents = (float) pv (63);
    engine.transpose = pv (10);
    engine.fineCents = (float) pv (11);
    engine.sustainMode = pv (67);
    engine.triggerMode = pv (76);
    engine.velCurve = pv (68);
    auto& c = engine.chords;
    c.enable = pv (44); c.key = pv (45); c.scale = pv (46); c.shape = pv (47); c.inversion = pv (48);
    for (int i = 0; i < 3; ++i) { c.arpMode[i] = pv (49 + i); c.arpRange[i] = pv (52 + i); }
    c.breakMs = pv (77); c.breakDir = pv (78);
    updateTuning (55, 56);
    engine.tuning = &tuning;
    for (auto& v : voices)
    {
        v.env1.aT = attackSeconds (pv (12), false); v.env1.dT = decaySeconds (pv (13)); v.env1.sustain = pv (14) / 127.0f; v.env1.rT = releaseSeconds (pv (15));
        v.env1.aK = Envelope::shapeToK (pv (16)); v.env1.dK = Envelope::shapeToK (pv (17));
        v.env2.aT = attackSeconds (pv (18), false); v.env2.dT = decaySeconds (pv (19)); v.env2.sustain = pv (20) / 127.0f; v.env2.rT = releaseSeconds (pv (21));
        v.env2.aK = Envelope::shapeToK (pv (22)); v.env2.dK = Envelope::shapeToK (pv (23));
        v.lfo.setHz (lfoHz (pv (40)), sr);
    }
}

void PolyWavetable::renderVoices (float* const* out, int start, int n)
{
    juce::ignoreUnused (start);
    auto t = std::atomic_load (&table);
    if (! t || t->numWaves == 0) return;
    const int ftype = pv (24);
    const float q01 = pv (26) / 100.0f;
    const int outMode = pv (65);
    const float spread01 = pv (64) / 100.0f;
    const float velVol = pv (27) / 100.0f;
    float loudest = 0; int loudestV = -1;
    for (int vi = 0; vi < PolyNoteEngine::kMaxVoices; ++vi)
    {
        auto& v = voices[vi];
        if (! v.active) { voiceLevel[vi] = 0.0f; continue; }
        // control rate (per chunk)
        float e1 = v.env1.advance (n);
        float e2 = v.env2.advance (n);
        float lfo = v.lfo.next (n);
        if (! v.env1.active() || (v.killing && v.fade <= 0.0f))
        {
            v.active = false; voiceLevel[vi] = 0.0f;
            if (! engine.voiceGate[vi]) engine.voiceFinished (vi);
            continue;
        }
        float pitch = v.pitch + bendSemis + 0.1f * ((float) pv (36) * e2 + (float) pv (39) * lfo);
        float hz = noteHz (pitch);
        float inc = hz / (float) sr;
        float maxH = (float) sr * 0.5f / std::max (1.0f, hz);
        int level = juce::jlimit (0, WaveTable::kLevels - 1, (int) std::ceil (std::log2 (1024.0f / std::max (1.0f, maxH))));
        float posTarget = wavePosition (v, vi) * (float) (t->numWaves - 1);
        if (v.posSmoothed < 0) v.posSmoothed = posTarget;
        float pos0 = v.posSmoothed, dpos = (posTarget - pos0) / (float) n;
        v.posSmoothed = posTarget;
        float cut = (float) pv (25) + (float) pv (29) * v.vel + (float) pv (31) / 100.0f * (v.pitch - 48.0f)
                    + (float) pv (33) * e1 + (float) pv (35) * e2 + (float) pv (38) * lfo;
        if (ftype > 0) v.svf.set (noteHz (cut), q01, sr);
        float amp = e1 * (1.0f - velVol * (1.0f - v.vel)) * 0.3f;
        float gl, gr;
        if (outMode < 3) panGains (spreadPan (outMode, vi, v.pitch, spread01), gl, gr);
        else gl = gr = 0.7f;
        const int perVoiceOut = vi % 4;
        float lvl = 0;
        for (int i = 0; i < n; ++i)
        {
            float pos = juce::jlimit (0.0f, (float) (t->numWaves - 1), pos0 + dpos * (float) i);
            int w0 = (int) pos; int w1 = juce::jmin (w0 + 1, t->numWaves - 1); float wf = pos - (float) w0;
            const float* a = t->wave (w0, level);
            const float* b = t->wave (w1, level);
            float fp = v.phase * WaveTable::kLen;
            int i0 = (int) fp; float fr = fp - (float) i0; int i1 = (i0 + 1) & (WaveTable::kLen - 1);
            float sa = a[i0] + fr * (a[i1] - a[i0]);
            float sb = b[i0] + fr * (b[i1] - b[i0]);
            float s = sa + wf * (sb - sa);
            v.phase += inc; if (v.phase >= 1.0f) v.phase -= 1.0f;
            if (ftype > 0) s = v.svf.process (s, ftype);
            if (v.killing) { v.fade -= 1.0f / (0.004f * (float) sr); if (v.fade < 0) v.fade = 0; }
            s *= amp * v.fade;
            lvl = std::max (lvl, std::abs (s));
            if (outMode < 3) { out[0][i] += s * gl; out[1][i] += s * gr; }
            else out[perVoiceOut][i] += s;
        }
        if (v.killing && v.fade <= 0.0f) { v.killing = false; v.fade = 1.0f; v.env1.kill(); v.env2.kill(); }
        voiceLevel[vi] = e1;
        if (e1 > loudest) { loudest = e1; loudestV = vi; displayPos = v.posSmoothed; }
        juce::ignoreUnused (lvl);
    }
    loudestVoice = loudestV;
}

void PolyWavetable::postProcess (float* const* out, int n)
{
    const int outMode = pv (65);
    if (outMode < 3)
    {
        chorus.process (out[0], out[1], n, pv (57));
        delay.process (out[0], out[1], n, pv (58), (float) pv (59), (float) delayMs (pv (60)), pv (61) / 100.0f);
    }
    const float g = dbToGain ((float) pv (41));
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < n; ++i) out[c][i] = softSat (out[c][i] * g);
    if (outMode < 3)
    {
        // output 4: mono mix, output 3: paraphonic gate
        bool gate = false;
        for (int v = 0; v < PolyNoteEngine::kMaxVoices; ++v)
            gate |= paraphonicGateMode == 0 ? voices[v].active : engine.voiceGate[v];
        for (int i = 0; i < n; ++i) { out[3][i] = 0.5f * (out[0][i] + out[1][i]); out[2][i] = gate ? 0.5f : 0.0f; }
    }
}

std::vector<BrowserSpec> PolyWavetable::browsers()
{
    BrowserSpec b;
    b.title = "Wavetables";
    b.items = [this] { juce::StringArray a; for (auto& e : entries) a.add (e.getFileNameWithoutExtension()); if (a.isEmpty()) a.add ("(built-in)"); return a; };
    b.selected = [this] { return pv (7); };
    b.choose = [this] (int i) { set (7, i); applied[7] = i; confirm (7); };
    BrowserSpec w;
    w.title = "Waves";
    w.items = [this] { juce::StringArray a; if (auto t = std::atomic_load (&table)) for (int i = 0; i < t->numWaves; ++i) a.add ("Wave " + juce::String (i + 1)); return a; };
    w.selected = [this] { auto t = std::atomic_load (&table); if (! t || t->numWaves < 2 || pv (8) < 0) return pv (8) < 0 || ! t ? -1 : 0;
                          return juce::roundToInt (pv (8) / 100.0 * (t->numWaves - 1)); };
    w.choose = [this] (int i) { if (auto t = std::atomic_load (&table)) set (8, t->numWaves < 2 ? 0 : juce::roundToInt (i * 100.0 / (t->numWaves - 1))); };
    return { b, w };
}

// ------------------------------------------------------------------------------------------------ import
static bool writeMonoWav (const juce::File& f, const float* data, int n)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    juce::WavAudioFormat fmt;
    auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (44100).withNumChannels (1).withBitsPerSample (16));
    if (! w) return false;
    const float* ch[1] = { data };
    return w->writeFromFloatArrays (ch, 1, n);
}

static juce::String safeName (juce::String s)
{
    s = s.replaceCharacters ("/\\:*?\"<>|", "_________").trim();
    return s.isEmpty() ? juce::String ("imported") : s;
}

// Converts WaveEdit banks (64 x 256 samples) / concatenated tables / folders / single cycles into
// disting-style wavetable folders (one single-cycle WAV per wave) inside SD/wavetables.
juce::Array<juce::File> PolyWavetable::importToCard (const juce::StringArray& paths)
{
    auto dest = SdCard::root().getChildFile ("wavetables");
    dest.createDirectory();
    juce::Array<juce::File> made;
    std::vector<juce::File> singles;
    auto importTable = [&] (const juce::String& name, const std::vector<std::vector<float>>& waves)
    {
        if (waves.empty()) return;
        auto dir = dest.getChildFile (safeName (name));
        if (dir.isDirectory()) for (auto& old : dir.findChildFiles (juce::File::findFiles, false, "*.wav")) old.deleteFile();
        dir.createDirectory();
        for (size_t i = 0; i < waves.size(); ++i)
            writeMonoWav (dir.getChildFile (safeName (name) + "_" + juce::String ((int) i).paddedLeft ('0', 3) + ".wav"), waves[i].data(), (int) waves[i].size());
        made.add (dir);
    };
    for (auto& p : paths)
    {
        juce::File f (p);
        if (f.isDirectory())
        {
            std::vector<std::vector<float>> waves;
            for (auto& c : SdCard::sortedChildren (f, juce::File::findFiles, "*.wav"))
            {
                WavData w;
                if (SdCard::isWav (c) && loadWav (c, w) && w.frames > 1)
                {
                    std::vector<float> v ((size_t) w.frames);
                    for (int64_t i = 0; i < w.frames; ++i) v[(size_t) i] = w.sample (i, 0);
                    waves.push_back (std::move (v));
                }
                if (waves.size() >= 256) break;
            }
            importTable (f.getFileName(), waves);
            continue;
        }
        WavData w;
        if (! f.hasFileExtension ("wav") || ! loadWav (f, w) || w.frames < 2) continue;
        int len = 0;
        if (w.frames % 256 == 0 && w.frames >= 512) len = 256;          // WaveEdit bank (64 x 256)
        else if (w.frames % 600 == 0 && w.frames >= 1200) len = 600;     // disting concatenated table
        if (len == 0) { singles.push_back (f); continue; }               // a single cycle
        std::vector<std::vector<float>> waves;
        for (int64_t s = 0; s + len <= w.frames && waves.size() < 256; s += len)
        {
            std::vector<float> v ((size_t) len);
            for (int i = 0; i < len; ++i) v[(size_t) i] = w.sample (s + i, 0);
            waves.push_back (std::move (v));
        }
        importTable (f.getFileNameWithoutExtension(), waves);
    }
    if (! singles.empty())
    {
        std::sort (singles.begin(), singles.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        std::vector<std::vector<float>> waves;
        for (auto& f : singles)
        {
            WavData w; loadWav (f, w);
            std::vector<float> v ((size_t) w.frames);
            for (int64_t i = 0; i < w.frames; ++i) v[(size_t) i] = w.sample (i, 0);
            waves.push_back (std::move (v));
        }
        importTable (singles.size() == 1 ? singles[0].getFileNameWithoutExtension() : singles[0].getParentDirectory().getFileName() + " waves", waves);
    }
    return made;
}

bool PolyWavetable::importFiles (const juce::StringArray& paths)
{
    auto made = importToCard (paths);
    if (made.isEmpty()) { plugin.message ("Nothing to import"); return false; }
    entries = SdCard::tableEntries ("wavetables", "playlist-wavetable.txt");
    int idx = entries.indexOf (made.getLast());
    if (idx >= 0) { set (7, idx); applied[7] = idx; confirm (7); }
    plugin.message ("Imported " + juce::String (made.size()) + " wavetable" + (made.size() > 1 ? "s" : ""));
    return true;
}

juce::File PolyWavetable::waveEditFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("WaveEdit").getChildFile ("banks");
}

std::vector<ActionSpec> PolyWavetable::actions()
{
    return {
        { "Import", [this] {
              chooser = std::make_unique<juce::FileChooser> ("Import wavetables (WaveEdit banks, single cycles or folders)",
                                                             waveEditFolder().isDirectory() ? waveEditFolder() : juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.wav");
              chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                                    [this] (const juce::FileChooser& fc) {
                                        juce::StringArray p; for (auto& f : fc.getResults()) p.add (f.getFullPathName());
                                        if (! p.isEmpty()) importFiles (p); });
          }, {} },
        { "WaveEdit", [this] {
              auto dir = waveEditFolder();
              juce::StringArray p;
              for (auto& f : SdCard::sortedChildren (dir, juce::File::findFiles, "*.wav")) p.add (f.getFullPathName());
              if (p.isEmpty()) plugin.message ("No banks in Documents/WaveEdit");
              else importFiles (p); }, {} },
        { "Rescan", [this] { onSelected(); plugin.message ("Rescanned wavetables"); }, {} },
        { "Notes off", [this] { plugin.allNotesOffRequest = true; }, {} },
    };
}

bool PolyWavetable::pushR (int cur)
{
    if (cur != 60) return false;
    double now = juce::Time::getMillisecondCounterHiRes();
    double dt = now - lastTap;
    lastTap = now;
    if (dt > 30 && dt < 10000) set (60, delayUsesMidiClock ? pv (60) : (int) dt);
    return true;
}

void PolyWavetable::drawBottomLine (Oled& o, int cur)
{
    juce::ignoreUnused (cur);
    drawVoiceLine (o);
}

void PolyWavetable::drawPage (Oled& o, int cur)
{
    if (cur != 7 && cur != 8 && cur != 69)
    {
        PolyAlgorithm::drawPage (o, cur);
        if (cur == 55 || cur == 56)
        {
            o.fillRect (0, 24, 128, 8, false);
            if (tuning.error.isNotEmpty()) o.at (0, 3, tuning.error);
            else if (cur == 55) o.at (0, 3, tuning.description.substring (0, 21));
        }
        return;
    }
    // wavetable page: wrapped name on the left, the waveform of the loudest voice on the right
    const auto& d = def (cur);
    juce::String nm = d.name;
    juce::String first = nm.upToFirstOccurrenceOf (" ", false, false), rest = nm.fromFirstOccurrenceOf (" ", false, false);
    if (! nm.containsChar (' ') && nm.length() > 5) { first = nm.substring (0, 4); rest = nm.substring (4); }
    o.at (0, 0, juce::String (cur).paddedLeft (' ', 2) + ":" + first);
    o.at (4, 1, rest);
    o.right (5, 2, juce::String (pv (cur)));
    o.right (10, 2, juce::String (appliedValue (cur)));
    if (awaitingConfirm (cur)) o.at (0, 3, "PUSH V");
    auto t = std::atomic_load (&table);
    if (cur == 7)
    {
        int idx = juce::jlimit (0, juce::jmax (0, entries.size() - 1), pv (7));
        if (! awaitingConfirm (cur))
            o.at (0, 3, entries.isEmpty() ? juce::String ("(no wavetables)") : entries[idx].getFileNameWithoutExtension().substring (0, 21));
    }
    else if (t) o.at (0, 3, juce::String (loudestVoice >= 0 ? displayPos : wavePosition (voices[0], 0) * (t->numWaves - 1), 2));
    if (t)
    {
        float pos = loudestVoice >= 0 ? displayPos : wavePosition (voices[0], 0) * (float) (t->numWaves - 1);
        int w0 = juce::jlimit (0, t->numWaves - 1, (int) pos);
        int w1 = juce::jmin (w0 + 1, t->numWaves - 1); float wf = pos - (float) w0;
        const float* a = t->wave (w0, 4); const float* b = t->wave (w1, 4);
        for (int x = 0; x < 64; ++x)
        {
            int i = x * WaveTable::kLen / 64;
            float s = a[i] + wf * (b[i] - a[i]);
            int y = juce::jlimit (0, 23, 11 - (int) std::round (s * 10.5f));
            if ((x & 1) == 0 || true) o.set (64 + x, y);
        }
    }
}
} // namespace ex

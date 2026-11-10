#include "PolyExciter.h"

namespace ex
{
static constexpr int kMaxExciterFrames = 28672;

std::shared_ptr<ExciterTable> ExciterTable::load (const juce::File& entry)
{
    auto t = std::make_shared<ExciterTable>();
    t->name = entry.getFileName();
    if (entry.isDirectory())
    {
        for (auto& f : SdCard::sortedChildren (entry, juce::File::findFiles, "*.wav"))
        {
            if (! SdCard::isWav (f)) continue;
            WavData w;
            if (! loadWav (f, w, kMaxExciterFrames)) continue;
            std::vector<float> e ((size_t) w.frames);
            for (int64_t i = 0; i < w.frames; ++i) e[(size_t) i] = w.sample (i, 0);
            t->exciters.push_back (std::move (e));
            t->names.add (f.getFileNameWithoutExtension());
            t->sampleRate = w.sampleRate;
            if (t->exciters.size() >= 128) break;
        }
    }
    else
    {
        WavData w;
        if (loadWav (entry, w) && w.frames >= 128)
        {
            int64_t len = juce::jmin ((int64_t) kMaxExciterFrames, w.frames / 128);
            for (int k = 0; k < 128; ++k)
            {
                std::vector<float> e ((size_t) len);
                for (int64_t i = 0; i < len; ++i) e[(size_t) i] = w.sample ((w.frames / 128) * k + i, 0);
                t->exciters.push_back (std::move (e));
                t->names.add ("Exciter " + juce::String (k).paddedLeft ('0', 3));
            }
            t->sampleRate = w.sampleRate;
        }
    }
    if (t->exciters.empty()) return nullptr;
    return t;
}

PolyExciter::PolyExciter (ExPlugin& p) : PolyAlgorithm (p, 13, "Poly Exciter")
{
    addAttenuverters();
    add ("Mode", 0, 2, 0, {}, { "Strike", "Scrape", "Strum" });          // 7
    add ("Table", -1, 999, -1);                                          // 8
    defs.back().fmt = [] (int v) { return v < 0 ? juce::String ("Internal") : juce::String(); };
    add ("Exciter", 0, 127, 0);                                          // 9
    defs.back().fmt = [this] (int v) {
        static const char* bi[] = { "Ramp", "Noise burst", "Noise ramp", "Square", "Silence" };
        return pv (8) < 0 ? juce::String (bi[juce::jlimit (0, 4, v)]) : juce::String(); };
    add ("Exciter length", 1, 597, 100, "ms");                           // 10
    add ("Tone", 0, 127, 50);                                            // 11
    add ("Damp", 0, 100, 4);                                             // 12
    add ("Release damp", 0, 100, 0);                                     // 13
    add ("Pressure", 0, 127, 0);                                         // 14
    add ("Veloc -> strike", 0, 100, 100, "%");                           // 15
    add ("Veloc -> tone", -100, 100, 0, "%");                            // 16
    add ("Veloc -> damp", -100, 100, 0, "%");                            // 17
    add ("Veloc -> exciter", -127, 127, 0);                              // 18
    add ("Pitch -> damp", -100, 100, 0, "%");                            // 19
    add ("Pitch -> exciter", -100, 100, 0, "%");                         // 20
    add ("Pitch -> LPF", -100, 100, 0, "%");                             // 21
    add ("Press -> exciter", -127, 127, 0);                              // 22
    add ("Env -> pitch", -120, 120, 0);                                  // 23
    add ("Env -> exciter", -127, 127, 0);                                // 24
    add ("LFO -> pitch", -200, 200, 0, "cents");                         // 25
    add ("LFO -> exciter", -127, 127, 0);                                // 26
    add ("LFO speed", -100, 100, 90);                                    // 27
    add ("Attack time", 0, 127, 60); add ("Decay time", 0, 127, 70);      // 28, 29
    add ("Sustain level", -127, 127, 0); add ("Release time", 0, 127, 50); // 30, 31
    add ("Coarse tune", -60, 60, 0); add ("Fine tune", -100, 100, 0, "cents"); // 32, 33
    add ("Gain", -40, 24, 0, "dB");                                       // 34
    add ("Sustain", 0, 1, 0, {}, { "Off", "On" });                        // 35
    add ("Bend range", 0, 48, 2);                                         // 36
    add ("Chord enable", 0, 7, 0); defs.back().fmt = [] (int v) { return chordEnableName (v); };   // 37
    add ("Chord key", -12, 12, 0); defs.back().fmt = [] (int v) { return keyName (v); };          // 38
    add ("Chord scale", 0, 7, 0); defs.back().fmt = [] (int v) { return chordScaleName (v); };    // 39
    add ("Chord shape", 0, 13, 0); defs.back().fmt = [] (int v) { return chordShapeName (v); };   // 40
    add ("Chord inversion", 0, 3, 0);                                                             // 41
    for (int i = 1; i <= 3; ++i) { add ("Arpeggio " + juce::String (i) + " mode", 0, 14, 0); defs.back().fmt = [] (int v) { return arpModeName (v); }; } // 42-44
    for (int i = 1; i <= 3; ++i) add ("Arpeggio " + juce::String (i) + " range", 1, 3, 1);      // 45-47
    add ("Scala/MTS", -16, 99, 0); defs.back().fmt = [this] (int v) { return scalaName (v); };    // 48
    add ("Scala KBM", 0, 99, 0); defs.back().fmt = [this] (int v) { return kbmName (v); };        // 49
    add ("Low pass filter", 0, 127, 126);                                 // 50
    add ("Chorus mode", 0, 2, 0, {}, { "Off", "I", "II" });                // 51
    add ("Delay mode", 0, 2, 0, {}, { "Off", "Stereo", "Ping-pong" });     // 52
    add ("Delay level", -40, 0, -3, "dB");                                // 53
    add ("Delay time", 1, 10000, 500, "ms");                              // 54
    defs.back().fmt = [this] (int v) { return delayUsesMidiClock ? "MIDI " + delayDivisionName (v) : juce::String ("ms"); };
    add ("Delay feedback", 0, 100, 50, "%");                              // 55
    add ("Unison", 1, 8, 1);                                              // 56
    add ("Unison detune", 0, 100, 10, "cents");                           // 57
    add ("Output spread", -100, 100, 0, "%");                             // 58
    add ("Break time", 0, 1000, 0, "ms");                                 // 59
    add ("Break direction", 0, 2, 0, {}, { "Up", "Down", "Alternate" });  // 60
    add ("Output mode", 0, 3, 0, {}, { "Spread voice", "Spread voice 2", "Spread pitch", "Per voice" }); // 61
    add ("Input mode", 0, 18, 1);                                         // 62
    add ("Sustain mode", 0, 1, 0, {}, { "Synth", "Piano" });              // 63
    add ("MIDI vel curve", 0, 3, 0, {}, { "Linear", "Soft", "Hard", "Fixed" }); // 64
    add ("Ext exciter input", 0, 6, 0);                                   // 65
    defs.back().fmt = [] (int v) { return v == 0 ? juce::String ("None") : v == 1 ? juce::String ("In L") : v == 2 ? juce::String ("In R") : juce::String ("In L+R"); };
    add ("Pitch bend input", 0, 6, 0);                                    // 66
    add ("Arp reset input", 0, 6, 0);                                     // 67
    add ("Max voices", 1, 16, 16);                                          // 68
    add ("LFO retrigger", 0, 1, 0, {}, { "Poly", "Off" });                 // 69
    add ("LFO spread", 0, 90, 0, "deg");                                  // 70
    add ("Gate offset", 0, 496, 0);                                       // 71

    sections = {
        { "EXCITER", { 7, 9, 10, 14, 65 } },
        { "RESONATOR", { 11, 12, 13, 50, 21, 34 } },
        { "VELO", { 15, 16, 17, 18, 19, 20, 22 } },
        { "ENV/LFO", { 28, 29, 30, 31, 23, 24, 27, 25, 26, 69, 70 } },
        { "CHORD", { 37, 38, 39, 40, 41, 44, 47, 59, 60 } },
        { "FX", { 51, 52, 54, 55, 53, 56, 57, 58, 61 } },
        { "KEYS", { 32, 33, 36, 35, 63, 64, 68, 48, 49 } },
    };
    engine.startVoice = [this] (const PolyNoteEngine::VoiceStart& s)
    {
        auto& v = voices[s.voice];
        v.active = true; v.gate = true; v.killing = false; v.fade = 1;
        v.note = s.note; v.pitch = s.pitch; v.vel = s.velocity; v.timbre = s.timbre;
        v.exPos = 0; v.exPlaying = true; v.quiet = 0;
        v.seed = (uint32_t) (s.voice * 7919 + s.note * 31 + 1);
        v.env.noteOn (false);
        if (pv (69) == 0) v.lfo.phase = (float) pv (70) / 360.0f * (float) s.voice;
    };
    engine.releaseVoice = [this] (int vi) { auto& v = voices[vi]; v.gate = false; v.env.noteOff(); if (pv (7) == 1 || pv (7) == 2) v.exPlaying = false; };
    engine.killVoice = [this] (int vi) { voices[vi].killing = true; };
    engine.voiceSounding = [this] (int vi) { return voices[vi].active; };
}

void PolyExciter::prepare (double sampleRate, int block)
{
    preparePoly (sampleRate, block);
    for (auto& v : voices)
    {
        v = Voice();
        v.line.assign (kDelay, 0.0f);
        v.env.sr = sampleRate;
    }
    delay.prepare (sampleRate, 10.5f);
    chorus.prepare (sampleRate);
    if (entries.isEmpty()) onSelected();
}

void PolyExciter::onSelected()
{
    entries = SdCard::tableEntries ("exciters", "");
    loadedTable = -999;
}

void PolyExciter::configure()
{
    updateSustain();
    engine.maxVoices = pv (68);
    engine.unison = pv (56);
    engine.unisonDetuneCents = (float) pv (57);
    engine.transpose = pv (32);
    engine.fineCents = (float) pv (33);
    engine.sustainMode = pv (63);
    engine.triggerMode = 0;
    engine.velCurve = pv (64);
    auto& c = engine.chords;
    c.enable = pv (37); c.key = pv (38); c.scale = pv (39); c.shape = pv (40); c.inversion = pv (41);
    for (int i = 0; i < 3; ++i) { c.arpMode[i] = pv (42 + i); c.arpRange[i] = pv (45 + i); }
    c.breakMs = pv (59); c.breakDir = pv (60);
    updateTuning (48, 49);
    engine.tuning = &tuning;
    for (auto& v : voices)
    {
        v.env.aT = attackSeconds (pv (28), false); v.env.dT = decaySeconds (pv (29));
        v.env.sustain = pv (30) / 127.0f; v.env.rT = releaseSeconds (pv (31));
        v.lfo.setHz (lfoHz (pv (27)), sr);
    }
    // load the exciter table when the Table parameter changes
    int tIdx = pv (8);
    if (tIdx != loadedTable)
    {
        loadedTable = tIdx;
        if (tIdx < 0 || entries.isEmpty()) std::atomic_store (&table, std::shared_ptr<ExciterTable>());
        else
        {
            auto entry = entries[juce::jlimit (0, entries.size() - 1, tIdx)];
            runAsync ([this, entry] { std::atomic_store (&table, ExciterTable::load (entry)); });
        }
    }
}

float PolyExciter::pressureFor (const Voice& v) const
{
    float p = pv (7) == 2 ? 0.0f : 1.0f;
    p += pv (14) / 127.0f + channelPressure + polyPressure[juce::jlimit (0, 127, v.note)];
    return juce::jlimit (0.0f, 1.0f, p);
}

int PolyExciter::exciterIndexFor (const Voice& v, float env, float lfo, float pressure, int count) const
{
    float idx = (float) pv (9) + (float) pv (18) * v.vel + (float) pv (20) / 100.0f * (v.pitch - 48.0f) * 2.0f
              + (float) pv (22) * pressure + (float) pv (24) * env + (float) pv (26) * lfo;
    return juce::jlimit (0, juce::jmax (0, count - 1), (int) std::round (idx));
}

float PolyExciter::exciterSample (const Voice& v, const ExciterTable* t, int idx, double pos, float lenSamples, uint32_t& seed) const
{
    juce::ignoreUnused (v);
    if (t == nullptr)
    {
        float ph = (float) pos / std::max (1.0f, lenSamples);
        seed = seed * 1664525u + 1013904223u;
        float noise = (float) (int32_t) seed / 2147483648.0f;
        switch (idx)
        {
            case 0: return 1.0f - ph;
            case 1: return noise;
            case 2: return noise * (1.0f - ph);
            case 3: return 1.0f;
            default: return 0.0f;
        }
    }
    const auto& e = t->exciters[(size_t) idx];
    int i = (int) pos;
    if (i < 0 || i + 1 >= (int) e.size()) return 0.0f;
    float fr = (float) (pos - i);
    return e[(size_t) i] + fr * (e[(size_t) i + 1] - e[(size_t) i]);
}

void PolyExciter::renderVoices (float* const* out, int start, int n)
{
    auto t = std::atomic_load (&table);
    const ExciterTable* tp = pv (8) < 0 ? nullptr : t.get();
    const int count = tp ? (int) tp->exciters.size() : 5;
    const int mode = pv (7);
    const float lenMs = (float) pv (10);
    const double exRate = tp ? tp->sampleRate : sr;
    const float lenSamples = (float) (lenMs * 0.001 * exRate);
    const double exStep = exRate / sr;
    const int outMode = pv (61);
    const float spread = pv (58) / 100.0f;
    const int extIn = pv (65);
    for (int vi = 0; vi < PolyNoteEngine::kMaxVoices; ++vi)
    {
        auto& v = voices[vi];
        if (! v.active) { voiceLevel[vi] = 0.0f; continue; }
        float env = v.env.advance (n);
        float lfo = v.lfo.next (n);
        float pressure = pressureFor (v);
        int exIdx = exciterIndexFor (v, env, lfo, pressure, count);
        v.exIndex = exIdx;
        if (v.gate) lastExciter = exIdx;
        float pitch = v.pitch + bendSemis + 0.1f * (float) pv (23) * env + 0.01f * (float) pv (25) * lfo;
        float f0 = noteHz (pitch);
        float period = (float) sr / f0;
        // loop filter (tone) and damping
        float tone = juce::jlimit (0.0f, 1.0f, pv (11) / 127.0f + pv (16) / 100.0f * v.vel);
        float coef = 0.04f + 0.96f * tone * tone;                // one-pole lowpass amount
        float lpDelay = (1.0f - coef) / coef;
        float damp = juce::jlimit (0.0f, 1.0f, pv (12) / 100.0f + pv (17) / 100.0f * v.vel + pv (19) / 100.0f * (v.pitch - 48.0f) / 48.0f
                                                + (v.gate ? 0.0f : pv (13) / 100.0f));
        float t60 = 30.0f * std::pow (0.002f, damp);
        float fb = std::pow (10.0f, -3.0f / (t60 * f0));
        float delaySamples = juce::jlimit (2.0f, (float) kDelay - 4, period - lpDelay - 0.5f);
        float lpfNote = (float) pv (50) + pv (21) / 100.0f * (v.pitch - 48.0f);
        float lpfA = std::exp (-2.0f * kPi * juce::jlimit (20.0f, (float) sr * 0.45f, noteHz (lpfNote)) / (float) sr);
        float strike = (1.0f - pv (15) / 100.0f * (1.0f - v.vel)) * pressure;
        float gl, gr;
        if (outMode < 3) panGains (spreadPan (outMode, vi, v.pitch, spread), gl, gr);
        else gl = gr = 0.7f;
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            float ex = 0;
            if (v.exPlaying)
            {
                ex = exciterSample (v, tp, exIdx, v.exPos, lenSamples, v.seed) * strike;
                v.exPos += exStep;
                if (v.exPos >= lenSamples) { if (mode == 2 && v.gate) v.exPos = 0; else v.exPlaying = false; }
            }
            if (extIn > 0 && inBuffer != nullptr && v.gate)
            {
                int ii = start + i;
                float a = inBuffer->getSample (0, ii), b = inBuffer->getSample (1, ii);
                ex += (extIn == 1 ? a : extIn == 2 ? b : 0.5f * (a + b)) * v.vel;
            }
            // read the delay line with linear interpolation
            float rp = (float) v.w - delaySamples;
            if (rp < 0) rp += kDelay;
            int i0 = (int) rp; float fr = rp - (float) i0; int i1 = (i0 + 1) & (kDelay - 1);
            float d = v.line[(size_t) i0] + fr * (v.line[(size_t) i1] - v.line[(size_t) i0]);
            v.lp += coef * (d - v.lp);
            float y = ex * 0.5f + fb * v.lp;
            v.line[(size_t) v.w] = y;
            v.w = (v.w + 1) & (kDelay - 1);
            // DC block + output low pass
            float dc = y - v.dcX + 0.995f * v.dcY; v.dcX = y; v.dcY = dc;
            v.outLp = dc + lpfA * (v.outLp - dc);
            float s = v.outLp * 0.6f;
            if (v.killing) { v.fade -= 1.0f / (0.004f * (float) sr); if (v.fade <= 0) { v.fade = 0; } s *= v.fade; }
            pk = std::max (pk, std::abs (s));
            if (outMode < 3) { out[0][i] += s * gl; out[1][i] += s * gr; }
            else out[vi % 4][i] += s;
        }
        v.energy = std::max (pk, v.energy * 0.9f);
        voiceLevel[vi] = juce::jlimit (0.0f, 1.0f, v.energy * 3.0f);
        if (v.killing && v.fade <= 0)
        {
            std::fill (v.line.begin(), v.line.end(), 0.0f); v.lp = v.dcX = v.dcY = v.outLp = 0;
            v.killing = false; v.fade = 1; v.active = false;
            if (! engine.voiceGate[vi]) engine.voiceFinished (vi);
            continue;
        }
        if (! v.gate && ! v.exPlaying)
        {
            v.quiet = pk < 2.0e-5f ? v.quiet + n : 0;
            if (v.quiet > (int) (0.05 * sr))
            {
                v.active = false; voiceLevel[vi] = 0.0f;
                std::fill (v.line.begin(), v.line.end(), 0.0f); v.lp = v.dcX = v.dcY = v.outLp = 0;
                engine.voiceFinished (vi);
            }
        }
    }
}

void PolyExciter::postProcess (float* const* out, int n)
{
    if (pv (61) < 3)
    {
        chorus.process (out[0], out[1], n, pv (51));
        delay.process (out[0], out[1], n, pv (52), (float) pv (53), (float) delayMs (pv (54)), pv (55) / 100.0f);
    }
    const float g = dbToGain ((float) pv (34));
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < n; ++i) out[c][i] = softSat (out[c][i] * g);
    if (pv (61) < 3)
        for (int i = 0; i < n; ++i) out[3][i] = 0.5f * (out[0][i] + out[1][i]);
}

std::vector<BrowserSpec> PolyExciter::browsers()
{
    BrowserSpec b;
    b.title = "Exciter tables";
    b.items = [this] { juce::StringArray a { "Internal" }; for (auto& e : entries) a.add (e.getFileNameWithoutExtension()); return a; };
    b.selected = [this] { return pv (8) + 1; };
    b.choose = [this] (int i) { set (8, i - 1); };
    BrowserSpec e;
    e.title = "Exciters";
    e.items = [this] {
        if (pv (8) < 0) return juce::StringArray { "Ramp", "Noise burst", "Noise ramp", "Square", "Silence" };
        auto t = std::atomic_load (&table);
        return t ? t->names : juce::StringArray(); };
    e.selected = [this] { return pv (9); };
    e.choose = [this] (int i) { set (9, i); };
    return { b, e };
}

bool PolyExciter::pushR (int cur)
{
    if (cur != 54) return false;
    double now = juce::Time::getMillisecondCounterHiRes(), dt = now - lastTap; lastTap = now;
    if (dt > 30 && dt < 10000 && ! delayUsesMidiClock) set (54, (int) dt);
    return true;
}

void PolyExciter::drawPage (Oled& o, int cur)
{
    PolyAlgorithm::drawPage (o, cur);
    drawVoiceLine (o);
    auto t = std::atomic_load (&table);
    if (cur == 8)
    {
        o.fillRect (0, 24, 128, 8, false);
        juce::String nm = pv (8) < 0 ? juce::String ("Internal") : entries.isEmpty() ? juce::String ("(no exciters)")
                                                                  : entries[juce::jlimit (0, entries.size() - 1, pv (8))].getFileName();
        o.at (0, 3, nm.substring (0, 21));
    }
    else if (cur == 9 || cur == 18 || cur == 20 || cur == 22 || cur == 24 || cur == 26)
    {
        o.fillRect (0, 24, 128, 8, false);
        juce::String nm = pv (8) < 0 || ! t ? juce::String ("Internal") : t->name;
        o.at (0, 3, nm.substring (0, 17));
        o.right (21, 3, juce::String (lastExciter).paddedLeft ('0', 3));
    }
}
} // namespace ex

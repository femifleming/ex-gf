#include "MultiFX.h"

namespace ex
{
namespace
{
    // scales for the VSR quantizer (semitones within an octave)
    struct Scale { const char* name; std::vector<int> notes; };
    const std::vector<Scale>& scales()
    {
        static const std::vector<Scale> s = {
            { "Chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
            { "Major", { 0, 2, 4, 5, 7, 9, 11 } },
            { "Minor", { 0, 2, 3, 5, 7, 8, 10 } },
            { "Harmonic minor", { 0, 2, 3, 5, 7, 8, 11 } },
            { "Melodic minor", { 0, 2, 3, 5, 7, 9, 11 } },
            { "Major pentatonic", { 0, 2, 4, 7, 9 } },
            { "Minor pentatonic", { 0, 3, 5, 7, 10 } },
            { "Blues", { 0, 3, 5, 6, 7, 10 } },
            { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } },
            { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
            { "Lydian", { 0, 2, 4, 6, 7, 9, 11 } },
            { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } },
            { "Locrian", { 0, 1, 3, 5, 6, 8, 10 } },
            { "Whole tone", { 0, 2, 4, 6, 8, 10 } },
            { "Fifths", { 0, 7 } },
            { "Octaves", { 0 } },
        };
        return s;
    }
    int quantizeSemis (int semis, int scale) // scale 1..16
    {
        const auto& notes = scales()[(size_t) juce::jlimit (0, 15, scale - 1)].notes;
        int oct = (int) std::floor ((double) semis / 12.0), within = semis - oct * 12, best = 0;
        for (int d : notes) if (d <= within) best = d;
        return oct * 12 + best;
    }

    struct Program { const char* name; std::vector<std::pair<int, int>> values; };
    const std::vector<Program>& programs()
    {
        static const std::vector<Program> p = {
            { "Init", {} },
            { "Stereo Chorus", { { 11, -40 }, { 14, -40 }, { 15, -40 }, { 30, -50 } } },
            { "Flanger", { { 11, -40 }, { 14, -40 }, { 15, -40 }, { 30, -50 }, { 31, 70 }, { 32, 25 }, { 33, 15 }, { 34, 80 } } },
            { "Phaser", { { 11, -40 }, { 13, -6 }, { 14, -40 }, { 15, -40 }, { 29, 1 }, { 31, 60 }, { 32, 450 }, { 33, 25 }, { 34, 60 } } },
            { "Shimmer", { { 13, -40 }, { 14, -40 }, { 26, 4 }, { 29, 2 }, { 30, -100 }, { 36, 120 }, { 37, 12 }, { 49, 2 }, { 50, -80 }, { 52, 6000 }, { 54, 50 } } },
            { "Tape Echo", { { 13, -40 }, { 15, -40 }, { 17, 1 }, { 18, 45 }, { 23, 1 }, { 24, 96 }, { 42, 3 }, { 43, 380 }, { 45, 62 } } },
            { "Ping-Pong", { { 13, -40 }, { 15, -40 }, { 42, 2 }, { 43, 375 }, { 45, 55 } } },
            { "Echo Verb", { { 13, -40 }, { 14, -6 }, { 43, 450 }, { 45, 40 }, { 49, 3 }, { 50, 0 }, { 52, 2500 } } },
            { "Big Hall", { { 13, -40 }, { 14, -40 }, { 15, -3 }, { 51, 3 }, { 52, 9000 }, { 54, 45 } } },
            { "Plate", { { 13, -40 }, { 14, -40 }, { 51, 2 }, { 52, 1800 }, { 54, 35 } } },
            { "Lo-Fi 12k", { { 7, -24 }, { 13, -40 }, { 14, -12 }, { 15, -9 } } },
            { "Octave Down Space", { { 13, -6 }, { 14, -40 }, { 29, 2 }, { 30, -100 }, { 37, -12 }, { 48, 2 }, { 52, 5000 } } },
            { "Detune Wide", { { 14, -40 }, { 15, -40 }, { 29, 2 }, { 30, -100 }, { 36, 40 }, { 38, 9 } } },
        };
        return p;
    }

    float gainFromDb (int db) { return db <= -40 ? 0.0f : dbToGain ((float) db); }
    // keeps feedback routings finite (NaN becomes -8)
    float bound (float x) { return x > -8.0f ? (x < 8.0f ? x : 8.0f) : -8.0f; }
}

juce::StringArray MultiFX::programNames()
{
    juce::StringArray a;
    for (auto& p : programs()) a.add (p.name);
    return a;
}

MultiFX::MultiFX (ExPlugin& p) : FxAlgorithm (p, 12, "Multi FX")
{
    const juce::StringArray sources { "Pre-EQ", "Post-EQ", "Pitch", "Delay", "Reverb" };
    const juce::StringArray offOn { "Off", "On" };
    auto inputName = [] (int v) { return v == 0 ? juce::String ("None") : "Input " + juce::String (v); };
    auto clockFmt = [this] (int v) { return delayUsesMidiClock ? "MIDI " + delayDivisionName (v) : juce::String ("ms"); };

    addAttenuverters();
    add ("VSR", -80, 80, 0, "ST");                                          // 7
    add ("VSR attenuator", 0, 100, 100, "%");                               // 8
    add ("Quantize", 0, 16, 0);                                             // 9
    defs.back().fmt = [] (int v) { return v == 0 ? juce::String ("Off") : juce::String (scales()[(size_t) juce::jlimit (0, 15, v - 1)].name); };
    add ("Dry routing", 0, 1, 0, {}, { "Pre-EQ", "Post-EQ" });              // 10
    add ("Dry gain", -40, 6, 0, "dB");                                      // 11
    add ("Effects gain", -40, 6, 0, "dB");                                  // 12
    add ("Pitch gain", -40, 6, 0, "dB");                                    // 13
    add ("Delay gain", -40, 6, 0, "dB");                                    // 14
    add ("Reverb gain", -40, 6, 0, "dB");                                   // 15
    add ("Saturation", 0, 1, 1, {}, offOn);                                 // 16
    add ("EQ 1 type", 0, 2, 2, {}, { "HP 6dB", "HP 12dB", "Low shelf" });   // 17
    add ("EQ 1 frequency", 0, 127, 26);                                     // 18
    add ("EQ 1 gain", -120, 120, 0, "0.1dB");                               // 19
    add ("EQ 2 frequency", 0, 127, 75);                                     // 20
    add ("EQ 2 Q", 0, 127, 64);                                             // 21
    add ("EQ 2 gain", -120, 120, 0, "0.1dB");                               // 22
    add ("EQ 3 type", 0, 2, 2, {}, { "LP 6dB", "LP 12dB", "High shelf" });  // 23
    add ("EQ 3 frequency", 0, 127, 118);                                    // 24
    add ("EQ 3 gain", -120, 120, 0, "0.1dB");                               // 25
    add ("Pitch input 1", 0, 4, 1, {}, sources);                            // 26
    add ("Pitch input 2", 0, 4, 3, {}, sources);                            // 27
    add ("Pitch input mix", -100, 100, -100, "%");                          // 28
    add ("Pitch effect", 0, 2, 0, {}, { "Chorus", "Phaser", "Pitch shift" }); // 29
    add ("Pitch blend", -100, 100, 100, "%");                               // 30
    add ("Pitch feedback", -100, 100, 0, "%");                              // 31
    add ("Chorus delay", 1, 853, 580, "0.1ms");                             // 32 (or phaser centre)
    add ("Chorus speed", 1, 200, 40, "0.01Hz");                             // 33
    add ("Chorus depth", 0, 100, 15);                                       // 34
    add ("Chorus spread", 0, 360, 180, "deg");                              // 35
    add ("Pitch shift delay", 1, 200, 200, "ms");                           // 36
    add ("Pitch shift", -48, 48, 0, "ST");                                  // 37
    add ("Pitch shift (fine)", -100, 100, 0, "cents");                      // 38
    add ("Delay input 1", 0, 4, 1, {}, sources);                            // 39
    add ("Delay input 2", 0, 4, 2, {}, sources);                            // 40
    add ("Delay input mix", -100, 100, -100, "%");                          // 41
    add ("Delay mode", 0, 5, 0, {}, { "Mono fade", "Stereo fade", "Ping-pong fade", "Mono slew", "Stereo slew", "Ping-pong slew" }); // 42
    add ("Delay time", 1, 5000, 500, "ms"); defs.back().fmt = clockFmt;     // 43
    add ("Delay time R", 1, 5000, 500, "ms"); defs.back().fmt = clockFmt;   // 44
    add ("Delay feedback", 0, 100, 50, "%");                                // 45
    add ("Delay mono-ize", 0, 100, 100, "%");                               // 46
    add ("Delay initial pan", -100, 100, -100);                             // 47
    add ("Reverb input 1", 0, 4, 1, {}, sources);                           // 48
    add ("Reverb input 2", 0, 4, 3, {}, sources);                           // 49
    add ("Reverb input mix", -100, 100, -100, "%");                         // 50
    add ("Reverb model", 0, 3, 1);                                          // 51
    defs.back().fmt = [] (int v) { return juce::String (ExReverb::modelName (v)); };
    add ("Reverb time", 400, 30000, 1000, "ms");                            // 52
    add ("Reverb size", 1, 100, 100, "%");                                  // 53
    add ("Reverb high damp", 0, 100, 60);                                   // 54
    add ("Reverb mod speed", 1, 500, 250, "0.01Hz");                        // 55
    add ("Reverb mod depth", 0, 100, 25);                                   // 56
    add ("Reverb early gain", -40, 6, -12, "dB");                           // 57
    add ("Reverb diff gain", -40, 6, -6, "dB");                             // 58
    add ("Reverb low pass", 0, 127, 100);                                   // 59
    add ("Mono input", 0, 1, 0, {}, offOn);                                 // 60
    add ("VSR input", 0, 6, 3); defs.back().fmt = inputName;                // 61
    add ("Pitch shift input", 0, 6, 4); defs.back().fmt = inputName;        // 62
    add ("Aux outputs", 0, 4, 1, {}, { "None", "Pitch", "Delay", "Reverb", "All effects" }); // 63
    add ("Aux input L", 0, 6, 0); defs.back().fmt = inputName;              // 64
    add ("Aux input R", 0, 6, 0); defs.back().fmt = inputName;              // 65
    add ("Aux input gain", -40, 6, 0, "dB");                                // 66
    add ("Delay clock input", 0, 6, 0); defs.back().fmt = inputName;        // 67
    add ("Delay R clock input", 0, 6, 0); defs.back().fmt = inputName;      // 68
    add ("Delay multiplier", 0, 23, 15);                                    // 69
    add ("Delay R multiplier", 0, 23, 15);                                  // 70

    sections = {
        { "MIX", { 11, 12, 13, 14, 15, 10, 16, 60, 63 } },
        { "EQ", { 17, 18, 19, 20, 21, 22, 23, 24, 25 } },
        { "PITCH", { 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 26, 27, 28 } },
        { "DELAY", { 42, 43, 44, 45, 46, 47, 39, 40, 41 } },
        { "REVERB", { 51, 52, 53, 54, 55, 56, 57, 58, 59, 48, 49, 50 } },
        { "VSR", { 7, 9 } },
    };
    defaultL = 52; // the module maps Reverb time to a CV input by default
}

void MultiFX::prepare (double sampleRate, int block)
{
    prepareFx (sampleRate, block);
    for (auto& l : pitchLine) l.init ((int) (sr * 0.45));
    for (auto& l : delayLine) l.init ((int) (sr * 10.0) + 8);
    reverb.prepare (sr);
    flush();
    for (auto& ch : eq) for (auto& b : ch) b = Biquad();
    rateSm = 1; phase = 0; accL = accR = 0; accN = 0; chorusSm = -1;
    for (auto& g : gSm) g = 1;
}

void MultiFX::flush()
{
    for (auto& l : pitchLine) l.clear();
    for (auto& l : delayLine) l.clear();
    reverb.clear();
    for (auto& s : src) s[0] = s[1] = 0;
    for (auto& ch : apZ) for (auto& z : ch) z = 0;
    phaserFb[0] = phaserFb[1] = 0; revLp[0] = revLp[1] = 0;
    for (int i = 0; i < 4; ++i) prevOut[i] = curOut[i] = 0;
    for (auto& t : tap) t = DelayTap();
}

// ---------------------------------------------------------------------------------------- EQ coefficients
void MultiFX::setFirstOrder (Biquad& b, bool high, float hz, double sr)
{
    float k = std::tan (kPi * juce::jlimit (10.0f, (float) sr * 0.45f, hz) / (float) sr);
    b.a1 = (k - 1.0f) / (k + 1.0f); b.a2 = 0; b.b2 = 0;
    if (high) { b.b0 = 1.0f / (1.0f + k); b.b1 = -b.b0; }
    else { b.b0 = k / (1.0f + k); b.b1 = b.b0; }
}

void MultiFX::setSecondOrder (Biquad& b, bool high, float hz, double sr)
{
    float w0 = 2 * kPi * juce::jlimit (10.0f, (float) sr * 0.45f, hz) / (float) sr;
    float cs = std::cos (w0), alpha = std::sin (w0) / (2.0f * 0.70710678f), a0 = 1 + alpha;
    if (high) { b.b0 = (1 + cs) / 2 / a0; b.b1 = -(1 + cs) / a0; b.b2 = b.b0; }
    else { b.b0 = (1 - cs) / 2 / a0; b.b1 = (1 - cs) / a0; b.b2 = b.b0; }
    b.a1 = -2 * cs / a0; b.a2 = (1 - alpha) / a0;
}

void MultiFX::setPeak (Biquad& b, float hz, float q, float db, double sr)
{
    float A = std::pow (10.0f, db / 40.0f), w0 = 2 * kPi * juce::jlimit (10.0f, (float) sr * 0.45f, hz) / (float) sr;
    float cs = std::cos (w0), alpha = std::sin (w0) / (2.0f * q), a0 = 1 + alpha / A;
    b.b0 = (1 + alpha * A) / a0; b.b1 = -2 * cs / a0; b.b2 = (1 - alpha * A) / a0;
    b.a1 = b.b1; b.a2 = (1 - alpha / A) / a0;
}

void MultiFX::configure()
{
    // variable sample rate (downwards only; positive values need a negative CV on the module)
    int semis = juce::jmin (0, pv (7));
    if (pv (9) > 0) semis = quantizeSemis (semis, pv (9));
    c.rate = std::pow (2.0f, (float) semis / 12.0f);

    c.mono = pv (60) != 0; c.dryPost = pv (10) != 0; c.sat = pv (16) != 0;
    c.gDry = gainFromDb (pv (11)); c.gFx = gainFromDb (pv (12));
    c.gPitch = gainFromDb (pv (13)); c.gDelay = gainFromDb (pv (14)); c.gReverb = gainFromDb (pv (15));
    c.aux = pv (63);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float f1 = noteHz ((float) pv (18)), f3 = noteHz ((float) pv (24));
        if (pv (17) == 0) setFirstOrder (eq[ch][0], true, f1, sr);
        else if (pv (17) == 1) setSecondOrder (eq[ch][0], true, f1, sr);
        else eq[ch][0].shelf (false, juce::jlimit (10.0f, (float) sr * 0.45f, f1), (float) pv (19) * 0.1f, sr);
        setPeak (eq[ch][1], noteHz ((float) pv (20)), 0.3f * std::pow (40.0f, (float) pv (21) / 127.0f), (float) pv (22) * 0.1f, sr);
        if (pv (23) == 0) setFirstOrder (eq[ch][2], false, f3, sr);
        else if (pv (23) == 1) setSecondOrder (eq[ch][2], false, f3, sr);
        else eq[ch][2].shelf (true, juce::jlimit (10.0f, (float) sr * 0.45f, f3), (float) pv (25) * 0.1f, sr);
    }

    c.pIn1 = pv (26); c.pIn2 = pv (27); c.pMix = (float) pv (28) / 100.0f;
    c.pitchFx = pv (29);
    c.blend = (float) pv (30) / 100.0f;
    c.pitchFb = (float) pv (31) / 100.0f * 0.93f;
    c.chorusDelay = (float) pv (32) * 0.0001f * (float) sr;
    c.phaserHz = 40.0f * std::pow (200.0f, (float) pv (32) / 853.0f);
    c.lfoInc = (float) pv (33) * 0.01f / (float) sr;
    c.depth = (float) pv (34) / 100.0f;
    c.spread = (float) pv (35) / 360.0f;
    c.shiftWin = juce::jmax (16.0f, (float) pv (36) * 0.001f * (float) sr);
    const float ratio = std::pow (2.0f, ((float) pv (37) + (float) pv (38) * 0.01f) / 12.0f);
    c.shiftInc = (1.0f - ratio) / c.shiftWin;

    c.dIn1 = pv (39); c.dIn2 = pv (40); c.dMix = (float) pv (41) / 100.0f;
    c.delayMode = pv (42);
    const float maxDelay = (float) (delayLine[0].size() - 8);
    c.delayL = juce::jlimit (1.0f, maxDelay, (float) (delayMs (pv (43)) * 0.001 * sr));
    c.delayR = (c.delayMode % 3) == 1 ? juce::jlimit (1.0f, maxDelay, (float) (delayMs (pv (44)) * 0.001 * sr)) : c.delayL;
    c.delayFb = (float) pv (45) / 100.0f;
    c.monoize = (float) pv (46) / 100.0f;
    const float a = ((float) pv (47) / 100.0f + 1.0f) * 0.25f * kPi;
    c.panL = std::cos (a); c.panR = std::sin (a);

    c.rIn1 = pv (48); c.rIn2 = pv (49); c.rMix = (float) pv (50) / 100.0f;
    reverb.set (pv (51), (float) pv (52), (float) pv (53), (float) pv (54), (float) pv (55), (float) pv (56), (float) pv (57), (float) pv (58));
    c.revLpA = std::exp (-2.0f * kPi * juce::jlimit (20.0f, (float) sr * 0.45f, noteHz ((float) pv (59))) / (float) sr);
}

// ---------------------------------------------------------------------------------------- the chain
float MultiFX::readTap (Line& line, DelayTap& t, float target, bool slew)
{
    if (t.cur < 0) { t.cur = target; t.slew = target; }
    if (slew)
    {
        // tape style: the read head glides to the new time
        t.slew += (target - t.slew) * (1.0f / (0.12f * (float) sr));
        t.cur = t.slew; t.fading = false;
        return line.read (t.slew);
    }
    t.slew = t.cur;
    if (! t.fading && std::abs (target - t.cur) > 0.5f) { t.next = target; t.fading = true; t.xf = 0; }
    if (! t.fading) return line.read (t.cur);
    float v = line.read (t.cur) * (1.0f - t.xf) + line.read (t.next) * t.xf;
    t.xf += 1.0f / (0.04f * (float) sr);
    if (t.xf >= 1.0f) { t.cur = t.next; t.fading = false; }
    return v;
}

// one sample of the whole chain at the internal clock: o = main L/R, aux L/R
void MultiFX::tick (float l, float r, float* o)
{
    static const float gainSmooth = 0.002f;
    const float targets[5] = { c.gDry, c.gFx, c.gPitch, c.gDelay, c.gReverb };
    for (int i = 0; i < 5; ++i) gSm[i] += (targets[i] - gSm[i]) * gainSmooth;

    // EQ
    src[0][0] = l; src[0][1] = r;
    for (int ch = 0; ch < 2; ++ch)
    {
        float x = src[0][ch];
        for (auto& b : eq[ch]) x = b.process (x);
        src[1][ch] = x;
    }
    auto mixIn = [this] (int a, int b, float m, int ch) { return 0.5f * ((1.0f - m) * src[a][ch] + (1.0f + m) * src[b][ch]); };

    // ---- pitch effects
    float pIn[2] = { mixIn (c.pIn1, c.pIn2, c.pMix, 0), mixIn (c.pIn1, c.pIn2, c.pMix, 1) };
    float proc[2] = { 0, 0 };
    lfoPhase += c.lfoInc; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
    const float lfo[2] = { std::sin (2.0f * kPi * lfoPhase), std::sin (2.0f * kPi * (lfoPhase + c.spread)) };
    if (c.pitchFx == 0)
    {
        if (chorusSm < 0) chorusSm = c.chorusDelay;
        chorusSm += (c.chorusDelay - chorusSm) * 0.0005f;
        for (int ch = 0; ch < 2; ++ch)
        {
            float d = juce::jmax (1.0f, chorusSm * (1.0f + 0.95f * c.depth * lfo[ch]));
            proc[ch] = pitchLine[ch].read (d);
            pitchLine[ch].write (bound (pIn[ch] + c.pitchFb * proc[ch]));
        }
    }
    else if (c.pitchFx == 1)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float hz = juce::jlimit (20.0f, (float) sr * 0.45f, c.phaserHz * std::pow (2.0f, 2.0f * c.depth * lfo[ch]));
            float t = std::tan (kPi * hz / (float) sr), a = (t - 1.0f) / (t + 1.0f);
            float x = bound (pIn[ch] + c.pitchFb * phaserFb[ch]);
            for (auto& z : apZ[ch]) { float y = a * x + z; z = x - a * y; x = y; }
            phaserFb[ch] = x;
            proc[ch] = x;
            pitchLine[ch].write (pIn[ch]);
        }
    }
    else
    {
        // two taps half a window apart, each faded out while it jumps
        shiftPhase += c.shiftInc;
        shiftPhase -= std::floor (shiftPhase);
        const float ph2 = shiftPhase + 0.5f - std::floor (shiftPhase + 0.5f);
        const float g1 = 0.5f - 0.5f * std::cos (2.0f * kPi * shiftPhase), g2 = 1.0f - g1;
        for (int ch = 0; ch < 2; ++ch)
        {
            proc[ch] = g1 * pitchLine[ch].read (1.0f + shiftPhase * c.shiftWin) + g2 * pitchLine[ch].read (1.0f + ph2 * c.shiftWin);
            pitchLine[ch].write (bound (pIn[ch] + c.pitchFb * proc[ch]));
        }
    }
    float pitchOut[2];
    for (int ch = 0; ch < 2; ++ch)
        pitchOut[ch] = bound (c.blend < 0 ? pIn[ch] * (1.0f + c.blend) - proc[ch] * c.blend : pIn[ch] + c.blend * proc[ch]);
    src[2][0] = pitchOut[0]; src[2][1] = pitchOut[1];

    // ---- delay
    float dIn[2] = { mixIn (c.dIn1, c.dIn2, c.dMix, 0), mixIn (c.dIn1, c.dIn2, c.dMix, 1) };
    const bool slew = c.delayMode >= 3;
    const float dl = readTap (delayLine[0], tap[0], c.delayL, slew), dr = readTap (delayLine[1], tap[1], c.delayR, slew);
    if ((c.delayMode % 3) == 2)
    {
        const float mono = 0.5f * (dIn[0] + dIn[1]);
        delayLine[0].write (bound ((1.0f - c.monoize) * dIn[0] + c.monoize * mono * c.panL + c.delayFb * dr));
        delayLine[1].write (bound ((1.0f - c.monoize) * dIn[1] + c.monoize * mono * c.panR + c.delayFb * dl));
    }
    else
    {
        delayLine[0].write (bound (dIn[0] + c.delayFb * dl));
        delayLine[1].write (bound (dIn[1] + c.delayFb * dr));
    }
    src[3][0] = dl; src[3][1] = dr;

    // ---- reverb
    for (int ch = 0; ch < 2; ++ch)
    {
        float x = mixIn (c.rIn1, c.rIn2, c.rMix, ch);
        revLp[ch] = x + c.revLpA * (revLp[ch] - x);
    }
    float rl, rr;
    reverb.process (revLp[0], revLp[1], rl, rr);
    src[4][0] = bound (rl); src[4][1] = bound (rr);

    // ---- mix
    const int dry = c.dryPost ? 1 : 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float fx = gSm[1] * (gSm[2] * src[2][ch] + gSm[3] * src[3][ch] + gSm[4] * src[4][ch]);
        const float m = gSm[0] * src[dry][ch] + fx;
        o[ch] = c.sat ? softSat (m) : m;
        o[2 + ch] = c.aux == 0 ? 0.0f : c.aux == 4 ? fx : src[c.aux + 1][ch];
    }
}

void MultiFX::render (const float* inL, const float* inR, float* const* out, int n)
{
    if (clearRequest.exchange (false)) flush();
    if (bypass.load())
    {
        std::memcpy (out[0], inL, sizeof (float) * (size_t) n);
        std::memcpy (out[1], inR, sizeof (float) * (size_t) n);
        return;
    }
    float inPk = 0, outPk = 0, pk[3] = { 0, 0, 0 };
    for (int i = 0; i < n; ++i)
    {
        const float l = inL[i], r = c.mono ? l : inR[i];
        inPk = std::max (inPk, std::max (std::abs (l), std::abs (r)));
        rateSm += (c.rate - rateSm) * 0.002f;
        if (c.rate >= 1.0f && rateSm > 0.9999f) rateSm = 1.0f;
        float o[4];
        if (rateSm >= 1.0f)
        {
            // full rate: exactly one chain sample per host sample
            tick (l, r, curOut);
            for (int k = 0; k < 4; ++k) { o[k] = curOut[k]; prevOut[k] = curOut[k]; }
            phase = 0; accL = accR = 0; accN = 0;
        }
        else
        {
            accL += l; accR += r; ++accN;
            phase += rateSm;
            if (phase >= 1.0f)
            {
                phase -= 1.0f;
                for (int k = 0; k < 4; ++k) prevOut[k] = curOut[k];
                tick (accL / (float) accN, accR / (float) accN, curOut);
                accL = accR = 0; accN = 0;
            }
            for (int k = 0; k < 4; ++k) o[k] = prevOut[k] + (curOut[k] - prevOut[k]) * phase;
        }
        for (int k = 0; k < 4; ++k) out[k][i] = o[k];
        outPk = std::max (outPk, std::max (std::abs (o[0]), std::abs (o[1])));
        for (int k = 0; k < 3; ++k) pk[k] = std::max (pk[k], std::max (std::abs (src[2 + k][0]), std::abs (src[2 + k][1])));
    }
    meters[0].push (inPk, n, sr);
    for (int k = 0; k < 3; ++k) meters[1 + k].push (pk[k], n, sr);
    if (outPk > 1.0f) clipSamples = (int) sr;
    else if (clipSamples.load() > 0) clipSamples = juce::jmax (0, clipSamples.load() - n);
}

// ---------------------------------------------------------------------------------------- UI
ExAlgorithm::Meter MultiFX::meter (int index)
{
    static const char* names[] = { "IN", "PIT", "DLY", "REV" };
    index = juce::jlimit (0, 3, index);
    Meter m;
    m.level = juce::jlimit (0.0f, 1.0f, meters[index].value.load());
    m.on = m.level > 0.001f;
    m.label = names[index];
    return m;
}

juce::String MultiFX::blockName (int n)
{
    if (n <= 6) return "Inputs";
    if (n <= 9 || n == 61) return "Variable sample rate";
    if (n <= 16 || n == 60) return "Mix";
    if (n <= 25) return "EQ";
    if (n <= 38 || n == 62) return "Pitch effects";
    if (n <= 47 || n >= 67) return "Delay";
    if (n <= 59) return "Reverb";
    return "Aux";
}

void MultiFX::drawBottomLine (Oled& o, int cur) { o.at (0, 3, blockName (cur)); }

void MultiFX::drawPage (Oled& o, int cur)
{
    FxAlgorithm::drawPage (o, cur);
    if (bypass.load()) { o.fillRect (72, 24, 56, 8, false); o.right (21, 3, "BYPASS"); }
    if (clipSamples.load() > 0) { o.fillRect (90, 0, 38, 8, false); o.right (20, 0, "CLIP"); }
}

void MultiFX::addMenuItems (std::vector<MenuItem>& items)
{
    items.push_back (MenuItem::act ("Clear buffers", [this] { clearRequest = true; plugin.message ("Buffers cleared"); }));
    items.push_back (MenuItem::sub ("Options", {
        MenuItem::val ("Delay uses MIDI clk", 0, 1, [this] { return delayUsesMidiClock ? 1 : 0; }, [this] (int v) { delayUsesMidiClock = v != 0; },
                       [] (int v) { return v ? juce::String ("Host tempo") : juce::String ("Off"); }),
    }));
}

bool MultiFX::pushR (int cur)
{
    if (cur != 43 && cur != 44) return false;
    double now = juce::Time::getMillisecondCounterHiRes(), dt = now - lastTap; lastTap = now;
    if (dt > 30 && dt < 5000 && ! delayUsesMidiClock) set (cur, (int) dt);
    return true;
}

void MultiFX::loadProgram (int index)
{
    const auto& all = programs();
    index = juce::jlimit (0, (int) all.size() - 1, index);
    for (int n = 1; n <= numParams(); ++n) set (n, def (n).def);
    for (auto& v : all[(size_t) index].values) set (v.first, v.second);
    delayUsesMidiClock = false;
    program = index;
    clearRequest = true;
}

std::vector<BrowserSpec> MultiFX::browsers()
{
    BrowserSpec b;
    b.title = "Programs";
    b.items = [] { return programNames(); };
    b.selected = [this] { return program; };
    b.choose = [this] (int i) { loadProgram (i); plugin.message (programNames()[program]); };
    return { b };
}

std::vector<ActionSpec> MultiFX::actions()
{
    return { { "Bypass", [this] { bypass = ! bypass.load(); }, [this] { return bypass.load(); } },
             { "Clear", [this] { clearRequest = true; plugin.message ("Buffers cleared"); }, {} },
             { "Tap", [this] { plugin.selectParam (43); pushR (43); }, {} } };
}
} // namespace ex

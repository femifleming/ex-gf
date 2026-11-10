#pragma once
// Shared DSP building blocks for the disting-EX-style algorithms.
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <climits>
#include <algorithm>
#include <functional>
#include <atomic>
#include <memory>
#include <map>
#include <set>
#include <vector>
#include <array>
#include <random>

namespace ex
{
constexpr float kPi = 3.14159265358979f;

// ---------- parameter value -> physical mappings (as documented in the disting EX manual) ----------
inline float attackSeconds (int v, bool zeroAtZero) // 0..127 : 1ms - 15s (or 0 at '0')
{
    if (zeroAtZero && v <= 0) return 0.0f;
    return 0.001f * std::pow (15000.0f, juce::jlimit (0, 127, v) / 127.0f);
}
inline float decaySeconds (int v) { return 0.02f * std::pow (750.0f, juce::jlimit (0, 127, v) / 127.0f); }   // 20ms - 15s
inline float releaseSeconds (int v) { return 0.01f * std::pow (3000.0f, juce::jlimit (0, 127, v) / 127.0f); } // 10ms - 30s
inline float lfoHz (int v) { return 0.01f * std::pow (1000.0f, (juce::jlimit (-100, 100, v) + 100) / 200.0f); } // 0.01 - 10Hz
inline float noteHz (float n) { return 440.0f * std::pow (2.0f, (n - 69.0f) / 12.0f); }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

inline float softSat (float x) // smooth saturation, unity gain for small signals, asymptote ±1.2
{
    const float a = 1.2f;
    float y = x / a;
    if (y > 3.0f) return a;
    if (y < -3.0f) return -a;
    float y2 = y * y;
    return a * y * (27.0f + y2) / (27.0f + 9.0f * y2); // tanh approximation
}

inline float velocityCurve (int curve, float v01)
{
    switch (curve)
    {
        case 1: return std::sqrt (v01);          // soft
        case 2: return v01 * v01;                // hard
        case 3: return 1.0f;                     // fixed
        default: return v01;                     // linear
    }
}

// ---------- ADSR with shapes ----------
struct Envelope
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float level = 0, start = 0, pos = 0, inc = 0;
    float sustain = 1.0f;
    float aT = 0.01f, dT = 0.1f, rT = 0.1f;
    float aK = 3.0f, dK = 5.0f; // curvature (higher = more exponential)
    bool infiniteRelease = false, oneShot = false;
    double sr = 48000;

    static float curve (float p, float k)
    {
        if (k < 0.05f) return p;
        return (1.0f - std::exp (-k * p)) / (1.0f - std::exp (-k));
    }
    static float shapeToK (int shape) { return 0.02f + (127 - juce::jlimit (0, 127, shape)) / 127.0f * 7.0f; }

    void noteOn (bool retrigFromCurrent = true)
    {
        start = retrigFromCurrent ? level : 0.0f;
        stage = Attack; pos = 0;
        inc = aT <= 0.0f ? 2.0f : (float) (1.0 / (aT * sr));
    }
    void noteOff()
    {
        if (stage == Idle || stage == Release) return;
        start = level; stage = Release; pos = 0;
        inc = infiniteRelease ? 0.0f : (float) (1.0 / (std::max (0.001f, rT) * sr));
    }
    void kill() { stage = Idle; level = 0; }
    bool active() const { return stage != Idle; }

    float next()
    {
        switch (stage)
        {
            case Idle: return 0.0f;
            case Attack:
                pos += inc;
                if (pos >= 1.0f) { level = 1.0f; stage = Decay; pos = 0; inc = (float) (1.0 / (dT * sr)); }
                else level = start + (1.0f - start) * curve (pos, aK);
                break;
            case Decay:
                pos += inc;
                if (pos >= 1.0f) { level = sustain; stage = Sustain; if (oneShot) noteOff(); }
                else level = 1.0f + (sustain - 1.0f) * curve (pos, dK);
                break;
            case Sustain: level = sustain; if (oneShot) noteOff(); break;
            case Release:
                pos += inc;
                if (pos >= 1.0f) { level = 0; stage = Idle; }
                else level = start * (1.0f - curve (pos, dK));
                break;
        }
        return level;
    }
    // advance n samples at once (for control-rate envelopes)
    float advance (int n)
    {
        float v = 0;
        for (int i = 0; i < n; ++i) v = next();
        return v;
    }
};

// ---------- LFO (sine) ----------
struct Lfo
{
    float phase = 0, inc = 0;
    void setHz (float hz, double sr) { inc = (float) (hz / sr); }
    float next (int n = 1)
    {
        phase += inc * n;
        phase -= std::floor (phase);
        return std::sin (2.0f * kPi * phase);
    }
    float value() const { return std::sin (2.0f * kPi * phase); }
};

// ---------- state variable filter (Cytomic / TPT) ----------
struct Svf
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void set (float hz, float q01, double sr)
    {
        hz = juce::jlimit (10.0f, (float) sr * 0.45f, hz);
        g = std::tan (kPi * hz / (float) sr);
        float Q = 0.5f + q01 * q01 * 24.0f;
        k = 1.0f / Q;
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    // type: 1 LP, 2 BP, 3 HP
    float process (float v0, int type)
    {
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        if (type == 1) return v2;
        if (type == 2) return v1;
        return v0 - k * v1 - v2;
    }
};

struct OnePole
{
    float z = 0, a = 0;
    void setHz (float hz, double sr) { a = std::exp (-2.0f * kPi * juce::jlimit (5.0f, (float) sr * 0.49f, hz) / (float) sr); }
    float lp (float x) { z = x + a * (z - x); return z; }
};

// ---------- shelving EQ (bass/treble) ----------
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x)
    {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void shelf (bool high, float hz, float db, double sr)
    {
        float A = std::pow (10.0f, db / 40.0f), w0 = 2 * kPi * hz / (float) sr;
        float cs = std::cos (w0), sn = std::sin (w0), alpha = sn / 2 * std::sqrt (2.0f), sa = 2 * std::sqrt (A) * alpha;
        float B0, B1, B2, A0, A1, A2;
        if (! high)
        {
            B0 = A * ((A + 1) - (A - 1) * cs + sa); B1 = 2 * A * ((A - 1) - (A + 1) * cs); B2 = A * ((A + 1) - (A - 1) * cs - sa);
            A0 = (A + 1) + (A - 1) * cs + sa; A1 = -2 * ((A - 1) + (A + 1) * cs); A2 = (A + 1) + (A - 1) * cs - sa;
        }
        else
        {
            B0 = A * ((A + 1) + (A - 1) * cs + sa); B1 = -2 * A * ((A - 1) + (A + 1) * cs); B2 = A * ((A + 1) + (A - 1) * cs - sa);
            A0 = (A + 1) - (A - 1) * cs + sa; A1 = 2 * ((A - 1) - (A + 1) * cs); A2 = (A + 1) - (A - 1) * cs - sa;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
};

// ---------- tempo-synced divisions for 'Delay uses MIDI clk' (values 1-18) ----------
inline double delayDivisionBeats (int v)
{
    static const double beats[] = { 0.125, 1.0 / 6, 0.25, 1.0 / 3, 0.5, 2.0 / 3, 0.75, 1.0, 4.0 / 3, 1.5, 2.0, 8.0 / 3, 3.0, 4.0, 16.0 / 3, 6.0, 8.0, 16.0 };
    return beats[juce::jlimit (1, 18, v) - 1];
}
inline juce::String delayDivisionName (int v)
{
    static const char* n[] = { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1T", "1/2D", "1", "2T", "1D", "2", "4" };
    return n[juce::jlimit (1, 18, v) - 1];
}

// ---------- stereo delay (Off / Stereo / Ping-pong) ----------
struct StereoDelay
{
    std::vector<float> bufL, bufR;
    int w = 0, size = 0;
    float timeSmoothed = -1;
    double sr = 48000;
    void prepare (double sampleRate, float maxSeconds)
    {
        sr = sampleRate;
        size = (int) (sr * maxSeconds) + 4;
        bufL.assign ((size_t) size, 0.0f); bufR.assign ((size_t) size, 0.0f);
        w = 0; timeSmoothed = -1;
    }
    void clear() { std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f); }
    // mode 0 off, 1 stereo, 2 ping-pong
    void process (float* L, float* R, int n, int mode, float levelDb, float timeMs, float fb01)
    {
        if (mode == 0 || size == 0) return;
        float lvl = levelDb <= -40 ? 0.0f : dbToGain (levelDb);
        float target = juce::jlimit (1.0f, (float) (size - 3), (float) (timeMs * 0.001 * sr));
        if (timeSmoothed < 0) timeSmoothed = target;
        for (int i = 0; i < n; ++i)
        {
            timeSmoothed += (target - timeSmoothed) * 0.0005f;
            float rp = (float) w - timeSmoothed;
            if (rp < 0) rp += (float) size;
            int i0 = (int) rp; float fr = rp - (float) i0; int i1 = (i0 + 1) % size;
            float dl = bufL[(size_t) i0] + fr * (bufL[(size_t) i1] - bufL[(size_t) i0]);
            float dr = bufR[(size_t) i0] + fr * (bufR[(size_t) i1] - bufR[(size_t) i0]);
            float inL = L[i], inR = R[i];
            if (mode == 1)
            {
                bufL[(size_t) w] = inL + dl * fb01;
                bufR[(size_t) w] = inR + dr * fb01;
            }
            else
            {
                bufL[(size_t) w] = 0.5f * (inL + inR) + dr * fb01;
                bufR[(size_t) w] = dl * fb01;
            }
            L[i] = inL + dl * lvl;
            R[i] = inR + dr * lvl;
            if (++w >= size) w = 0;
        }
    }
};

// ---------- Juno-6 style BBD chorus (modes I / II) ----------
struct JunoChorus
{
    std::vector<float> buf;
    int w = 0, size = 0;
    double sr = 48000;
    float phase = 0;
    OnePole lpA, lpB, inLp;
    void prepare (double sampleRate)
    {
        sr = sampleRate; size = (int) (sr * 0.02) + 4; buf.assign ((size_t) size, 0.0f); w = 0;
        lpA.setHz (9000, sr); lpB.setHz (9000, sr); inLp.setHz (11000, sr);
    }
    float tap (float delaySamples)
    {
        float rp = (float) w - delaySamples;
        if (rp < 0) rp += (float) size;
        int i0 = (int) rp; float fr = rp - (float) i0; int i1 = (i0 + 1) % size;
        return buf[(size_t) i0] + fr * (buf[(size_t) i1] - buf[(size_t) i0]);
    }
    void process (float* L, float* R, int n, int mode)
    {
        if (mode == 0 || size == 0) return;
        const float rate = mode == 1 ? 0.513f : 0.863f;
        const float minMs = mode == 1 ? 1.66f : 1.66f, maxMs = mode == 1 ? 5.35f : 5.35f;
        const float depth = mode == 1 ? 0.5f : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            phase += rate / (float) sr; if (phase >= 1) phase -= 1;
            float tri = phase < 0.5f ? 4 * phase - 1 : 3 - 4 * phase; // -1..1
            float mid = 0.5f * (minMs + maxMs), half = 0.5f * (maxMs - minMs) * depth;
            float dA = (mid + half * tri) * 0.001f * (float) sr;
            float dB = (mid - half * tri) * 0.001f * (float) sr;
            float mono = inLp.lp (0.5f * (L[i] + R[i]));
            buf[(size_t) w] = mono;
            float a = lpA.lp (tap (dA)), b = lpB.lp (tap (dB));
            L[i] = L[i] + a * 0.7f;
            R[i] = R[i] + b * 0.7f;
            if (++w >= size) w = 0;
        }
    }
};

// ---------- Scala tuning (.scl + .kbm) ----------
struct ScalaTuning
{
    bool active = false;
    std::vector<double> cents; // degrees 1..n (last = period)
    // kbm
    int mapSize = 0, firstNote = 0, lastNote = 127, middleNote = 60, refNote = 69, octaveDegree = 0;
    double refFreq = 440.0;
    std::vector<int> mapping; // -1 = unmapped
    juce::String description, error;

    static bool parseScl (const juce::String& text, std::vector<double>& out, juce::String& desc)
    {
        juce::StringArray lines; lines.addLines (text);
        int state = 0, count = 0;
        for (auto l : lines)
        {
            l = l.trim();
            if (l.startsWith ("!")) continue;
            if (state == 0) { desc = l; state = 1; continue; }
            if (state == 1) { count = l.getIntValue(); state = 2; continue; }
            if ((int) out.size() >= count) break;
            auto tok = l.upToFirstOccurrenceOf (" ", false, false).trim();
            if (tok.containsChar ('.')) out.push_back (tok.getDoubleValue());
            else if (tok.containsChar ('/'))
            {
                double a = tok.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
                double b = tok.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
                out.push_back (b > 0 ? 1200.0 * std::log2 (a / b) : 0.0);
            }
            else out.push_back (1200.0 * std::log2 (std::max (1e-9, tok.getDoubleValue())));
        }
        return count > 0 && (int) out.size() == count;
    }
    bool loadKbm (const juce::String& text)
    {
        juce::StringArray lines; lines.addLines (text);
        std::vector<juce::String> vals;
        for (auto l : lines) { l = l.trim(); if (l.isEmpty() || l.startsWith ("!")) continue; vals.push_back (l.upToFirstOccurrenceOf (" ", false, false)); }
        if (vals.size() < 7) return false;
        mapSize = vals[0].getIntValue(); firstNote = vals[1].getIntValue(); lastNote = vals[2].getIntValue();
        middleNote = vals[3].getIntValue(); refNote = vals[4].getIntValue(); refFreq = vals[5].getDoubleValue();
        octaveDegree = vals[6].getIntValue();
        mapping.clear();
        for (size_t i = 7; i < vals.size() && (int) mapping.size() < mapSize; ++i)
            mapping.push_back (vals[i].equalsIgnoreCase ("x") ? -1 : vals[i].getIntValue());
        while ((int) mapping.size() < mapSize) mapping.push_back (-1);
        return true;
    }
    void identityKbm() { mapSize = 0; firstNote = 0; lastNote = 127; middleNote = 60; refNote = 69; refFreq = 440.0; octaveDegree = 0; mapping.clear(); }

    double degreeCents (int deg) const // signed degree relative to middle note
    {
        int n = (int) cents.size();
        int oct = (int) std::floor ((double) deg / n);
        int d = deg - oct * n;
        double period = cents.back();
        return oct * period + (d == 0 ? 0.0 : cents[(size_t) d - 1]);
    }
    // returns fractional MIDI pitch for an integer key (or NaN if unmapped)
    double pitchFor (int key) const
    {
        if (! active || cents.empty()) return key;
        int deg;
        if (mapSize == 0) deg = key - middleNote;
        else
        {
            int rel = key - middleNote;
            int oct = (int) std::floor ((double) rel / mapSize);
            int idx = rel - oct * mapSize;
            int m = mapping[(size_t) idx];
            if (m < 0) return std::nan ("");
            int od = octaveDegree > 0 ? octaveDegree : (int) cents.size();
            deg = oct * od + m;
        }
        // reference: refNote has refFreq
        int refDeg;
        if (mapSize == 0) refDeg = refNote - middleNote;
        else
        {
            int rel = refNote - middleNote; int oct = (int) std::floor ((double) rel / mapSize); int idx = rel - oct * mapSize;
            int od = octaveDegree > 0 ? octaveDegree : (int) cents.size();
            refDeg = oct * od + std::max (0, mapping[(size_t) idx]);
        }
        double c = degreeCents (deg) - degreeCents (refDeg);
        double f = refFreq * std::pow (2.0, c / 1200.0);
        return 69.0 + 12.0 * std::log2 (f / 440.0);
    }
};
} // namespace ex

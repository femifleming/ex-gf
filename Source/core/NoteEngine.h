#pragma once
// Polyphonic note handling shared by the SD Multisample / Poly Wavetable / Poly Exciter / Poly FM algorithms:
// chord generator, arpeggiator, broken chords, sustain (synth/piano), unison and voice allocation.
#include "ExDsp.h"
#include <functional>
#include <algorithm>

namespace ex
{
// ---- chord scales & shapes (disting EX manual, SD Multisample section) ----
inline const std::vector<int>& chordScale (int s)
{
    static const std::vector<std::vector<int>> scales = {
        { 0, 2, 4, 5, 7, 9, 11 },        // Major
        { 0, 2, 3, 5, 7, 8, 10 },        // Natural Minor
        { 0, 2, 4, 5, 7, 9, 10 },        // Dominant
        { 0, 2, 3, 5, 6, 8, 9, 11 },     // Fully Diminished
        { 0, 1, 3, 4, 6, 7, 9, 10 },     // Dominant Dim
        { 0, 3, 4, 7, 8, 11 },           // Augmented
        { 0, 2, 4, 6, 8, 10 },           // Whole Tone
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, // Chromatic
        { 0, 2, 3, 5, 7, 8, 11 },        // Harmonic Minor
    };
    return scales[(size_t) juce::jlimit (0, 8, s)];
}
inline juce::String chordScaleName (int s)
{
    static const char* n[] = { "Major", "Natural Minor", "Dominant", "Fully Diminished", "Dominant Dim", "Augmented", "Whole Tone", "Chromatic", "Harmonic Minor" };
    return n[juce::jlimit (0, 8, s)];
}
// shape degrees; 'O' (=100) means one octave in scale degrees
inline const std::vector<int>& chordShape (int s)
{
    static const std::vector<std::vector<int>> shapes = {
        { 0 }, { 0, 100 }, { 0, 100, 200 }, { 0, 4 }, { 0, 4, 100 }, { 0, 2, 4 }, { 0, 2, 4, 100 },
        { 0, 3, 4 }, { 0, 3, 4, 100 }, { 0, 2, 4, 5 }, { 0, 2, 4, 5, 100 }, { 0, 2, 4, 6 }, { 0, 2, 4, 6, 100 }, { 0, 2, 4, 6, 101 },
    };
    return shapes[(size_t) juce::jlimit (0, 13, s)];
}
inline juce::String chordShapeName (int s)
{
    static const char* n[] = { "None", "Octave", "Two Octaves", "Root/Fifth", "Root/Fifth + 8ve", "Triad", "Triad + 8ve", "Sus4", "Sus4 + 8ve",
                               "Sixth", "Sixth + 8ve", "Seventh", "Seventh + 8ve", "Ninth" };
    return n[juce::jlimit (0, 13, s)];
}
inline bool chordShapeEndsWithOctave (int s) { return s == 1 || s == 2 || s == 4 || s == 6 || s == 8 || s == 10 || s == 12; }
inline juce::String arpModeName (int m)
{
    static const char* n[] = { "Off", "Up", "Down", "Alt", "Alt2", "Up -8ve", "Down -8ve", "Alt -8ve", "Alt2 -8ve", "Step Up", "Step Down",
                               "Random", "Random2", "Random3", "As Played" };
    return n[juce::jlimit (0, 14, m)];
}
inline juce::String chordEnableName (int v)
{
    static const char* n[] = { "Off", "On", "Gate 1", "Gate 2", "Gate 3", "Gates 1+2", "Gates 1+3", "Gates 2+3" };
    return n[juce::jlimit (0, 7, v)];
}
inline bool chordEnabledFor (int v, int gate) // gate 0..2
{
    static const int masks[] = { 0, 7, 1, 2, 4, 3, 5, 6 };
    return (masks[juce::jlimit (0, 7, v)] >> juce::jlimit (0, 2, gate)) & 1;
}
inline juce::String keyName (int k)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (k > 0 ? "+" : "") + juce::String (k) + " " + n[((k % 12) + 12) % 12];
}

struct ChordSettings
{
    int enable = 0, key = 0, scale = 0, shape = 0, inversion = 0;
    int arpMode[3] { 0, 0, 0 }, arpRange[3] { 1, 1, 1 };
    int breakMs = 0, breakDir = 0;
};

// builds the chord notes for an input note
inline std::vector<int> buildChord (int note, const ChordSettings& cs)
{
    const auto& sc = chordScale (cs.scale);
    const int n = (int) sc.size();
    // quantise note to scale (round down to nearest degree)
    int rel = note - cs.key;
    int oct = (int) std::floor (rel / 12.0);
    int within = rel - oct * 12;
    int deg = 0;
    for (int i = 0; i < n; ++i) if (sc[(size_t) i] <= within) deg = i;
    int baseDeg = oct * n + deg;
    std::vector<int> out;
    for (int d : chordShape (cs.shape))
    {
        int dd = d >= 100 ? baseDeg + (d / 100) * n + (d % 100) : baseDeg + d;
        if (d == 101) dd = baseDeg + n + 1; // ninth = 2(8ve)
        int o = (int) std::floor ((double) dd / n);
        int k = dd - o * n;
        out.push_back (cs.key + o * 12 + sc[(size_t) k]);
    }
    for (int i = 0; i < cs.inversion && i < (int) out.size(); ++i) out[(size_t) i] += 12;
    std::sort (out.begin(), out.end());
    return out;
}

class PolyNoteEngine
{
public:
    static constexpr int kMaxVoices = 8;

    struct VoiceStart
    {
        int voice;
        float pitch;     // final fractional MIDI pitch (without pitch bend)
        float velocity;  // 0..1 (after curve)
        int timbre, gate, note;
        int unisonIndex, unisonCount;
        bool oneShot;
    };

    // ----- host-provided callbacks -----
    std::function<void (const VoiceStart&)> startVoice;
    std::function<void (int voice)> releaseVoice;
    std::function<void (int voice)> killVoice;     // fast fade for steal (optional)
    std::function<bool (int voice)> voiceSounding; // still producing sound?

    // ----- config (update every block) -----
    int maxVoices = 8, unison = 1;
    float unisonDetuneCents = 10.0f;
    int transpose = 0;          // semitones (octave*12 + transpose)
    float fineCents = 0;
    int sustainMode = 0;        // 0 synth, 1 piano
    int triggerMode = 0;        // 0 gated, 1 drone, 2 triggered
    int velCurve = 0;
    ChordSettings chords;
    const ScalaTuning* tuning = nullptr;
    int transposeLimit = -1;    // semitones, -1 off (SD Multisample option, applied by the voice)

    // ----- state visible to display -----
    int voiceNote[kMaxVoices];
    bool voiceGate[kMaxVoices];
    int keysHeld = 0;

    PolyNoteEngine() { reset(); }

    void reset()
    {
        for (int v = 0; v < kMaxVoices; ++v) { voiceNote[v] = -1; voiceGate[v] = false; voiceTrig[v] = -1; voiceAge[v] = 0; voiceSustained[v] = false; }
        held.clear(); pending.clear(); sustainOn = false; keysHeld = 0;
        for (auto& a : arp) a = ArpState();
    }

    void noteOn (int note, float vel01, int timbre, int gate, int64_t now)
    {
        float v = velocityCurve (velCurve, vel01);
        // piano/synth sustain behaviour for a key already sounding under sustain
        for (int vi = 0; vi < kMaxVoices; ++vi)
            if (voiceSustained[vi] && voiceKey[vi] == note && voiceTimbre[vi] == timbre)
            {
                if (sustainMode == 0) return; // synth: cannot retrigger
                releaseVoiceInternal (vi);   // piano: retrigger
            }

        const int trig = ++trigCounter;
        held.push_back ({ note, timbre, gate, trig, now });
        keysHeld = (int) held.size();

        std::vector<int> notes;
        const bool chordOn = chordEnabledFor (chords.enable, gate);
        if (chordOn) notes = buildChord (note, chords);
        else notes = { note };

        auto& as = arp[(size_t) juce::jlimit (0, 2, gate)];
        const int mode = chords.arpMode[juce::jlimit (0, 2, gate)];
        if (mode > 0)
        {
            std::vector<int> pool;
            if (chordOn) pool = notes;
            else
                for (auto& h : held) if (h.gate == gate) pool.push_back (h.note);
            int pick = arpNext (as, pool, mode, chords.arpRange[juce::jlimit (0, 2, gate)], chordOn && chordShapeEndsWithOctave (chords.shape));
            notes = { pick };
        }
        // break (strum) scheduling
        const int nNotes = (int) notes.size();
        bool down = chords.breakDir == 1 || (chords.breakDir == 2 && as.breakFlip);
        if (chords.breakDir == 2) as.breakFlip = ! as.breakFlip;
        for (int i = 0; i < nNotes; ++i)
        {
            int idx = down ? nNotes - 1 - i : i;
            int64_t at = now + (int64_t) (i * chords.breakMs * sampleRate / 1000.0);
            if (chords.breakMs > 0 && i > 0) pending.push_back ({ at, notes[(size_t) idx], v, timbre, gate, trig, note });
            else fire (notes[(size_t) idx], v, timbre, gate, trig, note);
        }
    }

    void noteOff (int note, int timbre)
    {
        for (size_t i = 0; i < held.size(); ++i)
            if (held[i].note == note && held[i].timbre == timbre)
            {
                const int trig = held[i].trig;
                held.erase (held.begin() + (long) i);
                keysHeld = (int) held.size();
                pending.erase (std::remove_if (pending.begin(), pending.end(), [&] (const Pending& p) { return p.trig == trig; }), pending.end());
                if (triggerMode != 0) return; // drone / triggered: ignore gate off
                for (int v = 0; v < kMaxVoices; ++v)
                    if (voiceTrig[v] == trig && voiceGate[v])
                    {
                        if (sustainOn) voiceSustained[v] = true;
                        else releaseVoiceInternal (v);
                    }
                return;
            }
    }

    void setSustain (bool on)
    {
        if (on == sustainOn) return;
        sustainOn = on;
        if (! on)
            for (int v = 0; v < kMaxVoices; ++v)
                if (voiceSustained[v]) releaseVoiceInternal (v);
    }
    bool sustainActive() const { return sustainOn; }

    void allNotesOff()
    {
        held.clear(); pending.clear(); keysHeld = 0;
        for (int v = 0; v < kMaxVoices; ++v) if (voiceGate[v] || voiceSustained[v]) releaseVoiceInternal (v);
    }
    void resetArp (int gate = -1)
    {
        for (int g = 0; g < 3; ++g) if (gate < 0 || g == gate) arp[(size_t) g] = ArpState();
    }

    // call regularly from the audio thread with the current sample clock
    void tick (int64_t now)
    {
        if (pending.empty()) return;
        for (size_t i = 0; i < pending.size();)
        {
            if (pending[i].at <= now)
            {
                auto p = pending[i];
                pending.erase (pending.begin() + (long) i);
                fire (p.note, p.vel, p.timbre, p.gate, p.trig, p.key);
            }
            else ++i;
        }
        // tidy voices that have finished
    }
    void voiceFinished (int v) { voiceNote[v] = -1; voiceGate[v] = false; voiceSustained[v] = false; voiceTrig[v] = -1; }

    double sampleRate = 48000;

private:
    struct Held { int note, timbre, gate, trig; int64_t at; };
    struct Pending { int64_t at; int note; float vel; int timbre, gate, trig, key; };
    struct ArpState
    {
        int step = -1;
        int lastPick = -1;
        std::vector<int> perm;
        bool breakFlip = false;
    };
    std::vector<Held> held;
    std::vector<Pending> pending;
    ArpState arp[3];
    bool sustainOn = false;
    int trigCounter = 0;
    int voiceTrig[kMaxVoices], voiceKey[kMaxVoices] {}, voiceTimbre[kMaxVoices] {};
    int64_t voiceAge[kMaxVoices];
    bool voiceSustained[kMaxVoices];
    int64_t ageCounter = 0;
    std::minstd_rand rng { 12345 };

    void releaseVoiceInternal (int v)
    {
        voiceGate[v] = false; voiceSustained[v] = false;
        if (releaseVoice) releaseVoice (v);
    }

    int allocate()
    {
        const int mv = juce::jlimit (1, kMaxVoices, maxVoices);
        int best = -1; int64_t bestAge = INT64_MAX;
        for (int v = 0; v < mv; ++v)
            if (! voiceGate[v] && ! voiceSustained[v] && ! (voiceSounding && voiceSounding (v))) { return v; }
        // oldest released voice
        for (int v = 0; v < mv; ++v)
            if (! voiceGate[v] && ! voiceSustained[v] && voiceAge[v] < bestAge) { best = v; bestAge = voiceAge[v]; }
        if (best >= 0) return best;
        for (int v = 0; v < mv; ++v)
            if (voiceAge[v] < bestAge) { best = v; bestAge = voiceAge[v]; }
        return best;
    }

    void fire (int note, float vel, int timbre, int gate, int trig, int key)
    {
        const int u = juce::jlimit (1, kMaxVoices, unison);
        for (int i = 0; i < u; ++i)
        {
            int v = allocate();
            if (v < 0) return;
            if (voiceGate[v] || voiceSustained[v] || (voiceSounding && voiceSounding (v)))
                if (killVoice) killVoice (v);
            float detune = u > 1 ? unisonDetuneCents * (2.0f * i / (u - 1) - 1.0f) : 0.0f;
            int k = note + transpose;
            double p = tuning != nullptr && tuning->active ? tuning->pitchFor (juce::jlimit (0, 127, k)) + (k - juce::jlimit (0, 127, k)) : (double) k;
            if (std::isnan (p)) return;
            voiceNote[v] = juce::jlimit (0, 127, note);
            voiceGate[v] = true; voiceSustained[v] = false;
            voiceTrig[v] = trig; voiceKey[v] = key; voiceTimbre[v] = timbre;
            voiceAge[v] = ++ageCounter;
            if (startVoice)
                startVoice ({ v, (float) p + (fineCents + detune) * 0.01f, vel, timbre, gate, note, i, u, triggerMode == 2 });
        }
    }

    int arpNext (ArpState& as, std::vector<int> pool, int mode, int range, bool shapeEndsOctave)
    {
        if (pool.empty()) return 60;
        if (mode != 14) std::sort (pool.begin(), pool.end());
        // range copies
        std::vector<int> pat = pool;
        const bool skipDup = (mode >= 5 && mode <= 8) && shapeEndsOctave;
        for (int r = 1; r < range; ++r)
            for (size_t i = 0; i < pool.size(); ++i)
            {
                if (skipDup && i == 0) continue;
                pat.push_back (pool[i] + 12 * r);
            }
        const int n = (int) pat.size();
        std::vector<int> seq;
        int m = mode >= 5 && mode <= 8 ? mode - 4 : mode;
        switch (m)
        {
            case 1: seq = pat; break;
            case 2: seq.assign (pat.rbegin(), pat.rend()); break;
            case 3: seq = pat; for (int i = n - 2; i >= 1; --i) seq.push_back (pat[(size_t) i]); break;
            case 4: seq = pat; for (int i = n - 1; i >= 0; --i) seq.push_back (pat[(size_t) i]); break;
            case 9: for (int i = 1; i < n; ++i) { seq.push_back (pat[0]); seq.push_back (pat[(size_t) i]); } if (n == 1) seq = pat; break;
            case 10: for (int i = n - 2; i >= 0; --i) { seq.push_back (pat[(size_t) n - 1]); seq.push_back (pat[(size_t) i]); } if (n == 1) seq = pat; break;
            case 11: return pat[(size_t) (rng() % (unsigned) n)];
            case 12:
            {
                int pick = (int) (rng() % (unsigned) n);
                if (n > 1 && pick == as.lastPick) pick = (pick + 1 + (int) (rng() % (unsigned) (n - 1))) % n;
                as.lastPick = pick; return pat[(size_t) pick];
            }
            case 13:
            {
                if (as.perm.empty() || (int) as.perm.size() != n || as.step >= n - 1)
                {
                    as.perm.resize ((size_t) n);
                    for (int i = 0; i < n; ++i) as.perm[(size_t) i] = i;
                    std::shuffle (as.perm.begin(), as.perm.end(), rng);
                    as.step = -1;
                }
                ++as.step;
                return pat[(size_t) as.perm[(size_t) as.step]];
            }
            default: seq = pat; break; // As Played
        }
        if (seq.empty()) seq = pat;
        as.step = (as.step + 1) % (int) seq.size();
        return seq[(size_t) as.step];
    }
};
} // namespace ex

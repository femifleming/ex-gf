#pragma once
// Base for the polyphonic algorithms: MIDI dispatch -> PolyNoteEngine, sample-accurate chunked rendering,
// pitch bend / sustain / aftertouch, output spread and the voice display line.
#include "ExPlugin.h"

namespace ex
{
class PolyAlgorithm : public ExAlgorithm
{
public:
    using ExAlgorithm::ExAlgorithm;

    PolyNoteEngine engine;
    int numTimbres = 1;        // MIDI channels -> timbres
    bool midiIsGate3 = true;   // poly synths: MIDI notes count as gate 3
    float bendSemis = 0;
    bool ccSustain = false;
    float channelPressure = 0;
    float polyPressure[128] {};
    double sr = 48000;
    int maxBlock = 512;
    ScalaTuning tuning;
    int loadedScl = -9999, loadedKbm = -9999;

    // per-voice level for the display bars (0..1), written by the audio thread
    std::atomic<float> voiceLevel[PolyNoteEngine::kMaxVoices];

    void preparePoly (double sampleRate, int block)
    {
        sr = sampleRate; maxBlock = block;
        engine.sampleRate = sampleRate;
        engine.reset();
        for (auto& v : voiceLevel) v = 0.0f;
    }

    // ---- to implement ----
    virtual void configure() = 0;                                    // read params -> engine/voices (once per block)
    virtual void renderVoices (float* const* out, int start, int n) = 0; // add voices into out[0..3]
    virtual void postProcess (float* const* out, int n) { juce::ignoreUnused (out, n); }
    virtual void onPressure() {}
    virtual void onOtherMidi (const juce::MidiMessage& m) { juce::ignoreUnused (m); }
    virtual int bendRangeParam() const = 0;
    virtual int sustainParam() const = 0;
    virtual bool timbreAcceptsNote (int timbre, int note) const { juce::ignoreUnused (timbre, note); return true; }

    void allNotesOff() override { engine.allNotesOff(); }

    void process (juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, juce::MidiBuffer& midi, juce::MidiBuffer& midiOut) override
    {
        currentMidiOut = &midiOut;
        inBuffer = &in;
        configure();
        const int n = out.getNumSamples();
        float* ch[4] = { out.getWritePointer (0), out.getWritePointer (1), out.getWritePointer (2), out.getWritePointer (3) };
        int pos = 0;
        auto it = midi.cbegin();
        const int64_t base = plugin.sampleClock;
        while (pos < n)
        {
            while (it != midi.cend() && (*it).samplePosition <= pos)
            {
                handleMidi ((*it).getMessage(), base + pos);
                ++it;
            }
            int next = n;
            if (it != midi.cend()) next = juce::jmin (next, (*it).samplePosition);
            int len = juce::jmin (32, next - pos);
            if (len <= 0) len = 1;
            engine.tick (base + pos);
            float* sub[4] = { ch[0] + pos, ch[1] + pos, ch[2] + pos, ch[3] + pos };
            renderVoices (sub, pos, len);
            pos += len;
        }
        for (; it != midi.cend(); ++it) handleMidi ((*it).getMessage(), base + n);
        postProcess (ch, n);
    }

    void handleMidi (const juce::MidiMessage& m, int64_t now)
    {
        const int chn = m.getChannel();
        if (chn <= 0) { onOtherMidi (m); return; }
        const int timbre = plugin.timbreForChannel (chn, numTimbres);
        if (timbre < 0) { onOtherMidi (m); return; }
        if (m.isNoteOn())
        {
            int note = m.getNoteNumber();
            if (! acceptsNote (note) || ! timbreAcceptsNote (timbre, note)) return;
            polyPressure[note] = 0;
            engine.noteOn (note, m.getFloatVelocity(), timbre, midiIsGate3 ? 2 : juce::jmin (2, timbre), now);
        }
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber(), timbre);
        else if (m.isSustainPedalOn()) { ccSustain = true; }
        else if (m.isSustainPedalOff()) { ccSustain = false; }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
        else if (m.isPitchWheel()) bendSemis = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * (float) pv (bendRangeParam());
        else if (m.isChannelPressure()) { channelPressure = (float) m.getChannelPressureValue() / 127.0f; onPressure(); }
        else if (m.isAftertouch()) { polyPressure[m.getNoteNumber()] = (float) m.getAfterTouchValue() / 127.0f; onPressure(); }
        else onOtherMidi (m);
    }

    void updateSustain() { engine.setSustain (ccSustain || pv (sustainParam()) != 0); }

    // pan (-1..1) for a voice in the given spread mode
    static float spreadPan (int mode, int voice, float pitch, float spread01)
    {
        float p = 0;
        if (mode == 0) p = (float) voice / 3.5f - 1.0f;
        else if (mode == 1) p = ((voice & 1) ? 1.0f : -1.0f) * (float) (voice / 2 + 1) / 4.0f;
        else p = juce::jlimit (-1.0f, 1.0f, (pitch - 48.0f) / 36.0f);
        return juce::jlimit (-1.0f, 1.0f, p * spread01);
    }
    static void panGains (float pan, float& gl, float& gr)
    {
        float a = (pan + 1.0f) * 0.25f * kPi;
        gl = std::cos (a) * 1.41421356f * 0.7071f; gr = std::sin (a) * 1.41421356f * 0.7071f;
    }

    // Scala / KBM files on the card ('scl' and 'kbm' folders)
    static juce::Array<juce::File> tuningFiles (const char* folder, const char* ext)
    {
        return SdCard::sortedChildren (SdCard::root().getChildFile (folder), juce::File::findFiles, juce::String ("*.") + ext);
    }
    juce::String scalaName (int v) const
    {
        if (v == 0) return "None";
        if (v == 1) return "None from SysEx";
        if (v < 0) return "MTS n/a";
        auto f = tuningFiles ("scl", "scl");
        return v - 2 < f.size() ? f[v - 2].getFileName() : juce::String ("None");
    }
    juce::String kbmName (int v) const
    {
        if (v == 0) return "None";
        if (v == 1) return "None from SysEx";
        auto f = tuningFiles ("kbm", "kbm");
        return v - 2 < f.size() ? f[v - 2].getFileName() : juce::String ("None");
    }
    // call from the message thread (onSelected / confirm) or lazily: loads files when params change
    void updateTuning (int sclParam, int kbmParam)
    {
        int s = pv (sclParam), k = pv (kbmParam);
        if (s == loadedScl && k == loadedKbm) return;
        loadedScl = s; loadedKbm = k;
        ScalaTuning t;
        auto sf = tuningFiles ("scl", "scl"), kf = tuningFiles ("kbm", "kbm");
        if (s >= 2 && s - 2 < sf.size() && k >= 2 && k - 2 < kf.size())
        {
            juce::String desc;
            if (ScalaTuning::parseScl (sf[s - 2].loadFileAsString(), t.cents, desc) && t.loadKbm (kf[k - 2].loadFileAsString()))
            {
                t.description = desc;
                t.active = true;
                for (int m : t.mapping) if (m > (int) t.cents.size()) { t.active = false; t.error = "KBM MISMATCH"; }
            }
        }
        tuning = t;
    }

    // voice numbers line: "-- 62 76 ..." with envelope bars
    void drawVoiceLine (Oled& o, bool showNotes = true)
    {
        // up to 8 voices: note numbers, 16 px apart; more: 16 narrow level bars
        const bool wide = engine.maxVoices <= 8;
        const int n = wide ? 8 : PolyNoteEngine::kMaxVoices, step = 128 / n;
        for (int v = 0; v < n; ++v)
        {
            int x = v * step;
            int note = engine.voiceNote[v];
            float lvl = voiceLevel[v].load();
            if (note < 0 || (lvl <= 0.0005f && ! engine.voiceGate[v])) { o.text (wide ? x : x + 1, 24, wide ? "--" : "-"); continue; }
            if (showNotes && wide) o.text (x, 24, juce::String (note).paddedLeft ('0', 2).substring (0, 2));
            int h = juce::jlimit (wide ? 0 : 1, 8, (int) std::ceil (lvl * 8.0f));
            o.invertRect (x, 32 - h, wide ? 11 : 7, h);
        }
    }

protected:
    const juce::AudioBuffer<float>* inBuffer = nullptr;
    juce::MidiBuffer* currentMidiOut = nullptr;
};
} // namespace ex

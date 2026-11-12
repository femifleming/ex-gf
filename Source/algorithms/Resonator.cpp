#include "Resonator.h"

namespace ex
{
static const char* const kModes[] = { "Modal", "Sympathetic strings", "String", "FM voice", "Symp. quantized", "String + reverb", "String synth" };
static const char* const kSynthFx[] = { "Formant", "Chorus", "Reverb", "Formant 2", "Ensemble", "Reverb 2" };
static const char* const kChords[] = { "Octaves", "Fifths", "Sus 4", "Minor", "Minor 7", "Minor 9", "Minor 11", "6/9", "Major 9", "Major 7", "Major" };

// balance law: unity at the centre, one side fades out towards the other
static void balance (float pan, float& l, float& r)
{
    pan = juce::jlimit (-1.0f, 1.0f, pan);
    l = juce::jmin (1.0f, 1.0f - pan); r = juce::jmin (1.0f, 1.0f + pan);
}

Resonator::Resonator (ExPlugin& p) : FxAlgorithm (p, 20, "Resonator")
{
    addAttenuverters();
    juce::StringArray modes, fx;
    for (auto* m : kModes) modes.add (m);
    for (auto* f : kSynthFx) fx.add (f);
    add ("Mode", 0, 6, 0, {}, modes);                                   // 7
    add ("Synth effect", 0, 5, 0, {}, fx);                              // 8
    add ("Polyphony", 1, 4, 1);                                         // 9
    add ("Coarse tune", -60, 60, 0, "ST");                              // 10
    add ("Fine tune", -100, 100, 0, "cents");                           // 11
    add ("Structure", 0, 127, 64);                                      // 12
    add ("Brightness", 0, 127, 64);                                     // 13
    add ("Damping", 0, 127, 64);                                        // 14
    add ("Position", 0, 127, 64);                                       // 15
    add ("Chord", 0, 10, 0); defs.back().fmt = [] (int v) { return juce::String (kChords[juce::jlimit (0, 10, v)]); }; // 16
    add ("Noise gate", 0, 1, 1, {}, { "Off", "On" });                   // 17
    add ("Input gain", -40, 12, 0, "dB");                               // 18
    add ("Output gain", -40, 12, 0, "dB");                              // 19
    add ("Dry gain", -40, 12, 0, "dB");                                 // 20
    add ("Odd pan", -100, 100, -100, "%");                              // 21
    add ("Even pan", -100, 100, 100, "%");                              // 22
    add ("Dry pan", -100, 100, 0, "%");                                 // 23
    add ("Strum input", 0, 6, 0);                                       // 24
    add ("V/Oct input", 0, 6, 0);                                       // 25
    add ("Audio input", 0, 6, 2);                                       // 26
    defs.back().fmt = [] (int v) { return v == 0 ? juce::String ("None") : juce::String ("Input"); };
    add ("Brightness input", 0, 6, 5);                                  // 27
    add ("Frequency input", 0, 6, 0);                                   // 28
    add ("Damping input", 0, 6, 6);                                     // 29
    add ("Structure input", 0, 6, 1);                                   // 30
    add ("Position input", 0, 6, 3);                                    // 31
    // the module defaults to Off; a plug-in has no CV, so notes set the pitch and strum unless told otherwise
    add ("MIDI mode", 0, 3, 3, {}, { "Off", "Pitch", "Strum", "Pitch & strum" }); // 32
    add ("I2C mode", 0, 3, 0, {}, { "Off", "Pitch", "Strum", "Pitch & strum" });  // 33

    sections = {
        { "MODEL", { 7, 9, 12, 13, 14, 15, 16, 8 } },
        { "TUNE", { 10, 11, 32, 26 } },
        { "MIX", { 18, 17, 19, 20, 21, 22, 23 } },
    };
    defaultL = 12;
}

void Resonator::prepare (double sampleRate, int block)
{
    prepareFx (sampleRate, block);
    reverbBuffer.assign (32768, 0);
    engine = std::make_unique<Zeroed<Engine>>();
    part = &engine->obj.part; synth = &engine->obj.synth; strummer = &engine->obj.strummer;
    strummer->Init (0.01f, rings::kSampleRate / (float) rings::kMaxBlockSize);
    part->Init (reverbBuffer.data());
    synth->Init (reverbBuffer.data());
    adapter.processBlock = [this] (float* const* in, float* const* out) { renderBlock (in, out); };
    adapter.prepare (sampleRate, 48000.0, 1, 2, (int) rings::kMaxBlockSize);
    workIn.assign (kWork, 0.0f); workOdd.assign (kWork, 0.0f); workEven.assign (kWork, 0.0f);
    inLevel = 0;
}

void Resonator::configure()
{
    mode = pv (7);
    part->set_polyphony (pv (9));
    synth->set_polyphony (pv (9));
    if (mode < 6) part->set_model ((rings::ResonatorModel) mode);
    synth->set_fx ((rings::FxType) pv (8));
    tonic = 48.0f + (float) pv (10) + (float) pv (11) * 0.01f;
    patch.structure = (float) pv (12) / 127.0f;
    patch.brightness = (float) pv (13) / 127.0f;
    patch.damping = (float) pv (14) / 127.0f;
    patch.position = (float) pv (15) / 127.0f;
    chord = pv (16);
    noiseGate = pv (17) != 0;
    useInput = pv (26) != 0;
    midiMode = pv (32);
    auto gain = [this] (int n) { float db = (float) pv (n); return db <= -40.0f ? 0.0f : dbToGain (db); };
    inGain = gain (18); outGain = gain (19); dryGain = gain (20);
    balance ((float) pv (21) / 100.0f, oddL, oddR);
    balance ((float) pv (22) / 100.0f, evenL, evenR);
    balance ((float) pv (23) / 100.0f, dryL, dryR);
}

// one 24-sample block at 48 kHz - the same steps as the module's audio callback
void Resonator::renderBlock (float* const* in, float* const* out)
{
    const size_t size = rings::kMaxBlockSize;
    rings::PerformanceState ps {};
    const bool midiPitch = (midiMode & 1) != 0, midiStrum = (midiMode & 2) != 0;
    ps.internal_exciter = ! useInput;
    ps.internal_strum = ! midiStrum;
    ps.internal_note = ! midiPitch;
    ps.tonic = tonic;
    ps.note = midiPitch ? midiNote - 48.0f + bendSemis : 0.0f;
    ps.fm = 0.0f;
    ps.chord = juce::jlimit (0, (int) rings::kNumChords - 1, chord);
    ps.strum = strumPending || strumRequest.exchange (false);
    strumPending = false;
    const bool forced = ps.strum;
    float* x = in[0];
    if (! useInput) std::fill (x, x + size, 0.0f);
    if (mode == 6)
    {
        strummer->Process (nullptr, size, &ps);
        if (forced) ps.strum = true;
        synth->Process (ps, patch, x, out[0], out[1], size);
    }
    else
    {
        if (noiseGate)
        {
            const float threshold = 0.00003f;
            for (size_t i = 0; i < size; ++i)
            {
                float error = x[i] * x[i] - inLevel;
                inLevel += error * (error > 0.0f ? 0.1f : 0.0001f);
                x[i] *= inLevel <= threshold ? (1.0f / threshold) * inLevel : 1.0f;
            }
        }
        strummer->Process (x, size, &ps);
        if (forced) ps.strum = true;
        part->Process (ps, patch, x, out[0], out[1], size);
    }
    if (ps.strum) strumFlash = 6;
}

void Resonator::render (const float* inL, const float* inR, float* const* out, int n)
{
    for (int pos = 0; pos < n; pos += kWork)
    {
        const int len = juce::jmin (kWork, n - pos);
        float pkIn = 0, pkOdd = 0, pkEven = 0;
        for (int i = 0; i < len; ++i)
        {
            workIn[(size_t) i] = 0.5f * (inL[pos + i] + inR[pos + i]) * inGain;
            pkIn = std::max (pkIn, std::abs (workIn[(size_t) i]));
        }
        const float* ins[1] = { workIn.data() };
        float* outs[2] = { workOdd.data(), workEven.data() };
        adapter.process (ins, outs, len);
        for (int i = 0; i < len; ++i)
        {
            const float odd = workOdd[(size_t) i] * outGain, even = workEven[(size_t) i] * outGain;
            const float dl = inL[pos + i] * dryGain, dr = inR[pos + i] * dryGain;
            pkOdd = std::max (pkOdd, std::abs (odd)); pkEven = std::max (pkEven, std::abs (even));
            const float l = softSat (odd * oddL + even * evenL + dl * dryL);
            const float r = softSat (odd * oddR + even * evenR + dr * dryR);
            out[0][pos + i] = l; out[1][pos + i] = r;
            out[2][pos + i] = softSat (0.5f * (odd + even));
            out[3][pos + i] = 0.5f * (l + r);
        }
        meters[0].push (pkIn, len, sr); meters[1].push (pkOdd, len, sr); meters[2].push (pkEven, len, sr);
    }
}

void Resonator::midi (const juce::MidiMessage& m)
{
    const int ch = m.getChannel();
    if (ch <= 0 || channelOffset (ch) != 0) return;
    if (m.isNoteOn())
    {
        if (! acceptsNote (m.getNoteNumber())) return;
        if (midiMode & 1) midiNote = (float) m.getNoteNumber();
        if (midiMode & 2) strumPending = true;
    }
    else if (m.isPitchWheel()) bendSemis = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
}

std::vector<BrowserSpec> Resonator::browsers()
{
    BrowserSpec modes;
    modes.title = "Resonator modes";
    modes.items = [] { juce::StringArray a; for (auto* m : kModes) a.add (m); return a; };
    modes.selected = [this] { return pv (7); };
    modes.choose = [this] (int i) { set (7, i); };
    BrowserSpec second;
    if (pv (7) == 6)
    {
        second.title = "Synth effects";
        second.items = [] { juce::StringArray a; for (auto* f : kSynthFx) a.add (f); return a; };
        second.selected = [this] { return pv (8); };
        second.choose = [this] (int i) { set (8, i); };
    }
    else
    {
        second.title = "Chords";
        second.items = [] { juce::StringArray a; for (auto* c : kChords) a.add (c); return a; };
        second.selected = [this] { return pv (16); };
        second.choose = [this] (int i) { set (16, i); };
    }
    return { modes, second };
}

std::vector<ActionSpec> Resonator::actions()
{
    std::vector<ActionSpec> a;
    a.push_back ({ "Strum", [this] { strumRequest = true; }, [this] { return strumFlash.load() > 0; } });
    a.push_back ({ "Gate", [this] { set (17, pv (17) ? 0 : 1); }, [this] { return pv (17) != 0; } });
    a.push_back ({ "Poly " + juce::String (pv (9)), [this] { set (9, pv (9) % 4 + 1); }, {} });
    a.push_back ({ "Input", [this] { set (26, pv (26) ? 0 : 2); }, [this] { return pv (26) != 0; } });
    a.push_back ({ "Dry", [this] { set (20, pv (20) <= -40 ? 0 : -40); }, [this] { return pv (20) > -40; } });
    return a;
}

ExAlgorithm::Meter Resonator::meter (int i)
{
    static const char* names[] = { "IN", "ODD", "EVEN" };
    i = juce::jlimit (0, 2, i);
    Meter m;
    m.level = juce::jlimit (0.0f, 1.0f, meters[i].value.load() * 1.5f);
    m.on = m.level > 0.01f;
    m.label = names[i];
    return m;
}

void Resonator::drawBottomLine (Oled& o, int)
{
    o.at (0, 3, juce::String (kModes[juce::jlimit (0, 6, pv (7))]).substring (0, 15));
    if (strumFlash.load() > 0) { --strumFlash; o.fillRect (93, 26, 5, 5); }
    for (int i = 0; i < 3; ++i)
    {
        int h = juce::jlimit (0, 7, (int) std::ceil (meters[i].value.load() * 10.0f));
        int x = 106 + i * 7;
        o.hline (x, x + 4, 31);
        if (h > 0) o.fillRect (x, 31 - h, 5, h);
    }
}
} // namespace ex

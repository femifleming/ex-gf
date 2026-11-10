#include "PolyFM.h"

namespace ex
{
extern const uint8_t kPlaitsBank0[4096];
extern const uint8_t kPlaitsBank1[4096];
extern const uint8_t kPlaitsBank2[4096];

// ------------------------------------------------------------------------------------------------ DX7 formats
void dx7Pack (const uint8_t* c, uint8_t* p)
{
    for (int op = 0; op < 6; ++op)
    {
        const uint8_t* s = c + op * 21;
        uint8_t* d = p + op * 17;
        for (int i = 0; i < 11; ++i) d[i] = s[i] & 0x7F; // R1-4 L1-4 BP LD RD
        d[11] = (uint8_t) (((s[12] & 3) << 2) | (s[11] & 3));           // RC<<2 | LC
        d[12] = (uint8_t) (((s[20] & 15) << 3) | (s[13] & 7));          // DT<<3 | RS
        d[13] = (uint8_t) (((s[15] & 7) << 2) | (s[14] & 3));           // KVS<<2 | AMS
        d[14] = s[16] & 0x7F;                                           // OL
        d[15] = (uint8_t) (((s[18] & 31) << 1) | (s[17] & 1));          // FC<<1 | M
        d[16] = s[19] & 0x7F;                                           // FF
    }
    for (int i = 0; i < 8; ++i) p[102 + i] = c[126 + i] & 0x7F;
    p[110] = c[134] & 31;
    p[111] = (uint8_t) (((c[136] & 1) << 3) | (c[135] & 7));
    p[112] = c[137]; p[113] = c[138]; p[114] = c[139]; p[115] = c[140];
    p[116] = (uint8_t) (((c[143] & 7) << 4) | ((c[142] & 7) << 1) | (c[141] & 1));
    p[117] = c[144];
    for (int i = 0; i < 10; ++i) p[118 + i] = c[145 + i] & 0x7F;
}

void dx7Unpack (const uint8_t* p, uint8_t* c)
{
    for (int op = 0; op < 6; ++op)
    {
        const uint8_t* s = p + op * 17;
        uint8_t* d = c + op * 21;
        for (int i = 0; i < 11; ++i) d[i] = s[i] & 0x7F;
        d[11] = s[11] & 3; d[12] = (s[11] >> 2) & 3;
        d[13] = s[12] & 7; d[20] = (s[12] >> 3) & 15;
        d[14] = s[13] & 3; d[15] = (s[13] >> 2) & 7;
        d[16] = s[14] & 0x7F;
        d[17] = s[15] & 1; d[18] = (s[15] >> 1) & 31;
        d[19] = s[16] & 0x7F;
    }
    for (int i = 0; i < 8; ++i) c[126 + i] = p[102 + i] & 0x7F;
    c[134] = p[110] & 31;
    c[135] = p[111] & 7; c[136] = (p[111] >> 3) & 1;
    c[137] = p[112]; c[138] = p[113]; c[139] = p[114]; c[140] = p[115];
    c[141] = p[116] & 1; c[142] = (p[116] >> 1) & 7; c[143] = (p[116] >> 4) & 7;
    c[144] = p[117];
    for (int i = 0; i < 10; ++i) c[145 + i] = p[118 + i] & 0x7F;
}

juce::String dx7Name (const uint8_t* p)
{
    char n[11] {};
    for (int i = 0; i < 10; ++i) { char ch = (char) (p[118 + i] & 0x7F); n[i] = ch >= 32 && ch < 127 ? ch : ' '; }
    return juce::String (n);
}

static uint8_t dxChecksum (const uint8_t* d, int n)
{
    int s = 0; for (int i = 0; i < n; ++i) s += d[i];
    return (uint8_t) ((-s) & 0x7F);
}

// ------------------------------------------------------------------------------------------------ algorithm
PolyFM::PolyFM (ExPlugin& p) : PolyAlgorithm (p, 23, "Poly FM")
{
    numTimbres = kTimbres;
    midiIsGate3 = true;
    addAttenuverters();
    static const juce::StringArray outs { "1/2", "3/4", "1", "2", "3", "4", "1/2+3", "1/2+4" };
    for (int t = 1; t <= 4; ++t)
    {
        juce::String s (t);
        add ("Bank " + s, t == 1 ? 1 : 0, 999, t == 1 ? 1 : 0);
        defs.back().fmt = [] (int) { return juce::String(); };
        add ("Voice " + s, t == 1 ? 1 : 0, 32, t == 1 ? 1 : 0);
        defs.back().fmt = [] (int) { return juce::String(); };
        add ("Output " + s, 0, 7, 0, {}, outs);
        add ("Gain " + s, -40, 24, 0, "dB");
        add ("Pan " + s, -100, 100, 0, "%");
        add ("Brightness " + s, -100, 100, 0, "%");
        add ("Env scale " + s, -100, 100, 0, "%");
    }                                                                         // 7-34
    add ("Coarse tune", -60, 60, 0, "ST");                                     // 35
    add ("Fine tune", -100, 100, 0, "cents");                                  // 36
    add ("Sustain", 0, 1, 0, {}, { "Off", "On" });                             // 37
    add ("Bend range", 0, 48, 2);                                              // 38
    add ("Chord enable", 0, 7, 0); defs.back().fmt = [] (int v) { return chordEnableName (v); };   // 39
    add ("Chord key", -12, 12, 0); defs.back().fmt = [] (int v) { return keyName (v); };          // 40
    add ("Chord scale", 0, 7, 0); defs.back().fmt = [] (int v) { return chordScaleName (v); };    // 41
    add ("Chord shape", 0, 13, 0); defs.back().fmt = [] (int v) { return chordShapeName (v); };   // 42
    add ("Chord inversion", 0, 3, 0);                                                             // 43
    for (int i = 1; i <= 3; ++i) { add ("Arpeggio " + juce::String (i) + " mode", 0, 14, 0); defs.back().fmt = [] (int v) { return arpModeName (v); }; } // 44-46
    for (int i = 1; i <= 3; ++i) add ("Arpeggio " + juce::String (i) + " range", 1, 3, 1);      // 47-49
    add ("Scala/MTS", -16, 99, 0); defs.back().fmt = [this] (int v) { return scalaName (v); };    // 50
    add ("Scala KBM", 0, 99, 0); defs.back().fmt = [this] (int v) { return kbmName (v); };        // 51
    add ("Output mode", 0, 2, 0, {}, { "Spread voice", "Spread voice 2", "Spread pitch" });       // 52
    add ("Output spread", -100, 100, 0, "%");                                                     // 53
    // 54-64: not listed in the manual's table - used here for voice/FX extras
    add ("Max voices", 1, 8, 8);                                                                  // 54
    add ("Unison", 1, 8, 1);                                                                      // 55
    add ("Unison detune", 0, 100, 10, "cents");                                                   // 56
    add ("Chorus mode", 0, 2, 0, {}, { "Off", "I", "II" });                                       // 57
    add ("Delay mode", 0, 2, 0, {}, { "Off", "Stereo", "Ping-pong" });                            // 58
    add ("Delay level", -40, 0, -3, "dB");                                                        // 59
    add ("Delay time", 1, 10000, 500, "ms");                                                      // 60
    defs.back().fmt = [this] (int v) { return delayUsesMidiClock ? "MIDI " + delayDivisionName (v) : juce::String ("ms"); };
    add ("Delay feedback", 0, 100, 50, "%");                                                      // 61
    add ("Gain", -40, 24, 0, "dB");                                                               // 62
    add ("Veloc -> bright", -100, 100, 0, "%");                                                   // 63
    add ("Voice transpose", 0, 1, 1, {}, { "Ignore", "Use" });                                    // 64
    add ("Input mode", 0, 9, 1);                                                                  // 65
    add ("Sustain mode", 0, 1, 0, {}, { "Synth", "Piano" });                                      // 66
    add ("MIDI vel curve", 0, 3, 0, {}, { "Linear", "Soft", "Hard", "Fixed" });                   // 67
    add ("Pitch bend input", 0, 6, 0);                                                            // 68
    add ("Arp reset input", 0, 6, 0);                                                             // 69
    add ("Gate offset", 0, 496, 0);                                                               // 70
    add ("Trigger mode", 0, 2, 0, {}, { "Gated", "Drone", "Triggered" });                         // 71
    add ("Break time", 0, 1000, 0, "ms");                                                         // 72
    add ("Break direction", 0, 2, 0, {}, { "Up", "Down", "Alternate" });                          // 73
    add ("SysEx target", 1, 4, 1);                                                                // 74

    sections = {
        { "T1", { 9, 10, 11, 12, 13 } }, { "T2", { 16, 17, 18, 19, 20 } },
        { "T3", { 23, 24, 25, 26, 27 } }, { "T4", { 30, 31, 32, 33, 34 } },
        { "VOICE", { 54, 55, 56, 62, 63, 64, 52, 53 } },
        { "CHORD", { 39, 40, 41, 42, 43, 46, 49, 72, 73 } },
        { "FX", { 57, 58, 60, 61, 59 } },
        { "KEYS", { 35, 36, 38, 37, 66, 67, 71, 50, 51, 74 } },
    };
    for (auto& e : editedFlag) e = false;
    algorithms.Init();
    engine.startVoice = [this] (const PolyNoteEngine::VoiceStart& s)
    {
        auto& v = voices[s.voice];
        const int t = juce::jlimit (0, kTimbres - 1, s.timbre);
        plaits::fm::Patch np;
        np.Unpack (timbreVoice[t]);
        if (std::memcmp (&np, &v.patch, sizeof (np)) != 0 || ! v.active)
        {
            v.patch = np;
            v.fm.SetPatch (&v.patch);
        }
        v.lfo.Set (v.patch.modulations);
        v.lfo.Reset();
        v.transpose = pv (64) ? (int) v.patch.transpose - 24 : 0;
        v.needRetrig = v.gate;
        v.gate = true; v.active = true; v.quietSamples = 0;
        v.timbre = t; v.pitch = s.pitch; v.vel = s.velocity;
    };
    engine.releaseVoice = [this] (int vi) { voices[vi].gate = false; };
    engine.killVoice = [this] (int vi) { voices[vi].needRetrig = true; };
    engine.voiceSounding = [this] (int vi) { return voices[vi].active; };
    startTimerHz (4);
}

void PolyFM::prepare (double sampleRate, int block)
{
    preparePoly (sampleRate, block);
    for (auto& v : voices)
    {
        v.fm.Init (&algorithms, (float) sampleRate);
        v.lfo.Init ((float) sampleRate);
        v.active = v.gate = false;
        v.params = {};
        v.params.brightness = 0.5f; v.params.envelope_control = 0.5f; v.params.velocity = 1.0f; v.params.note = 60;
    }
    tmp.assign (3 * 64, 0.0f);
    delay.prepare (sampleRate, 10.5f);
    chorus.prepare (sampleRate);
    if (! banks()) scanBanks();
}

void PolyFM::onSelected() { scanBanks(); }

void PolyFM::scanBanks()
{
    auto list = std::make_shared<std::vector<FmBank>>();
    auto dir = SdCard::root().getChildFile ("FMSYX");
    for (auto& f : SdCard::sortedChildren (dir, juce::File::findFiles, "*.syx"))
    {
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb)) continue;
        auto* d = (const uint8_t*) mb.getData();
        size_t n = mb.getSize();
        const uint8_t* payload = nullptr;
        if (n >= 4104 && d[0] == 0xF0 && d[1] == 0x43 && d[3] == 0x09) payload = d + 6;
        else if (n == 4096) payload = d;
        if (! payload) continue;
        FmBank b; b.name = f.getFileName();
        std::memcpy (b.data.data(), payload, 4096);
        list->push_back (b);
    }
    if (list->empty())
    {
        const uint8_t* bi[3] = { kPlaitsBank0, kPlaitsBank1, kPlaitsBank2 };
        for (int i = 0; i < 3; ++i)
        {
            FmBank b; b.name = "<built-in " + juce::String (i + 1) + ">"; b.builtin = true;
            std::memcpy (b.data.data(), bi[i], 4096);
            list->push_back (b);
        }
        statusMsg = "No 'FMSYX' folder found - using built-in banks";
    }
    else statusMsg = {};
    std::atomic_store (&bankList, list);
}

void PolyFM::timerCallback()
{
    for (int t = 0; t < kTimbres; ++t) timbreEdited[t] = editedFlag[t].load();
}

int PolyFM::effBank (int t) const
{
    int b = pv (7 + t * 7);
    if (t > 0 && b == 0) b = pv (7);
    auto l = banks();
    int n = l ? (int) l->size() : 1;
    return juce::jlimit (1, juce::jmax (1, n), b) - 1;
}
int PolyFM::effVoice (int t) const
{
    int v = pv (8 + t * 7);
    if (t > 0 && v == 0) v = pv (8);
    return juce::jlimit (1, 32, v) - 1;
}

void PolyFM::refreshTimbres()
{
    auto l = banks();
    if (! l || l->empty()) return;
    for (int t = 0; t < kTimbres; ++t)
    {
        int b = effBank (t), v = effVoice (t);
        if (b == loadedBank[t] && v == loadedVoice[t] && l == loadedList) continue;
        if (b != loadedBank[t] || v != loadedVoice[t]) editedFlag[t] = false;
        loadedBank[t] = b; loadedVoice[t] = v;
        if (! editedFlag[t]) std::memcpy (timbreVoice[t], (*l)[(size_t) b].data.data() + v * 128, 128);
    }
    loadedList = l;
}

void PolyFM::configure()
{
    refreshTimbres();
    updateSustain();
    engine.maxVoices = pv (54);
    engine.unison = pv (55);
    engine.unisonDetuneCents = (float) pv (56);
    engine.transpose = pv (35);
    engine.fineCents = (float) pv (36);
    engine.sustainMode = pv (66);
    engine.triggerMode = pv (71);
    engine.velCurve = pv (67);
    auto& c = engine.chords;
    c.enable = pv (39); c.key = pv (40); c.scale = pv (41); c.shape = pv (42); c.inversion = pv (43);
    for (int i = 0; i < 3; ++i) { c.arpMode[i] = pv (44 + i); c.arpRange[i] = pv (47 + i); }
    c.breakMs = pv (72); c.breakDir = pv (73);
    updateTuning (50, 51);
    engine.tuning = &tuning;
    if (int r = sendRequest.exchange (0); r != 0 && currentMidiOut)
    {
        if (r == 1) sendVoiceDump (*currentMidiOut, pv (74) - 1);
        else sendBankDump (*currentMidiOut, pv (74) - 1);
    }
}

void PolyFM::renderVoices (float* const* out, int start, int n)
{
    juce::ignoreUnused (start);
    if ((int) tmp.size() < 3 * n) return;
    const int mode = pv (52);
    const float spread = pv (53) / 100.0f;
    for (int vi = 0; vi < PolyNoteEngine::kMaxVoices; ++vi)
    {
        auto& v = voices[vi];
        if (! v.active) { voiceLevel[vi] = 0.0f; continue; }
        const int t = v.timbre, base = 7 + t * 7;
        auto& p = v.params;
        p.sustain = false;
        p.note = v.pitch + (float) v.transpose + bendSemis;
        p.velocity = v.vel;
        p.brightness = juce::jlimit (0.0f, 1.0f, 0.5f + (float) pv (base + 5) / 200.0f + (float) pv (63) / 200.0f * (v.vel - 0.5f) * 2.0f);
        p.envelope_control = juce::jlimit (0.0f, 1.0f, 0.5f + (float) pv (base + 6) / 200.0f);
        v.lfo.Step ((float) n);
        p.pitch_mod = v.lfo.pitch_mod();
        p.amp_mod = v.lfo.amp_mod();
        if (v.needRetrig)
        {
            p.gate = false;
            std::fill (tmp.begin(), tmp.begin() + 3, 0.0f);
            v.fm.Render (p, tmp.data(), 1);
            v.needRetrig = false;
        }
        p.gate = v.gate;
        std::fill (tmp.begin(), tmp.begin() + 3 * n, 0.0f);
        v.fm.Render (p, tmp.data(), (size_t) n);
        const float g = pv (base + 3) <= -40 ? 0.0f : dbToGain ((float) pv (base + 3)) * 0.25f;
        const int o = pv (base + 2);
        float gl, gr;
        panGains (juce::jlimit (-1.0f, 1.0f, (float) pv (base + 4) / 100.0f + spreadPan (mode, vi, v.pitch, spread)), gl, gr);
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            float s = tmp[(size_t) i] * g;
            pk = std::max (pk, std::abs (s));
            switch (o)
            {
                case 0: out[0][i] += s * gl; out[1][i] += s * gr; break;
                case 1: out[2][i] += s * gl; out[3][i] += s * gr; break;
                case 6: out[0][i] += s * gl; out[1][i] += s * gr; out[2][i] += s; break;
                case 7: out[0][i] += s * gl; out[1][i] += s * gr; out[3][i] += s; break;
                default: out[o - 2][i] += s; break;
            }
        }
        v.level = std::max (pk * 4.0f, v.level * 0.97f);
        voiceLevel[vi] = juce::jlimit (0.0f, 1.0f, v.level * 2.0f);
        if (! v.gate)
        {
            v.quietSamples = pk < 1.0e-4f ? v.quietSamples + n : 0;
            if (v.quietSamples > (int) (0.05 * sr)) { v.active = false; voiceLevel[vi] = 0.0f; engine.voiceFinished (vi); }
        }
    }
}

void PolyFM::postProcess (float* const* out, int n)
{
    chorus.process (out[0], out[1], n, pv (57));
    delay.process (out[0], out[1], n, pv (58), (float) pv (59), (float) delayMs (pv (60)), pv (61) / 100.0f);
    const float g = dbToGain ((float) pv (62));
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < n; ++i) out[c][i] = softSat (out[c][i] * g);
}

// ------------------------------------------------------------------------------------------------ SysEx (DX7 editors)
void PolyFM::sendVoiceDump (juce::MidiBuffer& out, int t)
{
    uint8_t vced[155];
    dx7Unpack (timbreVoice[juce::jlimit (0, 3, t)], vced);
    uint8_t msg[163];
    msg[0] = 0xF0; msg[1] = 0x43; msg[2] = (uint8_t) juce::jlimit (0, 15, plugin.txChannel - 1); msg[3] = 0; msg[4] = 0x01; msg[5] = 0x1B;
    std::memcpy (msg + 6, vced, 155);
    msg[161] = dxChecksum (vced, 155); msg[162] = 0xF7;
    out.addEvent (juce::MidiMessage (msg, 163), 0);
}

void PolyFM::sendBankDump (juce::MidiBuffer& out, int t)
{
    auto l = banks();
    if (! l) return;
    std::vector<uint8_t> msg (4104);
    msg[0] = 0xF0; msg[1] = 0x43; msg[2] = (uint8_t) juce::jlimit (0, 15, plugin.txChannel - 1); msg[3] = 0x09; msg[4] = 0x20; msg[5] = 0x00;
    std::memcpy (msg.data() + 6, (*l)[(size_t) effBank (juce::jlimit (0, 3, t))].data.data(), 4096);
    msg[4102] = dxChecksum (msg.data() + 6, 4096); msg[4103] = 0xF7;
    out.addEvent (juce::MidiMessage (msg.data(), 4104), 0);
}

void PolyFM::onOtherMidi (const juce::MidiMessage& m)
{
    if (! m.isSysEx()) return;
    const uint8_t* d = m.getSysExData();
    const int n = m.getSysExDataSize();
    if (n < 4 || d[0] != 0x43) return;
    const int t = juce::jlimit (0, 3, pv (74) - 1);
    const int sub = d[1] >> 4;
    if (sub == 0 && d[2] == 0x00 && n >= 4 + 155)            // single voice dump
    {
        dx7Pack (d + 5, timbreVoice[t]);
        editedFlag[t] = true;
    }
    else if (sub == 0 && d[2] == 0x09 && n >= 5 + 4096)       // bank dump -> replaces the timbre's bank in RAM
    {
        auto l = banks();
        if (! l) return;
        auto copy = std::make_shared<std::vector<FmBank>> (*l);
        auto& b = (*copy)[(size_t) effBank (t)];
        std::memcpy (b.data.data(), d + 5, 4096);
        std::atomic_store (&bankList, copy);
        editedFlag[t] = false;
    }
    else if (sub == 1 && n >= 4)                              // parameter change
    {
        int group = (d[2] >> 2) & 31;
        int param = ((d[2] & 3) << 7) | d[3];
        if (group != 0 || param > 155 || n < 5) return;
        uint8_t vced[156];
        dx7Unpack (timbreVoice[t], vced);
        if (param < 155) vced[param] = d[4];
        dx7Pack (vced, timbreVoice[t]);
        editedFlag[t] = true;
    }
    else if (sub == 2)                                        // dump requests
    {
        if (! currentMidiOut) return;
        if (d[2] == 0x00) sendVoiceDump (*currentMidiOut, t);
        else if (d[2] == 0x09) sendBankDump (*currentMidiOut, t);
    }
}

void PolyFM::writeBuiltinToSd()
{
    auto dir = SdCard::root().getChildFile ("FMSYX");
    dir.createDirectory();
    const uint8_t* bi[3] = { kPlaitsBank0, kPlaitsBank1, kPlaitsBank2 };
    for (int i = 0; i < 3; ++i)
    {
        std::vector<uint8_t> msg (4104);
        msg[0] = 0xF0; msg[1] = 0x43; msg[2] = 0; msg[3] = 0x09; msg[4] = 0x20; msg[5] = 0;
        std::memcpy (msg.data() + 6, bi[i], 4096);
        msg[4102] = dxChecksum (bi[i], 4096); msg[4103] = 0xF7;
        dir.getChildFile ("builtin_" + juce::String (i + 1) + ".syx").replaceWithData (msg.data(), msg.size());
    }
    scanBanks();
}

void PolyFM::addMenuItems (std::vector<MenuItem>& items)
{
    addStandardPolyMenu (items, true, false);
    items.push_back (MenuItem::sub ("Send", {
        MenuItem::act ("Send voice dump", [this] { sendRequest = 1; plugin.message ("Voice dump sent"); }),
        MenuItem::act ("Send bank dump", [this] { sendRequest = 2; plugin.message ("Bank dump sent"); }),
    }));
    items.push_back (MenuItem::act ("Abandon edit", [this] {
        int t = pv (74) - 1; editedFlag[t] = false; loadedBank[t] = -1; plugin.message ("Edit abandoned"); }));
    items.push_back (MenuItem::act ("Write voice", [this] {
        int t = pv (74) - 1;
        auto l = banks(); if (! l) return;
        auto copy = std::make_shared<std::vector<FmBank>> (*l);
        std::memcpy ((*copy)[(size_t) effBank (t)].data.data() + effVoice (t) * 128, timbreVoice[t], 128);
        std::atomic_store (&bankList, copy);
        editedFlag[t] = false;
        plugin.message ("Voice written to RAM"); }));
    items.push_back (MenuItem::act ("Write bank to SD", [this] {
        int t = pv (74) - 1;
        auto l = banks(); if (! l) return;
        auto& b = (*l)[(size_t) effBank (t)];
        std::vector<uint8_t> msg (4104);
        msg[0] = 0xF0; msg[1] = 0x43; msg[2] = 0; msg[3] = 0x09; msg[4] = 0x20; msg[5] = 0;
        std::memcpy (msg.data() + 6, b.data.data(), 4096);
        msg[4102] = dxChecksum (b.data.data(), 4096); msg[4103] = 0xF7;
        auto dir = SdCard::root().getChildFile ("FMSYX"); dir.createDirectory();
        juce::String name = b.builtin ? "builtin_" + juce::String (effBank (t) + 1) + ".syx" : b.name;
        dir.getChildFile (name).replaceWithData (msg.data(), msg.size());
        plugin.message ("Bank written to SD"); }));
    items.push_back (MenuItem::act ("Write built-in to SD", [this] { writeBuiltinToSd(); plugin.message ("Built-in banks written"); }));
}

std::vector<BrowserSpec> PolyFM::browsers()
{
    BrowserSpec t;
    t.title = "Timbre"; t.compact = true;
    t.items = [] { return juce::StringArray { "1", "2", "3", "4" }; };
    t.selected = [this] { return uiTimbre; };
    t.choose = [this] (int i) { uiTimbre = i; };
    BrowserSpec b;
    b.title = "Banks";
    b.items = [this] { juce::StringArray a; if (uiTimbre > 0) a.add ("(same as timbre 1)"); if (auto l = banks()) for (auto& x : *l) a.add (x.name.upToLastOccurrenceOf (".syx", false, true)); return a; };
    b.selected = [this] { int v = pv (7 + uiTimbre * 7); return uiTimbre == 0 ? v - 1 : v; };
    b.choose = [this] (int i) { set (7 + uiTimbre * 7, uiTimbre == 0 ? i + 1 : i); };
    BrowserSpec v;
    v.title = "Voices";
    v.items = [this] {
        juce::StringArray a;
        if (uiTimbre > 0) a.add ("(same as timbre 1)");
        if (auto l = banks(); l && ! l->empty())
        {
            auto& bank = (*l)[(size_t) effBank (uiTimbre)];
            for (int i = 0; i < 32; ++i) a.add (juce::String (i + 1).paddedLeft (' ', 2) + " " + dx7Name (bank.data.data() + i * 128).trim());
        }
        return a; };
    v.selected = [this] { int x = pv (8 + uiTimbre * 7); return uiTimbre == 0 ? x - 1 : x; };
    v.choose = [this] (int i) { set (8 + uiTimbre * 7, uiTimbre == 0 ? i + 1 : i); };
    return { t, b, v };
}

std::vector<ActionSpec> PolyFM::actions()
{
    return {
        { "Notes off", [this] { plugin.allNotesOffRequest = true; }, {} },
        { "Rescan", [this] { scanBanks(); plugin.message ("Banks rescanned"); }, {} },
    };
}

void PolyFM::drawPage (Oled& o, int cur)
{
    PolyAlgorithm::drawPage (o, cur);
    auto l = banks();
    if (cur >= 7 && cur <= 34 && l)
    {
        int t = (cur - 7) / 7, k = (cur - 7) % 7;
        if (k == 0) // bank
        {
            o.fillRect (0, 24, 128, 8, false);
            auto name = timbreEdited[t] ? juce::String() : (*l)[(size_t) effBank (t)].name;
            o.right (21, 3, name.substring (0, 21));
        }
        else if (k == 1) // voice name
        {
            o.right (21, 2, "'" + dx7Name (timbreVoice[t]).trimEnd() + "'");
        }
    }
    bool edited = false;
    for (int t = 0; t < kTimbres; ++t) edited |= timbreEdited[t];
    if (edited)
    {
        o.fillRect (0, 0, 128, 8, true);
        o.centre (0, "VOICE EDITED", true);
    }
    if (statusMsg.isNotEmpty() && cur == 7)
    {
        o.fillRect (0, 24, 128, 8, false);
        o.at (0, 3, "<no FMSYX: built-in>");
    }
}
} // namespace ex

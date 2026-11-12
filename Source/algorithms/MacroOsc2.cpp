#include "MacroOsc2.h"
#include "plaits/resources.h"

namespace ex
{
juce::String MacroOsc2::modelName (int model)
{
    // the 16 classic models first, then the 8 added in Plaits firmware 1.2
    static const char* n[] = {
        "Virtual analog", "Waveshaper", "2-op FM", "Granular formant", "Harmonic", "Wavetable", "Chords", "Speech",
        "Granular cloud", "Filtered noise", "Particle noise", "Inharmonic string", "Modal resonator", "Analog bass drum",
        "Analog snare", "Analog hi-hat", "VA with filter", "Phase distortion", "6-op FM 1", "6-op FM 2", "6-op FM 3",
        "Wave terrain", "String machine", "Chiptune" };
    return n[juce::jlimit (0, 23, model)];
}

juce::String MacroOsc2::midiOptionName (int v)
{
    if (v <= 0) return "Off";
    if (v <= 4) return "Pitch ch+" + juce::String (v - 1);
    if (v <= 8) return "Pitch/trig ch+" + juce::String (v - 5);
    if (v <= 12) return "Trig ch+" + juce::String (v - 9);
    if (v <= 140) return "Trig by " + noteName (v - 13);
    return "Poly ch+" + juce::String (v - 141);
}

// models whose engines shape their own amplitude: velocity becomes the accent
static bool selfEnveloped (int model) { return (model >= 11 && model <= 15) || MacroOsc2::isSixOp (model); }

MacroOsc2::MacroOsc2 (ExPlugin& p) : FxAlgorithm (p, 21, "Macro Oscillator 2")
{
    addAttenuverters();
    for (int o = 0; o < kOscs; ++o)
    {
        const juce::String s = " " + juce::String (o + 1);
        add ("Model" + s, 0, 23, 0); defs.back().fmt = [] (int v) { return modelName (v); };
        add ("Coarse tune" + s, -60, 60, 0, "ST");
        add ("Fine tune" + s, -100, 100, 0, "cents");
        add ("Harmonics" + s, 0, 127, 64);
        defs.back().fmt = [this, o] (int v) {
            const int b = kFirst + kPerOsc * o;
            if (params.empty() || ! isSixOp (pv (b))) return juce::String();
            return patchName (pv (b) - 18, juce::jlimit (0, 31, (int) ((float) v / 127.0f * 32.64f))); };
        add ("Timbre" + s, 0, 127, 64);
        add ("Morph" + s, 0, 127, 64);
        add ("FM" + s, -100, 100, 0, "%");
        add ("Timbre mod" + s, -100, 100, 0, "%");
        add ("Morph mod" + s, -100, 100, 0, "%");
        add ("Low-pass gate" + s, 0, 127, 127);
        add ("Time/decay" + s, 0, 127, 64);
    }
    for (int o = 1; o <= 4; ++o) add ("Output " + juce::String (o) + " gain", -40, 6, 0, "dB");                // 51-54
    for (int o = 1; o <= 4; ++o) { add ("MIDI option " + juce::String (o), 0, 144, 141); defs.back().fmt = [] (int v) { return midiOptionName (v); }; } // 55-58
    for (int o = 1; o <= 4; ++o) add ("Share params " + juce::String (o), 1, 4, 1);                           // 59-62
    add ("Bend range", 0, 48, 2, "ST");                                                                       // 63
    add ("Envelope", 0, 1, 0, {}, { "Trigger", "Gate" });                                                     // 64
    add ("Output mode", 0, 1, 0, {}, { "Mixed", "Individual" });                                              // 65
    for (int o = 1; o <= 4; ++o)                                                                              // 66-69
    {
        add ("Output " + juce::String (o), 0, 7, (o - 1) * 2);
        defs.back().fmt = [] (int v) { return "Osc " + juce::String (v / 2 + 1) + (v % 2 ? " aux" : " main"); };
    }

    for (int o = 0; o < kOscs; ++o)
    {
        Section s; s.name = "OSC " + juce::String (o + 1);
        const int b = kFirst + kPerOsc * o;
        s.params = { b, b + 3, b + 4, b + 5, b + 6, b + 7, b + 8, b + 9, b + 10, b + 1, b + 2 };
        sections.push_back (s);
    }
    sections.push_back ({ "OUT", { 65, 64, 51, 52, 53, 54, 66, 67, 68, 69 } });
    sections.push_back ({ "MIDI", { 55, 59, 56, 60, 57, 61, 58, 62, 63 } });
}

void MacroOsc2::prepare (double sampleRate, int block)
{
    prepareFx (sampleRate, block);
    for (auto& o : osc)
    {
        o.holder = std::make_unique<Zeroed<plaits::Voice>>();
        o.voice = &o.holder->obj;
        o.ram = std::make_unique<char[]> (16384);
        std::memset (o.ram.get(), 0, 16384);
        stmlib::BufferAllocator alloc (o.ram.get(), 16384);
        o.voice->Init (&alloc);
        o.patch = {}; o.mod = {};
        o.gate = o.sustained = o.retrig = false; o.held = -1; o.trigBlocks = 0;
    }
    adapter.processBlock = [this] (float* const*, float* const* out) { renderBlock (out); };
    adapter.prepare (sampleRate, 48000.0, 0, kOscs * 2, (int) plaits::kBlockSize);
    for (auto& w : work) w.assign (kWork, 0.0f);
    banksPending = true;
    if (! std::atomic_load (&pendingBanks)) loadBanks();
}

void MacroOsc2::onSelected() { runAsync ([this] { loadBanks(); }); }

// FMSYX/MACRO2_1.SYX .. MACRO2_3.SYX replace the built-in banks of the three 6-op FM models
void MacroOsc2::loadBanks()
{
    auto set = std::make_shared<BankSet>();
    const uint8_t* builtIn[3] = { plaits::syx_bank_0, plaits::syx_bank_1, plaits::syx_bank_2 };
    for (int b = 0; b < 3; ++b)
    {
        std::memcpy (set->data[(size_t) b].data(), builtIn[b], 4096);
        auto f = SdCard::root().getChildFile ("FMSYX").getChildFile ("MACRO2_" + juce::String (b + 1) + ".SYX");
        juce::MemoryBlock mb;
        if (f.existsAsFile() && f.loadFileAsData (mb) && mb.getSize() >= 4096)
        {
            const size_t skip = mb.getSize() >= 4102 ? 6 : 0; // 4104-byte SysEx dump, or raw voice data
            std::memcpy (set->data[(size_t) b].data(), (const uint8_t*) mb.getData() + skip, 4096);
            set->custom[b] = true;
        }
    }
    std::atomic_store (&names, std::shared_ptr<const BankSet> (set));
    std::atomic_store (&pendingBanks, set);
    banksPending = true;
}

juce::String MacroOsc2::patchName (int bank, int index) const
{
    auto set = std::atomic_load (&names);
    if (! set) return {};
    const uint8_t* v = set->data[(size_t) juce::jlimit (0, 2, bank)].data() + juce::jlimit (0, 31, index) * 128 + 118;
    juce::String s;
    for (int i = 0; i < 10; ++i) s << (char) (v[i] >= 32 && v[i] < 127 ? v[i] : ' ');
    return s.trim();
}

void MacroOsc2::configure()
{
    if (banksPending.exchange (false))
    {
        if (auto nb = std::atomic_exchange (&pendingBanks, std::shared_ptr<BankSet>())) banks = nb;
        for (auto& o : osc)
        {
            if (! o.voice) continue;
            for (int b = 0; b < 3; ++b) o.voice->set_custom_fm_bank (b, banks ? banks->data[(size_t) b].data() : nullptr);
            o.voice->ReloadUserData();
        }
    }
    if (panic.exchange (false)) allNotesOff();
    for (int i = 0; i < kOscs; ++i)
    {
        auto& o = osc[i];
        const int b = base (i);
        o.model = pv (b);
        o.patch.engine = engineFor (o.model);
        o.patch.note = 60.0f + (float) pv (b + 1) + (float) pv (b + 2) * 0.01f;
        o.patch.harmonics = (float) pv (b + 3) / 127.0f;
        o.patch.timbre = (float) pv (b + 4) / 127.0f;
        o.patch.morph = (float) pv (b + 5) / 127.0f;
        o.patch.frequency_modulation_amount = (float) pv (b + 6) / 100.0f;
        o.patch.timbre_modulation_amount = (float) pv (b + 7) / 100.0f;
        o.patch.morph_modulation_amount = (float) pv (b + 8) / 100.0f;
        o.patch.lpg_colour = (float) pv (b + 9) / 127.0f;
        o.patch.decay = (float) pv (b + 10) / 127.0f;
        o.option = pv (55 + i);
    }
    gateMode = pv (64) == 1;
    bendRange = (float) pv (63);
    for (int k = 0; k < 4; ++k) { float db = (float) pv (51 + k); outGain[k] = db <= -40.0f ? 0.0f : dbToGain (db); outAssign[k] = pv (66 + k); }
    individual = pv (65) == 1;
}

// one 12-sample block of all four oscillators at 48 kHz
void MacroOsc2::renderBlock (float* const* out)
{
    for (int i = 0; i < kOscs; ++i)
    {
        auto& o = osc[i];
        const int opt = o.option;
        const bool usesTrig = opt >= 5;
        const bool usesPitch = (opt >= 1 && opt <= 8) || opt >= 141;
        auto& m = o.mod;
        m = {};
        m.note = usesPitch ? o.note - 60.0f + bendSemis : 0.0f;
        m.trigger_patched = usesTrig;
        float gainTarget = 1.0f;
        if (usesTrig)
        {
            // a new note drops the trigger for one block so the rising edge is seen even when playing legato;
            // it then stays high while the key is held, which the 6-op FM models use as their gate
            if (o.retrig) { o.retrig = false; o.trigBlocks = 24; m.trigger = 0.0f; }
            else if (o.trigBlocks > 0) { --o.trigBlocks; m.trigger = 1.0f; }
            else if (o.gate || o.sustained) m.trigger = 1.0f;
            const float v = 1.3f * o.vel / (0.3f + o.vel);
            if (selfEnveloped (o.model)) { m.level_patched = true; m.level = o.vel; }
            else if (gateMode) { m.level_patched = true; m.level = (o.gate || o.sustained) ? o.vel : 0.0f; }
            else gainTarget = v;
        }
        o.gainNow += 0.3f * (gainTarget - o.gainNow);
        o.voice->Render (o.patch, m, frames, plaits::kBlockSize);
        const float g = o.gainNow / 32768.0f;
        for (size_t k = 0; k < plaits::kBlockSize; ++k)
        {
            out[i * 2][k] = (float) frames[k].out * g;
            out[i * 2 + 1][k] = (float) frames[k].aux * g;
        }
    }
}

void MacroOsc2::render (const float*, const float*, float* const* out, int n)
{
    for (int pos = 0; pos < n; pos += kWork)
    {
        const int len = juce::jmin (kWork, n - pos);
        float* w[kOscs * 2];
        for (int c = 0; c < kOscs * 2; ++c) w[c] = work[c].data();
        adapter.process (nullptr, w, len);
        for (int i = 0; i < kOscs; ++i)
        {
            float pk = 0;
            for (int k = 0; k < len; ++k) pk = std::max (pk, std::abs (w[i * 2][k]));
            osc[i].level.push (pk, len, sr);
        }
        if (individual)
        {
            for (int c = 0; c < 4; ++c)
            {
                const float* src = w[juce::jlimit (0, 7, outAssign[c])];
                for (int k = 0; k < len; ++k) out[c][pos + k] = softSat (src[k] * outGain[c]);
            }
        }
        else
        {
            // output 1 = all main outputs, output 2 = all aux outputs; 3 and 4 repeat them
            for (int k = 0; k < len; ++k)
            {
                float a = 0, b = 0;
                for (int i = 0; i < kOscs; ++i) { a += w[i * 2][k]; b += w[i * 2 + 1][k]; }
                a *= 0.7f; b *= 0.7f;
                out[0][pos + k] = softSat (a * outGain[0]); out[1][pos + k] = softSat (b * outGain[1]);
                out[2][pos + k] = softSat (a * outGain[2]); out[3][pos + k] = softSat (b * outGain[3]);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------- MIDI
void MacroOsc2::trigger (Osc& o, int ch, int note, float vel, bool setPitch, bool hold)
{
    if (setPitch) o.note = (float) note;
    o.vel = vel;
    if (hold) { o.held = note; o.heldCh = ch; o.gate = true; o.sustained = false; }
    o.retrig = true;
}

void MacroOsc2::noteOn (int ch, int note, float vel)
{
    if (! acceptsNote (note)) return;
    const int off = channelOffset (ch);
    // round-robin group: the oscillators set to 'Poly' on this channel
    int group[kOscs], gn = 0;
    for (int i = 0; i < kOscs; ++i) if (osc[i].option >= 141 && osc[i].option - 141 == off) group[gn++] = i;
    if (gn > 0)
    {
        int pick = -1;
        for (int k = 0; k < gn && pick < 0; ++k)
        {
            int c = group[(rr + k) % gn];
            if (! osc[c].gate && ! osc[c].sustained) { pick = (rr + k) % gn; }
        }
        if (pick < 0) pick = rr % gn;
        rr = (pick + 1) % gn;
        trigger (osc[group[pick]], ch, note, vel, true, true);
    }
    for (auto& o : osc)
    {
        const int opt = o.option;
        if (opt >= 1 && opt <= 4) { if (opt - 1 == off) o.note = (float) note; }
        else if (opt >= 5 && opt <= 8) { if (opt - 5 == off) trigger (o, ch, note, vel, true, true); }
        else if (opt >= 9 && opt <= 12) { if (opt - 9 == off) trigger (o, ch, note, vel, false, true); }
        else if (opt >= 13 && opt <= 140) { if (opt - 13 == note && off == 0) trigger (o, ch, note, vel, false, true); }
    }
}

void MacroOsc2::noteOff (int ch, int note)
{
    for (auto& o : osc)
        if (o.gate && o.held == note && o.heldCh == ch) { o.gate = false; o.sustained = sustainOn; }
}

void MacroOsc2::allNotesOff()
{
    for (auto& o : osc) { o.gate = false; o.sustained = false; o.trigBlocks = 0; }
}

void MacroOsc2::midi (const juce::MidiMessage& m)
{
    const int ch = m.getChannel();
    if (ch <= 0) return;
    if (m.isNoteOn()) noteOn (ch, m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff()) noteOff (ch, m.getNoteNumber());
    else if (channelOffset (ch) < 0 || channelOffset (ch) > 3) return;
    else if (m.isSustainPedalOn()) sustainOn = true;
    else if (m.isSustainPedalOff()) { sustainOn = false; for (auto& o : osc) o.sustained = false; }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) allNotesOff();
    else if (m.isPitchWheel()) bendSemis = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * bendRange;
}

// ---------------------------------------------------------------------------------------- UI
std::vector<BrowserSpec> MacroOsc2::browsers()
{
    BrowserSpec sel;
    sel.title = "Oscillator"; sel.compact = true;
    sel.items = [] { return juce::StringArray { "1", "2", "3", "4" }; };
    sel.selected = [this] { return selOsc; };
    sel.choose = [this] (int i) { selOsc = juce::jlimit (0, kOscs - 1, i); };
    BrowserSpec models;
    models.title = "Models - osc " + juce::String (selOsc + 1);
    models.items = [] { juce::StringArray a; for (int i = 0; i < 24; ++i) a.add (modelName (i)); return a; };
    models.selected = [this] { return pv (base (selOsc)); };
    models.choose = [this] (int i) { set (base (selOsc), i); plugin.selectParam (base (selOsc)); };
    std::vector<BrowserSpec> out { sel, models };
    const int model = pv (base (selOsc));
    if (isSixOp (model))
    {
        BrowserSpec v;
        v.title = "FM voices - bank " + juce::String (model - 17);
        v.items = [this, model] { juce::StringArray a; for (int i = 0; i < 32; ++i) a.add (juce::String (i + 1).paddedLeft ('0', 2) + " " + patchName (model - 18, i)); return a; };
        v.selected = [this] { return juce::jlimit (0, 31, (int) ((float) pv (base (selOsc) + 3) / 127.0f * 32.64f)); };
        v.choose = [this] (int i) { set (base (selOsc) + 3, juce::roundToInt (127.0f * ((float) i + 0.5f) / 32.64f)); };
        out.push_back (v);
    }
    return out;
}

std::vector<ActionSpec> MacroOsc2::actions()
{
    std::vector<ActionSpec> a;
    a.push_back ({ "Notes off", [this] { panic = true; }, {} });
    a.push_back ({ "Gate env", [this] { set (64, pv (64) ? 0 : 1); }, [this] { return pv (64) == 1; } });
    a.push_back ({ "Split out", [this] { set (65, pv (65) ? 0 : 1); }, [this] { return pv (65) == 1; } });
    a.push_back ({ "Unlink", [this] {
        // give every oscillator its own settings (or link them all to oscillator 1 again)
        const bool linked = pv (60) == 1 && pv (61) == 1 && pv (62) == 1;
        for (int o = 1; o < kOscs; ++o) set (59 + o, linked ? o + 1 : 1); },
        [this] { return ! (pv (60) == 1 && pv (61) == 1 && pv (62) == 1); } });
    return a;
}

juce::String MacroOsc2::statusText()
{
    juce::StringArray shared;
    for (int o = 1; o < kOscs; ++o) if (pv (59 + o) != o + 1) shared.add (juce::String (o + 1) + ">" + juce::String (pv (59 + o)));
    return shared.isEmpty() ? juce::String() : "Shared settings: osc " + shared.joinIntoString (", ");
}

ExAlgorithm::Meter MacroOsc2::meter (int i)
{
    auto& o = osc[juce::jlimit (0, kOscs - 1, i)];
    Meter m;
    m.level = juce::jlimit (0.0f, 1.0f, o.level.value.load() * 1.5f);
    m.on = o.gate || o.sustained || m.level > 0.01f;
    m.label = juce::String (i + 1) + (o.gate ? " " + noteName ((int) o.note) : juce::String());
    return m;
}

void MacroOsc2::addMenuItems (std::vector<MenuItem>& items)
{
    addStandardPolyMenu (items, false, false);
    items.push_back (MenuItem::act ("Write FM banks to SD", [this] {
        // create FMSYX/MACRO2_n.SYX from the built-in banks, without overwriting existing files
        auto dir = SdCard::root().getChildFile ("FMSYX");
        dir.createDirectory();
        const uint8_t* builtIn[3] = { plaits::syx_bank_0, plaits::syx_bank_1, plaits::syx_bank_2 };
        int written = 0;
        for (int b = 0; b < 3; ++b)
        {
            auto f = dir.getChildFile ("MACRO2_" + juce::String (b + 1) + ".SYX");
            if (f.existsAsFile()) continue;
            juce::MemoryBlock mb;
            const uint8_t head[6] = { 0xF0, 0x43, 0x00, 0x09, 0x20, 0x00 };
            mb.append (head, 6);
            mb.append (builtIn[b], 4096);
            int sum = 0; for (int i = 0; i < 4096; ++i) sum += builtIn[b][i];
            const uint8_t tail[2] = { (uint8_t) ((128 - (sum & 127)) & 127), 0xF7 };
            mb.append (tail, 2);
            if (f.replaceWithData (mb.getData(), mb.getSize())) ++written;
        }
        plugin.message ("Wrote " + juce::String (written) + " bank files"); }));
    items.push_back (MenuItem::act ("Reload FM banks", [this] { onSelected(); plugin.message ("Reloaded FM banks"); }));
}

void MacroOsc2::drawPage (Oled& o, int cur)
{
    ExAlgorithm::drawPage (o, cur);
    // bottom line: the model of the oscillator the current parameter belongs to (or the selected one), and four level bars
    int which = selOsc;
    if (cur >= kFirst && cur < kFirst + kPerOsc * kOscs) which = (cur - kFirst) / kPerOsc;
    const int b = kFirst + kPerOsc * which;
    juce::String t = juce::String (which + 1) + ":" + modelName (pv (b));
    if (isSixOp (pv (b))) t = juce::String (which + 1) + ":" + patchName (pv (b) - 18, juce::jlimit (0, 31, (int) ((float) pv (b + 3) / 127.0f * 32.64f)));
    o.at (0, 3, t.substring (0, 16));
    for (int i = 0; i < kOscs; ++i)
    {
        int h = juce::jlimit (0, 7, (int) std::ceil (osc[i].level.value.load() * 10.0f));
        int x = 102 + i * 7;
        o.hline (x, x + 4, 31);
        if (h > 0) o.fillRect (x, 31 - h, 5, h);
    }
}
} // namespace ex

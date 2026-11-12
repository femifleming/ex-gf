#include "ExEditor.h"
#include "PolyAlgorithm.h"

namespace ex
{
// Layout C (mirror symmetric, canvas pixels): side boxes | display | side boxes, then list | tiles | list.
namespace L
{
    constexpr int W = Canvas::W, H = Canvas::H;
    constexpr int sideW = 127, sideL = 3, sideR = W - 3 - sideW, topY = 21, topH = 70;
    constexpr int dispScale = 2, dispW = 128 * dispScale + 6, dispX = (W - dispW) / 2;
    constexpr int statusY = 95;
    constexpr int listW = 175, listL = 3, listR = W - 3 - listW, listY = 106, listH = H - 3 - listY;
    constexpr int midX = listL + listW + 5, midW = listR - 5 - midX;
}

ExEditor::ExEditor (ExPlugin& p) : juce::AudioProcessorEditor (p), plugin (p)
{
    frame = juce::Image (juce::Image::ARGB, Canvas::W, Canvas::H, true);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (Canvas::W, Canvas::H, Canvas::W * 4, Canvas::H * 4);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) Canvas::W / Canvas::H);
    setSize (baseW, baseH);
    draw();
    startTimerHz (30);
}

ExEditor::~ExEditor()
{
    stopTimer();
    for (int n : keysDown) noteOff (n);
    if (mouseNote >= 0) noteOff (mouseNote);
}

void ExEditor::resized()
{
    // whole-pixel scaling only, so every pixel stays perfectly sharp
    pixScale = juce::jmax (1, juce::jmin (getWidth() / Canvas::W, getHeight() / Canvas::H));
    scale = (float) pixScale;
    origin = { (getWidth() - Canvas::W * pixScale) / 2, (getHeight() - Canvas::H * pixScale) / 2 };
}

void ExEditor::paint (juce::Graphics& g)
{
    // plain white pixels on black - no glow, no grid
    juce::PixelARGB lut[16];
    for (int l = 0; l < 16; ++l)
    {
        auto v = (juce::uint8) juce::jlimit (0, 255, (int) std::round (255.0 * l / 15.0));
        lut[l] = juce::PixelARGB (255, v, v, v);
    }
    {
        juce::Image::BitmapData bd (frame, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < Canvas::H; ++y)
        {
            auto* line = (juce::PixelARGB*) bd.getLinePointer (y);
            for (int x = 0; x < Canvas::W; ++x) line[x] = lut[cv.px[y][x]];
        }
    }
    g.fillAll (juce::Colours::black);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImageTransformed (frame, juce::AffineTransform::scale ((float) pixScale).translated ((float) origin.x, (float) origin.y));
}

void ExEditor::timerCallback()
{
    ++tick;
    draw();
    repaint();
}

// ================================================================================================ helpers
juce::String ExEditor::shortName (juce::String s)
{
    static const std::pair<const char*, const char*> rep[] = {
        { "Round robin mode", "Round robin" }, { "Attack time", "Attack" }, { "Decay time", "Decay" }, { "Release time", "Release" },
        { "Sustain level", "Sustain lvl" }, { "Attack shape", "Atk shape" }, { "Decay shape", "Dec shape" },
        { "Delay feedback", "Dly feedback" }, { "Delay level", "Dly level" }, { "Delay time", "Dly time" }, { "Delay mode", "Dly mode" },
        { "Chord inversion", "Inversion" }, { "Output spread", "Out spread" }, { "Output mode", "Out mode" }, { "MIDI vel curve", "Vel curve" },
        { "Ext exciter input", "Ext exciter" }, { "Exciter length", "Exc length" }, { "Low pass filter", "Low pass" }, { "Unison detune", "Uni detune" },
        { "Thru routing", "Thru" }, { "Arpeggio", "Arp" }, { "Veloc -> ", "Vel>" }, { "Pitch -> ", "Pitch>" }, { "Env 2 -> ", "Env2>" },
        { "Env -> ", "Env>" }, { "LFO -> ", "LFO>" }, { "Press -> ", "Press>" }, { "Playback gain", "Play gain" }, { "Playback file", "Play file" },
        { "AS Min velocity", "AS Min vel" }, { "AS Max velocity", "AS Max vel" }, { "AS Preview note", "AS Preview" }, { "Release damp", "Rel damp" },
        { "Voice transpose", "Voice transp" }, { "Pitch bend input", "Bend input" }, { "Break direction", "Break dir" },
        { "Granulator gain", "Grain gain" }, { "Input feedback", "Input fdbk" }, { " enable", " on" }, { " opacity", " opac" },
        { "Out delay ", "Dly " }, { "Pitch quantize", "Pitch quant" }, { "Effects routing", "FX routing" },
        { "Reverb high damp", "Rev hi damp" }, { "Reverb mod speed", "Rev mod spd" }, { "Reverb mod depth", "Rev mod dep" },
        { "Reverb early gain", "Rev early" }, { "Reverb diff gain", "Rev diffuse" }, { "Reverb low pass", "Rev low pass" },
        { "Reverb input mix", "Rev in mix" }, { "Reverb input ", "Rev in " }, { "Reverb routing", "Rev routing" },
        { " frequency", " freq" }, { "Pitch input mix", "Pitch in mix" }, { "Pitch input ", "Pitch in " }, { "Pitch feedback", "Pitch fdbk" },
        { "Pitch shift delay", "Shift delay" }, { "Pitch shift (fine)", "Shift fine" }, { "Delay input mix", "Dly in mix" },
        { "Delay input ", "Dly in " }, { "Delay mono-ize", "Dly mono-ize" }, { "Delay initial pan", "Dly init pan" },
        { "Delay routing", "Dly routing" }, { "Freeze toggle", "Frz toggle" }, { "Freeze target", "Frz target" },
        { "Low-pass gate", "LP gate" }, { "Share params", "Share set" },
    };
    for (auto& p : rep) s = s.replace (p.first, p.second);
    if (s.startsWith ("Voice ") && s.length() > 12) s = "V" + s.substring (6);
    return s;
}

juce::String ExEditor::valueString (ExAlgorithm& a, int n, int v) const
{
    const auto& d = a.def (n);
    auto t = a.valueText (n, v);
    if (! d.labels.isEmpty()) return t;
    if (d.fmt)
    {
        if (t.isEmpty()) return juce::String (v);
        if (t == "ms") return juce::String (v) + "ms";
        return t;
    }
    if (d.unit.isEmpty()) return juce::String (v);
    if (d.unit == "%" || d.unit == "ms" || d.unit == "s" || d.unit == "dB") return juce::String (v) + d.unit;
    if (d.unit == "cents") return juce::String (v) + "ct";
    return juce::String (v) + " " + d.unit;
}

bool ExEditor::isChoice (ExAlgorithm& a, int n) const
{
    const auto& d = a.def (n);
    if (! d.labels.isEmpty()) return true;
    if (d.fmt && d.max - d.min <= 20)
    {
        auto t = d.fmt (d.min);
        return t.isNotEmpty() && t != "ms" && ! t.containsOnly ("-0123456789");
    }
    return false;
}

void ExEditor::noteOn (int note, float vel)
{
    plugin.keyboardState.noteOn (juce::jmax (1, plugin.rxChannel), juce::jlimit (0, 127, note), vel);
}
void ExEditor::noteOff (int note)
{
    plugin.keyboardState.noteOff (juce::jmax (1, plugin.rxChannel), juce::jlimit (0, 127, note), 0.0f);
}

// ================================================================================================ widgets
// ================================================================================================ widgets
int ExEditor::button (int x, int y, int w, const juce::String& label, bool on, std::function<void()> click, const juce::String& tipText, bool rec)
{
    juce::Rectangle<int> r (x, y, w, 11);
    const bool hv = hovered (r);
    if (on)
    {
        bool blink = rec && (tick / 8) % 2 == 0;
        cv.fill (x, y, w, 11, blink ? 9 : 15);
        cv.text (x + (w - Canvas::width (label)) / 2, y + 2, label, 0, (w - 2) / 6);
    }
    else
    {
        cv.rect (x, y, w, 11, hv ? 12 : 5);
        int tx = x + (w - Canvas::width (label)) / 2;
        if (rec) { cv.fill (x + 3, y + 4, 3, 3, 12); tx += 2; }
        cv.text (tx, y + 2, label, hv ? 15 : 11, (w - 2) / 6);
    }
    auto& h = addHit (r);
    h.click = std::move (click);
    h.tip = tipText;
    return x + w;
}

void ExEditor::topBar()
{
    const int bw = 40;
    const char* left[] = { "SAVE", "LOAD", "SD" };
    const char* right[] = { "MIDI", "PANIC", "MENU" };
    std::function<void()> lf[] = { [this] { plugin.savePresetToSd(); }, [this] { presetsMenu(); }, [this] { sdMenu(); } };
    std::function<void()> rf[] = { [this] { midiMenu(); }, [this] { plugin.allNotesOffRequest = true; plugin.message ("All notes off"); }, [this] { fullMenu(); } };
    const char* lt[] = { "Save the current settings as a preset on the SD card", "Load a preset", "SD card folder" };
    const char* rt[] = { "MIDI channels", "All notes off", "All module menus and options" };
    for (int i = 0; i < 3; ++i)
    {
        button (3 + i * (bw + 3), 2, bw, left[i], false, lf[i], lt[i]);
        button (Canvas::W - 3 - bw - (2 - i) * (bw + 3), 2, bw, right[i], false, rf[i], rt[i]);
    }
    const int n = plugin.numAlgorithms();
    if (n > 1)
    {
        // algorithm tabs, centred
        int tw = 0;
        juce::StringArray labels;
        for (int i = 0; i < n; ++i) { labels.add (juce::String (plugin.algAt (i).number) + " " + plugin.algAt (i).name.toUpperCase()); tw += Canvas::width (labels[i]) + 10 + (i ? 3 : 0); }
        int x = (Canvas::W - tw) / 2;
        for (int i = 0; i < n; ++i)
            x = button (x, 2, Canvas::width (labels[i]) + 10, labels[i], i == plugin.currentAlgorithmIndex(), [this, i] { plugin.selectAlgorithm (i); }, "Switch algorithm") + 3;
    }
    else
    {
        juce::String t = "EX  " + plugin.identity.product.fromFirstOccurrenceOf ("EX ", false, false).toUpperCase();
        cv.text ((Canvas::W - Canvas::width (t)) / 2, 4, t, 15);
    }
    cv.dots (0, Canvas::W - 1, 16, 6);
}

void ExEditor::listBox (int x, int y, int w, int h, BrowserSpec& spec, int& scroll)
{
    auto items = spec.items();
    cv.fill (x, y, w, 9, 13);
    cv.text (x + 2, y + 1, spec.title.toUpperCase(), 0, (w - 20) / 6);
    if (! spec.compact)
    {
        juce::String cnt (items.size());
        cv.text (x + w - 1 - Canvas::width (cnt), y + 1, cnt, 0);
    }
    if (spec.compact)
    {
        int n = items.size(), bw = n > 0 ? (w - (n - 1) * 2) / n : 0;
        int sel = spec.selected();
        for (int i = 0; i < n; ++i)
            button (x + i * (bw + 2), y + 11, bw, items[i], i == sel, [choose = spec.choose, i] { choose (i); }, spec.title + " " + items[i]);
        return;
    }
    const int rowH = 9, top = y + 11, rows = (h - 12) / rowH;
    const int sel = spec.selected();
    const int maxScroll = juce::jmax (0, items.size() - rows);
    scroll = juce::jlimit (0, maxScroll, scroll);
    cv.rect (x, top - 1, w, rows * rowH + 2, 3);
    auto& area = addHit ({ x, top, w, rows * rowH });
    area.wheel = [&scroll] (int d) { scroll -= d; };
    for (int i = 0; i < rows && scroll + i < items.size(); ++i)
    {
        int idx = scroll + i, ry = top + i * rowH;
        juce::Rectangle<int> rr (x + 1, ry, w - 4, rowH);
        bool hv = hovered (rr);
        if (idx == sel) cv.fill (x + 1, ry, w - 4, rowH, 15);
        else if (hv) cv.fill (x + 1, ry, w - 4, rowH, 2);
        cv.text (x + 3, ry + 1, items[idx], idx == sel ? 0 : (hv ? 15 : 10), (w - 8) / 6);
        auto& hit = addHit (rr);
        hit.click = [choose = spec.choose, idx] { choose (idx); };
        hit.tip = items[idx];
        hit.wheel = [&scroll] (int d) { scroll -= d; };
    }
    if (maxScroll > 0)
    {
        int bx = x + w - 3, bh = rows * rowH;
        for (int yy = top; yy < top + bh; yy += 2) cv.set (bx + 1, yy, 4);
        int th = juce::jmax (4, bh * rows / items.size());
        int ty = top + (bh - th) * scroll / maxScroll;
        cv.fill (bx, ty, 2, th, 13);
    }
}

void ExEditor::sideBoxes()
{
    auto specs = plugin.alg().browsers();
    // ---- left box: timbre selector (if any) + voice activity
    int x = L::sideL, y = L::topY, w = L::sideW;
    int yy = y;
    bool hasTimbre = false;
    for (auto& s : specs)
        if (s.compact)
        {
            hasTimbre = true;
            cv.fill (x, yy, w, 9, 15);
            cv.text (x + 2, yy + 1, s.title.toUpperCase(), 0);
            auto items = s.items();
            int n = items.size(), bw = n > 0 ? (w - (n - 1) * 3) / n : 0, sel = s.selected();
            for (int i = 0; i < n; ++i)
                button (x + i * (bw + 3), yy + 11, bw, items[i], i == sel, [choose = s.choose, i] { choose (i); }, s.title + " " + items[i]);
            yy += 26;
            break;
        }
    cv.fill (x, yy, w, 9, 15);
    const int nMeters = plugin.alg().numMeters();
    cv.text (x + 2, yy + 1, dynamic_cast<PolyAlgorithm*> (&plugin.alg()) != nullptr ? juce::String ("VOICES")
                             : nMeters > 0 ? plugin.alg().metersTitle().toUpperCase() : juce::String ("STATUS"), 0);
    cv.rect (x, yy + 9, w, y + L::topH - yy - 9, 5);
    yy += 11;
    if (auto* pa = dynamic_cast<PolyAlgorithm*> (&plugin.alg()))
    {
        const int nv = PolyNoteEngine::kMaxVoices;
        const int rows = hasTimbre ? 2 : 4, perRow = nv / rows, gap = perRow > 4 ? 2 : 3, cellW = (w - 4 + gap) / perRow - gap;
        const int rowH = (y + L::topH - 2 - yy) / rows;
        const bool names = cellW >= 3 * 6 + 3; // room for "C#4"; otherwise the MIDI note number
        for (int v = 0; v < nv; ++v)
        {
            int cx = x + 2 + (v % perRow) * (cellW + gap), cy = yy + (v / perRow) * rowH;
            int note = pa->engine.voiceNote[v];
            float lvl = pa->voiceLevel[v].load();
            bool on = note >= 0 && (lvl > 0.0005f || pa->engine.voiceGate[v]);
            cv.rect (cx, cy, cellW, rowH - 2, on ? 10 : 3);
            if (on)
            {
                int fw = juce::jlimit (0, cellW - 2, (int) std::round (lvl * (float) (cellW - 2)));
                cv.fill (cx + 1, cy + 1, fw, rowH - 4, 6);
                if (names) cv.text (cx + 2, cy + (rowH - 2 - 7) / 2, noteName (note), 15, (cellW - 3) / 6);
                else cv.text (cx + 1, cy + (rowH - 2 - 7) / 2, juce::String (note).paddedLeft ('0', 2).substring (0, 2), 15);
            }
        }
    }
    else if (nMeters > 0)
    {
        const int rows = hasTimbre ? 1 : (nMeters > 4 ? 2 : 1), perRow = (nMeters + rows - 1) / rows, gap = 3;
        const int cellW = (w - 4 + gap) / perRow - gap;
        const int rowH = juce::jmin (24, (y + L::topH - 2 - yy) / rows);
        for (int v = 0; v < nMeters; ++v)
        {
            auto m = plugin.alg().meter (v);
            int cx = x + 2 + (v % perRow) * (cellW + gap), cy = yy + (v / perRow) * rowH;
            cv.rect (cx, cy, cellW, rowH - 2, m.on ? 10 : 3);
            int fw = juce::jlimit (0, cellW - 2, (int) std::round (juce::jlimit (0.0f, 1.0f, m.level) * (float) (cellW - 2)));
            if (fw > 0) cv.fill (cx + 1, cy + 1, fw, rowH - 4, 6);
            cv.text (cx + 2, cy + (rowH - 2 - 7) / 2, m.label, m.on ? 15 : 7, (cellW - 3) / 6);
        }
        juce::String st = plugin.alg().statusText();
        int ly = yy + rows * rowH + 2;
        if (st.isNotEmpty() && ly < y + L::topH - 8) cv.text (x + 3, ly, st, 10, (w - 6) / 6);
    }
    else
    {
        juce::String st = plugin.alg().statusText();
        if (st.isEmpty()) st = "Ready";
        juce::StringArray words = juce::StringArray::fromTokens (st, " ", "");
        juce::String line; int ly = yy + 1;
        for (auto& wd : words)
        {
            if (Canvas::width (line + " " + wd) > w - 4 && line.isNotEmpty()) { cv.text (x + 2, ly, line, 12); ly += 9; line = {}; }
            line = line.isEmpty() ? wd : line + " " + wd;
        }
        if (line.isNotEmpty() && ly < y + L::topH - 7) cv.text (x + 2, ly, line, 12);
    }

    // ---- right box: actions
    x = L::sideR;
    cv.fill (x, y, w, 9, 15);
    cv.text (x + 2, y + 1, "ACTIONS", 0);
    cv.rect (x, y + 9, w, L::topH - 9, 5);
    auto acts = plugin.alg().actions();
    const int cols = 2, bw = (w - 4 - 3) / cols;
    for (int i = 0; i < (int) acts.size() && i < 8; ++i)
    {
        auto& a = acts[(size_t) i];
        bool lit = a.lit ? a.lit() : false;
        button (x + 2 + (i % cols) * (bw + 3), y + 11 + (i / cols) * 14, bw, a.name.toUpperCase(), lit, [run = a.run] { run(); }, a.name, a.record);
    }
}

void ExEditor::lists()
{
    auto specs = plugin.alg().browsers();
    static thread_local int dummy = 0; juce::ignoreUnused (dummy);
    std::vector<BrowserSpec> ls;
    for (auto& s : specs) if (! s.compact) ls.push_back (s);
    const int a = juce::jmin (7, plugin.currentAlgorithmIndex());
    if (ls.size() > 0) listBox (L::listL, L::listY, L::listW, L::listH, ls[0], listScroll[a][0]);
    if (ls.size() > 1) listBox (L::listR, L::listY, L::listW, L::listH, ls[1], listScroll[a][1]);
    else if (ls.size() == 1)
    {
        // nothing to mirror: show the selected parameter's details
        int cur = plugin.curParam[plugin.currentAlgorithmIndex()];
        auto& al = plugin.alg();
        int x = L::listR, y = L::listY, w = L::listW;
        cv.fill (x, y, w, 9, 15);
        cv.text (x + 2, y + 1, "PARAMETER", 0);
        cv.rect (x, y + 9, w, L::listH - 9, 5);
        if (cur >= 1 && cur <= al.numParams())
        {
            const auto& d = al.def (cur);
            cv.text (x + 4, y + 14, juce::String (cur) + ": " + d.name, 15, (w - 8) / 6);
            cv.text (x + 4, y + 26, "Value  " + valueString (al, cur, al.pv (cur)), 12, (w - 8) / 6);
            cv.text (x + 4, y + 36, "Range  " + juce::String (d.min) + " .. " + juce::String (d.max), 9, (w - 8) / 6);
            cv.text (x + 4, y + 46, "Default " + valueString (al, cur, d.def), 9, (w - 8) / 6);
        }
    }
}

void ExEditor::displayArea (int x, int y)
{
    plugin.render (oled);
    const int s = L::dispScale, w = Oled::W * s + 6, hh = Oled::H * s + 6;
    cv.rect (x, y, w, hh, 15);
    for (int j = 0; j < Oled::H; ++j)
        for (int i = 0; i < Oled::W; ++i)
            if (oled.px[j][i]) cv.fill (x + 3 + i * s, y + 3 + j * s, s, s, 15);
    auto& h = addHit ({ x, y, w, hh });
    h.wheel = [this] (int d) { if (juce::ModifierKeys::currentModifiers.isShiftDown()) plugin.turnV (d); else plugin.turnP (-d); };
    h.click = [this] { grabKeyboardFocus(); };
    h.tip = "Wheel: parameter / shift+wheel: value / arrow keys";
}

void ExEditor::tile (int x, int y, int w, int h, ExAlgorithm& a, int n)
{
    const auto& d = a.def (n);
    const int v = a.pv (n);
    const bool sel = plugin.curParam[plugin.currentAlgorithmIndex()] == n;
    juce::Rectangle<int> r (x, y, w, h);
    const bool hv = hovered (r);
    cv.rect (x, y, w, h, sel ? 15 : hv ? 8 : 3);
    const bool choice = isChoice (a, n);
    cv.text (x + 4, y + 4, shortName (d.name), sel ? 13 : 7, (w - (choice ? 12 : 6)) / 6);
    if (choice) { cv.hline (x + w - 8, x + w - 4, y + 4, 9); cv.hline (x + w - 7, x + w - 5, y + 5, 9); cv.set (x + w - 6, y + 6, 9); }
    cv.text (x + 4, y + 14, valueString (a, n, v), 15, (w - 6) / 6);
    int bx = x + 4, bw = w - 8, by = y + h - 5;
    cv.dots (bx, bx + bw - 1, by, 3);
    if (d.max > d.min)
    {
        auto frac = [&] (int q) { return (float) (q - d.min) / (float) (d.max - d.min); };
        float z = d.min < 0 && d.max > 0 ? frac (0) : 0.0f, f = frac (v);
        int a0 = bx + (int) std::round (juce::jmin (z, f) * (float) (bw - 1)), a1 = bx + (int) std::round (juce::jmax (z, f) * (float) (bw - 1));
        cv.hline (a0, juce::jmax (a0 + 1, a1), by, 13);
        cv.hline (a0, juce::jmax (a0 + 1, a1), by + 1, 13);
    }
    auto& hit = addHit (r);
    ExAlgorithm* ap = &a;
    auto start = std::make_shared<int> (v);
    hit.press = [this, n, ap, start] (juce::Point<int>) { plugin.selectParam (n); *start = ap->pv (n); };
    hit.drag = [ap, n, start] (float dy, bool shift) {
        const auto& dd = ap->def (n);
        const int range = dd.max - dd.min;
        float steps = shift ? 1.0f / 8.0f : range <= 40 ? 1.0f / 6.0f : (float) range / 260.0f;
        ap->set (n, *start + juce::roundToInt (dy * steps));
    };
    hit.click = [this, ap, n, choice] { if (choice) choicesFor (*ap, n); };
    hit.dbl = [ap, n, choice] { if (! choice) ap->set (n, ap->def (n).def); };
    hit.wheel = [this, ap, n] (int dd) { plugin.selectParam (n); ap->set (n, ap->pv (n) + dd * (juce::ModifierKeys::currentModifiers.isShiftDown() ? 10 : 1)); };
    hit.tip = juce::String (n) + ":" + d.name + (choice ? " - click for choices" : " - drag/wheel, dbl-click resets");
}

void ExEditor::tabsAndTiles (int x, int y, int w, int h)
{
    auto& a = plugin.alg();
    const int ai = juce::jmin (7, plugin.currentAlgorithmIndex());
    if (a.sections.empty()) return;
    int& s = section[ai];
    s = juce::jlimit (0, (int) a.sections.size() - 1, s);
    const int n = (int) a.sections.size();
    const int perRow = n <= 2 ? n : 4, rowsT = (n + perRow - 1) / perRow;
    const int tw = (w - (perRow - 1) * 3) / perRow;
    ExAlgorithm* ap = &a;
    for (int i = 0; i < n; ++i)
    {
        int row = i / perRow, col = i % perRow;
        int inRow = juce::jmin (perRow, n - row * perRow);
        int rowX = x + (w - (inRow * tw + (inRow - 1) * 3)) / 2;
        button (rowX + col * (tw + 3), y + row * 14, tw, a.sections[(size_t) i].name, i == s, [this, i, ai, ap] {
            section[ai] = i; gridScroll[ai] = 0;
            if (! ap->sections[(size_t) i].params.empty()) plugin.selectParam (ap->sections[(size_t) i].params.front()); });
    }
    const int top = y + rowsT * 14 + 2;
    auto& params = a.sections[(size_t) s].params;
    const int cols = 2, gap = 3, cw = (w - gap) / cols, th = 29;
    const int totalRows = ((int) params.size() + cols - 1) / cols;
    int avail = y + h - top;
    int rowsVisible = (avail + gap) / (th + gap);
    int& gs = gridScroll[ai];
    if (totalRows > rowsVisible)
    {
        rowsVisible = (avail - 14 + gap) / (th + gap);
        int by = y + h - 11;
        button (x + w / 2 - 33, by, 30, "^", false, [&gs] { --gs; }, "Scroll parameters up");
        button (x + w / 2 + 3, by, 30, "v", false, [&gs] { ++gs; }, "Scroll parameters down");
    }
    gs = juce::jlimit (0, juce::jmax (0, totalRows - rowsVisible), gs);
    auto& gridHit = addHit ({ x, top, w, rowsVisible * (th + gap) });
    gridHit.wheel = [&gs] (int d) { gs -= d; };
    for (int i = 0; i < (int) params.size(); ++i)
    {
        int row = i / cols - gs;
        if (row < 0 || row >= rowsVisible) continue;
        int pn = params[(size_t) i];
        if (pn < 1 || pn > a.numParams()) continue;
        tile (x + (i % cols) * (cw + gap), top + row * (th + gap), cw, th, a, pn);
    }
}

void ExEditor::overlayArea()
{
    if (overlay.empty()) return;
    for (auto& row : cv.px) for (auto& p : row) p = (uint8_t) (p / 4);
    auto& block = addHit ({ 0, 0, Canvas::W, Canvas::H });
    block.click = [this] { overlay.clear(); };
    block.wheel = [] (int) {};
    auto& pg = overlay.back();
    const int w = 280, rowH = 10, maxRows = 28;
    const bool back = overlay.size() > 1;
    const int n = (int) pg.items.size() + (back ? 1 : 0);
    const int rows = juce::jmin (maxRows, n);
    const int h = 12 + rows * rowH + 3;
    const int x = (Canvas::W - w) / 2, y = juce::jmax (15, (Canvas::H - h) / 2);
    cv.fill (x, y, w, h, 0);
    cv.rect (x, y, w, h, 15);
    cv.fill (x, y, w, 10, 15);
    cv.text (x + 3, y + 2, pg.title.toUpperCase(), 0, (w - 16) / 6);
    cv.text (x + w - 8, y + 2, "x", 0);
    auto& inside = addHit ({ x, y + 10, w, h - 10 });
    pg.scroll = juce::jlimit (0, juce::jmax (0, n - rows), pg.scroll);
    auto wheelFn = [this] (int d) { if (! overlay.empty()) overlay.back().scroll -= d; };
    inside.wheel = wheelFn;
    auto& closeHit = addHit ({ x + w - 10, y, 10, 10 });
    closeHit.click = [this] { overlay.clear(); };
    for (int i = 0; i < rows; ++i)
    {
        int idx = pg.scroll + i, ry = y + 12 + i * rowH;
        juce::Rectangle<int> rr (x + 2, ry, w - 6, rowH);
        bool hv = hovered (rr);
        if (back && idx == 0)
        {
            if (hv) cv.fill (rr.getX(), ry, rr.getWidth(), rowH, 3);
            cv.text (x + 6, ry + 1, "< BACK", hv ? 15 : 9);
            auto& bh = addHit (rr);
            bh.click = [this] { overlay.pop_back(); };
            bh.wheel = wheelFn;
            continue;
        }
        auto& it = pg.items[(size_t) (idx - (back ? 1 : 0))];
        if (it.ticked) cv.fill (rr.getX(), ry, rr.getWidth(), rowH, 13);
        else if (hv && it.enabled) cv.fill (rr.getX(), ry, rr.getWidth(), rowH, 3);
        int lvl = ! it.enabled ? 4 : it.ticked ? 0 : hv ? 15 : 11;
        cv.text (x + 6, ry + 1, it.label, lvl, (w - 22) / 6);
        if (it.sub) cv.text (x + w - 12, ry + 1, ">", lvl);
        auto& ih = addHit (rr);
        ih.wheel = wheelFn;
        if (it.enabled && it.action)
        {
            auto act = it.action;
            bool sub = it.sub;
            ih.click = [this, act, sub] { if (! sub) overlay.clear(); act(); };
        }
        else ih.click = [] {};
    }
    if (n > rows)
    {
        int bx = x + w - 4, top = y + 12, bh2 = rows * rowH;
        int th = juce::jmax (4, bh2 * rows / n), ty = top + (bh2 - th) * pg.scroll / juce::jmax (1, n - rows);
        cv.fill (bx, ty, 2, th, 13);
    }
}

void ExEditor::draw()
{
    if (plugin.currentAlgorithmIndex() != shownAlg)
    {
        shownAlg = plugin.currentAlgorithmIndex();
        overlay.clear();
    }
    cv.clear();
    hits.clear();
    topBar();
    sideBoxes();
    displayArea (L::dispX, L::topY);
    lists();
    tabsAndTiles (L::midX, L::listY, L::midW, L::listH);
    // status line (centred) with mirrored MIDI in/out lights
    juce::String status;
    if (overlay.empty())
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (it->r.contains (hover)) { status = it->tip; break; }
    if (status.isEmpty()) status = plugin.alg().statusText();
    if (status.isEmpty())
        status = "SD: " + SdCard::root().getFullPathName().replace (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName(), "~");
    const int maxChars = (Canvas::W - 80) / 6;
    if (status.length() > maxChars) status = status.substring (0, maxChars - 2) + "..";
    cv.text ((Canvas::W - Canvas::width (status)) / 2, L::statusY, status, 9);
    bool in = plugin.midiInFlash.load() > 0, out = plugin.midiOutFlash.load() > 0;
    cv.fill (3, L::statusY + 2, 3, 3, in ? 15 : 4);   cv.text (9, L::statusY, "IN", in ? 15 : 5);
    cv.text (Canvas::W - 9 - Canvas::width ("OUT"), L::statusY, "OUT", out ? 15 : 5); cv.fill (Canvas::W - 6, L::statusY + 2, 3, 3, out ? 15 : 4);
    overlayArea();
    if (dropHover)
    {
        for (auto& row : cv.px) for (auto& p : row) p = (uint8_t) (p / 4);
        const juce::String t1 = "DROP TO IMPORT", t2 = plugin.alg().importHint();
        int bw = 260, bh = 40, bx = (Canvas::W - bw) / 2, by = (Canvas::H - bh) / 2;
        cv.fill (bx, by, bw, bh, 0);
        for (int k = 0; k < 2; ++k) cv.rect (bx + k * 2, by + k * 2, bw - k * 4, bh - k * 4, 15);
        cv.text ((Canvas::W - Canvas::width (t1)) / 2, by + 10, t1, 15);
        cv.text ((Canvas::W - Canvas::width (t2)) / 2, by + 22, t2, 8);
    }
}

bool ExEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (! plugin.alg().acceptsFileImport()) return false;
    for (auto& f : files) if (juce::File (f).isDirectory() || juce::File (f).hasFileExtension ("wav")) return true;
    return false;
}

void ExEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    plugin.alg().importFiles (files);
}

// ================================================================================================ mouse / keys
void ExEditor::mouseMove (const juce::MouseEvent& e) { hover = toCanvas (e.position); }

void ExEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    auto p = toCanvas (e.position);
    hover = downPos = p;
    hasActive = false; dragged = false;
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        if (it->r.contains (p) && (it->click || it->drag || it->press || it->wheel || it->dbl))
        {
            active = *it; hasActive = true;
            if (active.press) active.press (p);
            break;
        }
}

void ExEditor::mouseDrag (const juce::MouseEvent& e)
{
    hover = toCanvas (e.position);
    if (! hasActive) return;
    if (active.move) active.move (hover);
    float dy = (float) -e.getDistanceFromDragStartY();
    if (std::abs (dy) > 3.0f) dragged = true;
    if (active.drag && dragged) active.drag (dy, e.mods.isShiftDown());
}

void ExEditor::mouseUp (const juce::MouseEvent&)
{
    if (! hasActive) return;
    hasActive = false;
    if (active.release) active.release (hover);
    if (! dragged && active.click) active.click();
}

void ExEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto p = toCanvas (e.position);
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        if (it->r.contains (p)) { if (it->dbl) it->dbl(); return; }
}

void ExEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    static float acc = 0;
    acc += (w.deltaY != 0 ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f) * 8.0f;
    int d = (int) acc;
    if (d == 0) return;
    acc -= (float) d;
    auto p = toCanvas (e.position);
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        if (it->r.contains (p) && it->wheel) { it->wheel (d); return; }
}

static const char* kKeyMap = "awsedftgyhujkolp;";

bool ExEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::escapeKey))
    {
        if (! overlay.empty()) { overlay.pop_back(); return true; }
        plugin.pushV(); return true;
    }
    int mul = k.getModifiers().isShiftDown() ? 10 : 1;
    if (k.isKeyCode (juce::KeyPress::upKey)) { plugin.turnP (-1); return true; }
    if (k.isKeyCode (juce::KeyPress::downKey)) { plugin.turnP (1); return true; }
    if (k.isKeyCode (juce::KeyPress::rightKey)) { plugin.turnV (mul); return true; }
    if (k.isKeyCode (juce::KeyPress::leftKey)) { plugin.turnV (-mul); return true; }
    if (k.isKeyCode (juce::KeyPress::returnKey)) { plugin.pushP(); return true; }
    auto c = k.getTextCharacter();
    if (c == 'z') { kbOctave = juce::jmax (0, kbOctave - 1); return true; }
    if (c == 'x') { kbOctave = juce::jmin (8, kbOctave + 1); return true; }
    return c != 0 && juce::String (kKeyMap).containsChar (c);
}

bool ExEditor::keyStateChanged (bool)
{
    bool used = false;
    for (int i = 0; kKeyMap[i]; ++i)
    {
        int note = (kbOctave + 1) * 12 + i;
        bool down = juce::KeyPress::isKeyCurrentlyDown (kKeyMap[i]);
        if (down && ! keysDown.count (note)) { keysDown.insert (note); noteOn (note, 0.8f); used = true; }
        else if (! down && keysDown.count (note)) { keysDown.erase (note); noteOff (note); used = true; }
    }
    return used;
}

// ================================================================================================ overlays
void ExEditor::choicesFor (ExAlgorithm& a, int n)
{
    OverlayPage pg;
    pg.title = a.def (n).name;
    const auto& d = a.def (n);
    ExAlgorithm* ap = &a;
    for (int v = d.min; v <= d.max; ++v)
        pg.items.push_back ({ valueString (a, n, v), v == a.pv (n), true, false, [ap, n, v] { ap->set (n, v); } });
    pg.scroll = juce::jmax (0, a.pv (n) - d.min - 8);
    openOverlay (std::move (pg));
}

void ExEditor::pushMenuPage (MenuItem& item)
{
    OverlayPage pg;
    pg.title = item.label.isEmpty() ? juce::String ("Menu") : item.label;
    for (auto& c : item.children)
    {
        MenuItem* cp = &c;
        if (c.label == "Presets" || c.label == "Algorithms") continue;
        if (! c.children.empty())
            pg.items.push_back ({ c.label, false, true, true, [this, cp] { pushMenuPage (*cp); } });
        else if (c.isValue && c.get && c.set && ! c.action)
        {
            juce::String cur = c.fmt ? c.fmt (c.get()) : juce::String (c.get());
            pg.items.push_back ({ c.label + ": " + cur, false, true, true, [this, cp] {
                OverlayPage vp;
                vp.title = cp->label;
                int cur2 = cp->get();
                for (int v = cp->vmin; v <= cp->vmax && v - cp->vmin < 256; ++v)
                {
                    auto set = cp->set;
                    vp.items.push_back ({ cp->fmt ? cp->fmt (v) : juce::String (v), v == cur2, true, false, [set, v] { set (v); } });
                }
                vp.scroll = juce::jmax (0, cur2 - cp->vmin - 8);
                overlay.push_back (std::move (vp)); } });
        }
        else if (c.action && ! c.isValue)
        {
            auto act = c.action;
            pg.items.push_back ({ c.label, false, true, false, [act] { act(); } });
        }
    }
    overlay.push_back (std::move (pg));
}

void ExEditor::fullMenu()
{
    overlay.clear();
    pushMenuPage (plugin.buildMenuTree());
}

void ExEditor::presetsMenu()
{
    OverlayPage pg;
    pg.title = "Presets";
    for (auto& f : SdCard::sortedChildren (plugin.presetDir(), juce::File::findFiles, "*.xml"))
        pg.items.push_back ({ f.getFileNameWithoutExtension(), f.getFileNameWithoutExtension() == plugin.lastPresetName, true, false,
                              [this, f] { plugin.loadPresetFile (f); } });
    if (pg.items.empty()) pg.items.push_back ({ "(none saved yet - use SAVE)", false, false, false, {} });
    pg.items.push_back ({ "Reset to defaults", false, true, false, [this] {
        auto& a = plugin.alg();
        for (int p = 1; p <= a.numParams(); ++p) { a.set (p, a.def (p).def); if (a.def (p).confirm) { a.applied[p] = a.def (p).def; a.confirm (p); } } } });
    pg.items.push_back ({ "Show preset folder", false, true, false, [this] { plugin.presetDir().createDirectory(); plugin.presetDir().revealToUser(); } });
    openOverlay (std::move (pg));
}

void ExEditor::sdMenu()
{
    OverlayPage pg;
    pg.title = "SD card";
    pg.items.push_back ({ SdCard::root().getFileName(), false, false, false, {} });
    pg.items.push_back ({ "Choose folder...", false, true, false, [this] { plugin.chooseSdFolder(); } });
    pg.items.push_back ({ "Show in Finder", false, true, false, [] { SdCard::root().revealToUser(); } });
    pg.items.push_back ({ "Rescan", false, true, false, [this] {
        for (int i = 0; i < plugin.numAlgorithms(); ++i) plugin.algAt (i).onSelected();
        plugin.message ("Rescanned SD card"); } });
    openOverlay (std::move (pg));
}

void ExEditor::midiMenu()
{
    OverlayPage pg;
    pg.title = "MIDI";
    auto chanPage = [this] (juce::String title, int* target, juce::String zeroName) {
        return [this, title, target, zeroName] {
            OverlayPage p;
            p.title = title;
            for (int c = 0; c <= 16; ++c)
                p.items.push_back ({ c == 0 ? zeroName : "Channel " + juce::String (c), *target == c, true, false, [target, c] { *target = c; } });
            overlay.push_back (std::move (p)); };
    };
    pg.items.push_back ({ "Receive: " + (plugin.rxChannel == 0 ? juce::String ("Omni") : "ch " + juce::String (plugin.rxChannel)), false, true, true,
                          chanPage ("Receive channel", &plugin.rxChannel, "Omni") });
    if (plugin.producesMidi())
        pg.items.push_back ({ "Send: " + (plugin.txChannel == 0 ? juce::String ("Off") : "ch " + juce::String (plugin.txChannel)), false, true, true,
                              chanPage ("Send channel", &plugin.txChannel, "Off") });
    for (int t = 0; t < plugin.identity.numTimbreChannels && plugin.identity.numTimbreChannels > 1; ++t)
        pg.items.push_back ({ "Timbre " + juce::String (t + 1) + ": " + (plugin.timbreChannel[t] == 0 ? "RX+" + juce::String (t) : "ch " + juce::String (plugin.timbreChannel[t])),
                              false, true, true, chanPage ("Timbre " + juce::String (t + 1) + " channel", &plugin.timbreChannel[t], "Default (RX+" + juce::String (t) + ")") });
    pg.items.push_back ({ "All notes off", false, true, false, [this] { plugin.allNotesOffRequest = true; } });
    openOverlay (std::move (pg));
}
} // namespace ex

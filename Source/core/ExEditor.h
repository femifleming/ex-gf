#pragma once
#include "ExPlugin.h"
#include <set>

namespace ex
{
// The whole plug-in is one big OLED: a 360x232 grey-level pixel canvas drawn with the module's 5x7 font.
struct Canvas
{
    static constexpr int W = 540, H = 300;
    uint8_t px[H][W] {};   // brightness 0..15
    void clear() { std::memset (px, 0, sizeof (px)); }
    void set (int x, int y, int l) { if (x >= 0 && x < W && y >= 0 && y < H) px[y][x] = (uint8_t) juce::jlimit (0, 15, l); }
    void hline (int x0, int x1, int y, int l) { for (int x = x0; x <= x1; ++x) set (x, y, l); }
    void vline (int x, int y0, int y1, int l) { for (int y = y0; y <= y1; ++y) set (x, y, l); }
    void fill (int x, int y, int w, int h, int l) { for (int j = y; j < y + h; ++j) for (int i = x; i < x + w; ++i) set (i, j, l); }
    void rect (int x, int y, int w, int h, int l) { hline (x, x + w - 1, y, l); hline (x, x + w - 1, y + h - 1, l); vline (x, y, y + h - 1, l); vline (x + w - 1, y, y + h - 1, l); }
    void dots (int x0, int x1, int y, int l) { for (int x = x0; x <= x1; x += 2) set (x, y, l); }
    void glyph (int x, int y, char c, int l)
    {
        unsigned char uc = (unsigned char) c;
        if (uc < 32 || uc > 126) uc = '?';
        const uint8_t* g = kFont5x7[uc - 32];
        for (int r = 0; r < 7; ++r)
            for (int k = 0; k < 5; ++k)
                if ((g[r] >> (4 - k)) & 1) set (x + k, y + r, l);
    }
    // text with its top at y (7 rows tall); clipped to maxChars
    int text (int x, int y, const juce::String& s, int l, int maxChars = 999)
    {
        int n = 0;
        for (auto ch : s)
        {
            if (n++ >= maxChars) break;
            glyph (x, y, (char) (ch < 128 ? ch : '?'), l);
            x += 6;
        }
        return x;
    }
    static int width (const juce::String& s) { return juce::jmax (0, s.length() * 6 - 1); }
};

class ExEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit ExEditor (ExPlugin&);
    ~ExEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = { -1, -1 }; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; }
    void filesDropped (const juce::StringArray&, int, int) override;

    static constexpr int scaleBase = 2;
    static constexpr int baseW = Canvas::W * scaleBase, baseH = Canvas::H * scaleBase;

private:
    struct Hit
    {
        juce::Rectangle<int> r;
        std::function<void()> click, dbl;
        std::function<void (float dyScreen, bool shift)> drag;   // dy since mouse down (screen px, up = +)
        std::function<void (int)> wheel;
        std::function<void (juce::Point<int>)> press, move, release;  // keyboard
        juce::String tip;
    };
    struct OverlayItem
    {
        juce::String label;
        bool ticked = false, enabled = true, sub = false;
        std::function<void()> action;
    };
    struct OverlayPage { juce::String title; std::vector<OverlayItem> items; int scroll = 0; };

    ExPlugin& plugin;
    Canvas cv;
    Oled oled;
    juce::Image frame;
    std::vector<Hit> hits;
    Hit active; bool hasActive = false, dragged = false;
    juce::Point<int> hover { -1, -1 }, downPos;
    float scale = 2.0f;
    int section[8] {}, gridScroll[8] {}, listScroll[8][4] {};
    int pixScale = 2;
    juce::Point<int> origin;
    std::vector<OverlayPage> overlay;
    juce::String tip;
    int shownAlg = -1, tick = 0;
    int mouseNote = -1;
    std::set<int> keysDown;
    int kbOctave = 4;
    bool dropHover = false;

    void timerCallback() override;
    void draw();
    Hit& addHit (juce::Rectangle<int> r) { hits.push_back ({}); hits.back().r = r; return hits.back(); }
    bool hovered (juce::Rectangle<int> r) const { return r.contains (hover); }
    juce::Point<int> toCanvas (juce::Point<float> p) const { return { (int) std::floor ((p.x - (float) origin.x) / (float) pixScale), (int) std::floor ((p.y - (float) origin.y) / (float) pixScale) }; }

    // widgets
    int button (int x, int y, int w, const juce::String& label, bool on, std::function<void()> click, const juce::String& tipText = {}, bool rec = false);
    void topBar();
    void sideBoxes();
    void lists();
    void listBox (int x, int y, int w, int h, BrowserSpec& spec, int& scroll);
    void displayArea (int x, int y);
    void tabsAndTiles (int x, int y, int w, int h);
    void tile (int x, int y, int w, int h, ExAlgorithm& a, int n);
    void overlayArea();

    // overlays
    void openOverlay (OverlayPage p) { overlay.clear(); overlay.push_back (std::move (p)); }
    void pushMenuPage (MenuItem& item);
    void choicesFor (ExAlgorithm& a, int n);
    void presetsMenu();
    void sdMenu();
    void midiMenu();
    void fullMenu();

    juce::String valueString (ExAlgorithm& a, int n, int v) const;
    bool isChoice (ExAlgorithm& a, int n) const;
    static juce::String shortName (juce::String s);
    void noteOn (int note, float vel);
    void noteOff (int note);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExEditor)
};
} // namespace ex

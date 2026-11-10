#pragma once
// 128x32 monochrome framebuffer emulating the disting EX OLED, with its 6x8 character grid (21 cols x 4 rows).
#include <juce_core/juce_core.h>
#include <cstring>
#include <cstdint>
#include "ExFont.h"

namespace ex
{
struct Oled
{
    static constexpr int W = 128, H = 32, CW = 6, CH = 8, COLS = 21, ROWS = 4;
    uint8_t px[H][W] {};

    void clear() { std::memset (px, 0, sizeof (px)); }
    void set (int x, int y, bool on = true)
    {
        if (x >= 0 && x < W && y >= 0 && y < H) px[y][x] = on ? 1 : 0;
    }
    void flip (int x, int y)
    {
        if (x >= 0 && x < W && y >= 0 && y < H) px[y][x] ^= 1;
    }
    bool get (int x, int y) const { return x >= 0 && x < W && y >= 0 && y < H && px[y][x]; }
    void hline (int x0, int x1, int y, bool on = true) { for (int x = x0; x <= x1; ++x) set (x, y, on); }
    void vline (int x, int y0, int y1, bool on = true) { for (int y = y0; y <= y1; ++y) set (x, y, on); }
    void fillRect (int x, int y, int w, int h, bool on = true)
    {
        for (int j = y; j < y + h; ++j) for (int i = x; i < x + w; ++i) set (i, j, on);
    }
    void invertRect (int x, int y, int w, int h)
    {
        for (int j = y; j < y + h; ++j) for (int i = x; i < x + w; ++i) flip (i, j);
    }
    void rect (int x, int y, int w, int h)
    {
        hline (x, x + w - 1, y); hline (x, x + w - 1, y + h - 1);
        vline (x, y, y + h - 1); vline (x + w - 1, y, y + h - 1);
    }
    void dottedRect (int x, int y, int w, int h)
    {
        for (int i = x; i < x + w; i += 2) { set (i, y); set (i, y + h - 1); }
        for (int j = y; j < y + h; j += 2) { set (x, j); set (x + w - 1, j); }
    }

    // draw one glyph with its top-left at pixel (x, y); sx/sy integer scale
    void glyph (int x, int y, char c, bool invert = false, int sx = 1, int sy = 1)
    {
        unsigned char uc = (unsigned char) c;
        if (uc < 32 || uc > 126) uc = '?';
        const uint8_t* g = kFont5x7[uc - 32];
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 6; ++col)
            {
                bool on = row >= 1 && col < 5 && ((g[row - 1] >> (4 - col)) & 1);
                if (invert) on = ! on;
                if (! on && ! invert) continue;
                for (int a = 0; a < sy; ++a)
                    for (int b = 0; b < sx; ++b)
                        set (x + col * sx + b, y + row * sy + a, on);
            }
    }
    // text at pixel position; returns x after the text
    int text (int x, int y, const juce::String& s, bool invert = false, int sx = 1, int sy = 1)
    {
        for (auto ch : s)
        {
            if (x >= W) break;
            glyph (x, y, (char) (ch < 128 ? ch : '?'), invert, sx, sy);
            x += CW * sx;
        }
        return x;
    }
    // text on the character grid
    void at (int col, int row, const juce::String& s, bool invert = false) { text (col * CW, row * CH, s, invert); }
    void right (int endCol, int row, const juce::String& s, bool invert = false) { at (endCol - s.length(), row, s, invert); }
    void centre (int row, const juce::String& s, bool invert = false) { text ((W - s.length() * CW) / 2, row * CH, s, invert); }
    static int textWidth (const juce::String& s, int sx = 1) { return s.length() * CW * sx; }

    // tall font used for the LOCKED message / zoomed values: each glyph pixel becomes 2x3 (smoothed a little)
    void bigText (int x, int y, const juce::String& s)
    {
        for (auto ch : s)
        {
            glyph (x, y - 3, (char) ch, false, 2, 3);
            x += 12;
        }
    }
};

inline juce::String noteName (int n) // MIDI 60 = C4 (disting EX convention)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}
} // namespace ex

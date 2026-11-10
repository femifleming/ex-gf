#!/usr/bin/env python3
"""Builds docs/EX-GF-manual.pdf (white on black, headings in the module's 5x7 pixel font).

    python3 tools/make_manual.py <params.json> <out.pdf>
params.json comes from:  EXTest --dump-params params.json
"""
import json, os, re, sys
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT, TA_CENTER
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Table, TableStyle,
                                PageBreak, Image, Flowable, KeepTogether, CondPageBreak)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PARAMS = json.load(open(sys.argv[1]))
OUT = sys.argv[2]
VERSION = "1.0.2"

# ------------------------------------------------------------------ pixel font (from Source/core/ExFont.h)
FONT = {}
for m in re.finditer(r'\{(0x[0-9a-f]{2}(?:,0x[0-9a-f]{2}){6})\}, // (.+)', open(os.path.join(ROOT, "Source/core/ExFont.h")).read()):
    FONT[eval(m.group(2))] = [int(v, 16) for v in m.group(1).split(',')]

WHITE = colors.white
GREY = colors.Color(0.62, 0.62, 0.62)
DIM = colors.Color(0.32, 0.32, 0.32)
BLACK = colors.black


def draw_pixel_text(c, text, x, y_top, px, colour=WHITE):
    c.setFillColor(colour)
    for ch in text:
        g = FONT.get(ch, FONT['?'])
        for r in range(7):
            for k in range(5):
                if g[r] >> (4 - k) & 1:
                    c.rect(x + k * px, y_top - (r + 1) * px, px * 0.92, px * 0.92, stroke=0, fill=1)
        x += 6 * px


def pixel_width(text, px):
    return len(text) * 6 * px - px


class PixelHeading(Flowable):
    def __init__(self, text, px=2.4, colour=WHITE, align="left", rule=True, space=6):
        super().__init__()
        self.text, self.px, self.colour, self.align, self.rule, self.space = text, px, colour, align, rule, space
        self._bookmark = None
        self.keepWithNext = True

    def wrap(self, aw, ah):
        self.aw = aw
        return aw, 7 * self.px + (8 if self.rule else 2) + self.space

    def draw(self):
        c = self.canv
        h = 7 * self.px
        x = 0 if self.align == "left" else (self.aw - pixel_width(self.text, self.px)) / 2
        top = h + (8 if self.rule else 2)
        draw_pixel_text(c, self.text, x, top, self.px, self.colour)
        if self.rule:
            c.setFillColor(DIM)
            for i in range(0, int(self.aw), 4):
                c.rect(i, 1, 2, 1.2, stroke=0, fill=1)


class Inverted(Flowable):
    """A white bar with black pixel text - like the module's highlighted headers."""
    def __init__(self, text, px=1.6):
        super().__init__()
        self.text, self.px = text, px
        self.keepWithNext = True

    def wrap(self, aw, ah):
        self.aw = aw
        return aw, 7 * self.px + 6 * self.px / 1.6 + 6

    def draw(self):
        c = self.canv
        h = 7 * self.px + 6
        c.setFillColor(WHITE)
        c.rect(0, 6, self.aw, h, stroke=0, fill=1)
        draw_pixel_text(c, self.text, 4, 6 + h - 3, self.px, BLACK)


class Diagram(Flowable):
    """The screen layout (layout C) as labelled pixel boxes."""
    def wrap(self, aw, ah):
        self.aw = aw
        return aw, aw * 300 / 540 + 4

    def draw(self):
        c = self.canv
        s = self.aw / 540.0
        H = 300 * s

        def box(x, y, w, h, label, fill=False):
            yy = H - (y + h) * s
            c.setStrokeColor(WHITE if fill else GREY)
            c.setLineWidth(0.8)
            c.setFillColor(colors.Color(0.12, 0.12, 0.12))
            c.rect(x * s, yy, w * s, h * s, stroke=1, fill=1)
            px = 1.15
            lines = label.split("\n")
            for i, ln in enumerate(lines):
                draw_pixel_text(c, ln, x * s + (w * s - pixel_width(ln, px)) / 2, yy + h * s / 2 + (len(lines) * 9 * px) / 2 - i * 9 * px, px, WHITE)

        for i, t in enumerate(["SAVE", "LOAD", "SD"]):
            box(3 + i * 43, 2, 40, 11, t)
        for i, t in enumerate(["MIDI", "PANIC", "MENU"]):
            box(540 - 3 - 40 - (2 - i) * 43, 2, 40, 11, t)
        box(160, 2, 220, 11, "TITLE / ALGORITHM TABS")
        box(3, 21, 127, 70, "1 TIMBRE +\nVOICES")
        box(139, 21, 262, 70, "2 MODULE DISPLAY (2x)")
        box(410, 21, 127, 70, "3 ACTIONS")
        box(3, 95, 534, 8, "4 STATUS LINE - HOVER FOR HELP")
        box(3, 106, 175, 191, "5 LEFT LIST\n(FOLDERS/\nTABLES/BANKS)")
        box(183, 106, 174, 26, "6 SECTION TABS")
        box(183, 135, 174, 162, "7 PARAMETER\nTILES")
        box(362, 106, 175, 191, "8 RIGHT LIST\n(SAMPLES/WAVES/\nVOICES)")


# ------------------------------------------------------------------ styles
body = ParagraphStyle("body", fontName="Helvetica", fontSize=9.6, leading=13.6, textColor=colors.Color(0.86, 0.86, 0.86), spaceAfter=6)
small = ParagraphStyle("small", parent=body, fontSize=8, leading=10.5, textColor=GREY)
bullet = ParagraphStyle("bullet", parent=body, leftIndent=12, bulletIndent=2, spaceAfter=3)
code = ParagraphStyle("code", parent=body, fontName="Courier", fontSize=8.6, leading=11.5, textColor=WHITE,
                      backColor=colors.Color(0.1, 0.1, 0.1), borderPadding=(5, 6, 5, 6), leftIndent=6, rightIndent=6, spaceBefore=4, spaceAfter=10)
h3 = ParagraphStyle("h3", parent=body, fontName="Helvetica-Bold", fontSize=10.5, textColor=WHITE, spaceBefore=8, spaceAfter=4)
cell = ParagraphStyle("cell", parent=body, fontSize=7.6, leading=9.4, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Helvetica-Bold", textColor=WHITE)


def fix(t):
    t = str(t)
    for a, b in (("↑↓", "Up/Down"), ("←→", "Left/Right"), ("→", "->"), ("▾", "(v)"), ("\u25aa", "\u2022")):
        t = t.replace(a, b)
    return t


def P(t, st=body):
    return Paragraph(fix(t), st)


def B(items):
    return [Paragraph(fix(t), bullet, bulletText="\u2022") for t in items]


def C(t):
    return Paragraph(t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\n", "<br/>").replace(" ", "&nbsp;"), code)


def table(rows, widths, header=True):
    data = [[Paragraph(fix(x), cellb if (header and i == 0) else cell) for x in r] for i, r in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    st = [("VALIGN", (0, 0), (-1, -1), "TOP"),
          ("LINEBELOW", (0, 0), (-1, -1), 0.3, DIM),
          ("TOPPADDING", (0, 0), (-1, -1), 2.5), ("BOTTOMPADDING", (0, 0), (-1, -1), 2.5),
          ("LEFTPADDING", (0, 0), (-1, -1), 3), ("RIGHTPADDING", (0, 0), (-1, -1), 3)]
    if header:
        st += [("BACKGROUND", (0, 0), (-1, 0), colors.Color(0.16, 0.16, 0.16)), ("LINEBELOW", (0, 0), (-1, 0), 0.8, WHITE)]
    t.setStyle(TableStyle(st))
    return t


def shot(name, width=170 * mm):
    p = os.path.join(ROOT, "docs/screenshots", name + ".png")
    from PIL import Image as PILImage
    w, h = PILImage.open(p).size
    img = Image(p, width=width, height=width * h / w)
    img.hAlign = "CENTER"
    return img


CV_ONLY = re.compile(r"Attenuverter|Input mode|Gate offset|bend input|Arp reset input|Wave input|Rec CV Mode|Play CV Mode|Pitch bend input")


def param_table(alg):
    rows = [["#", "Parameter", "Range", "Default", "Page", "Values / notes"]]
    for p in alg["params"]:
        vals = p.get("values", "")
        vlist = vals.split("|") if vals else []
        rng = f'{p["min"]} .. {p["max"]}' + (f' {p["unit"]}' if p["unit"] else "")
        d = p["def"]
        dflt = vlist[d - p["min"]] if vlist and 0 <= d - p["min"] < len(vlist) else (str(d) + (f' {p["unit"]}' if p["unit"] else ""))
        note = ", ".join(f"{p['min'] + i}={v}" for i, v in enumerate(vlist)) if vlist and len(vlist) <= 15 else ""
        if CV_ONLY.search(p["name"]):
            note = "<i>CV/hardware only - kept for numbering, no effect in the plug-in</i>"
        LIST = re.compile(r"^(Folder|Folder 2|Folder 3|Wavetable|Table|Exciter|Bank \d|Voice \d|Playback file)$")
        page = p.get("section", "") or ("-" if CV_ONLY.search(p["name"]) else "list" if LIST.match(p["name"]) else "display")
        rows.append([p["n"], p["name"], rng, dflt, page, note])
    return table(rows, [9 * mm, 36 * mm, 26 * mm, 22 * mm, 17 * mm, 60 * mm])


def alg(number):
    return next(a for a in PARAMS if a["number"] == number)


# ------------------------------------------------------------------ document
class Doc(BaseDocTemplate):
    def __init__(self, path):
        super().__init__(path, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=18 * mm, bottomMargin=18 * mm,
                         title="EX-GF manual", author="EX-GF", subject="disting EX-style instruments - user manual")
        frame = Frame(self.leftMargin, self.bottomMargin, self.width, self.height, id="f")
        self.addPageTemplates([PageTemplate("cover", [frame], onPage=self.bg_cover), PageTemplate("page", [frame], onPage=self.bg)])
        self.toc = []

    def bg_cover(self, c, d):
        c.setFillColor(BLACK); c.rect(0, 0, A4[0], A4[1], stroke=0, fill=1)

    def bg(self, c, d):
        c.setFillColor(BLACK); c.rect(0, 0, A4[0], A4[1], stroke=0, fill=1)
        draw_pixel_text(c, "EX-GF MANUAL", 18 * mm, 12 * mm, 0.9, DIM)
        n = str(c.getPageNumber())
        draw_pixel_text(c, n, A4[0] - 18 * mm - pixel_width(n, 1.2), 12.5 * mm, 1.2, GREY)

    def afterFlowable(self, f):
        if isinstance(f, PixelHeading) and f.px >= 2.4 and getattr(f, "toc", False):
            key = "h%d" % len(self.toc)
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(f.text.title(), key, 0)
            self.toc.append((f.text, self.page))


def H1(t):
    h = PixelHeading(t, px=2.6)
    h.toc = True
    return h


def H2(t):
    return PixelHeading(t, px=1.6, rule=False, space=4)


class TocTable(Flowable):
    def __init__(self, doc):
        super().__init__()
        self.doc = doc

    def wrap(self, aw, ah):
        self.aw = aw
        return aw, 16 * max(1, len(self.doc.toc_prev))

    def draw(self):
        c = self.canv
        y = 16 * len(self.doc.toc_prev)
        for text, page in self.doc.toc_prev:
            y -= 16
            draw_pixel_text(c, text, 0, y + 11, 1.4, WHITE)
            pn = str(page)
            draw_pixel_text(c, pn, self.aw - pixel_width(pn, 1.4), y + 11, 1.4, WHITE)
            c.setFillColor(DIM)
            x0 = pixel_width(text, 1.4) + 6
            for x in range(int(x0), int(self.aw - pixel_width(pn, 1.4) - 6), 4):
                c.rect(x, y + 2, 1.4, 1.4, stroke=0, fill=1)


def story(doc):
    s = []
    # ---------------- cover
    s.append(Spacer(1, 40 * mm))
    s.append(PixelHeading("EX-GF", px=12, align="center", rule=False, space=14))
    s.append(PixelHeading("USER MANUAL", px=2.2, align="center", rule=False, space=20))
    s.append(shot("fm", 165 * mm))
    s.append(Spacer(1, 10 * mm))
    s.append(P("<para alignment='center'>Four disting EX-style instruments: <b>EX Multisample</b> · <b>EX Poly Wavetable</b> · "
               "<b>EX Poly FM</b> · <b>EX Poly Exciter</b><br/>VST3 · AU · LV2 · standalone - macOS, Windows and Linux</para>"))
    s.append(Spacer(1, 30 * mm))
    s.append(P(f"<para alignment='center'>Version {VERSION} - github.com/femifleming/ex-gf</para>", small))
    from reportlab.platypus.doctemplate import NextPageTemplate
    s.append(NextPageTemplate("page"))
    s.append(PageBreak())

    # ---------------- contents
    s.append(PixelHeading("CONTENTS", px=2.6))
    s.append(Spacer(1, 4))
    s.append(TocTable(doc))
    s.append(PageBreak())

    # ---------------- intro
    s.append(H1("INTRODUCTION"))
    s.append(P("<b>EX-GF</b> is a suite of four software instruments modelled on algorithms of the Expert Sleepers "
               "<b>disting EX</b> Eurorack module. They follow the module's manual (firmware 1.26) parameter for parameter, "
               "use the same file formats (sample folders, wavetables, exciters, DX7 banks, Scala tunings) and wear the "
               "module's look: the whole window is one white-on-black pixel display in the module's own 5×7 font."))
    s.append(table([
        ["Plug-in", "Algorithm(s)", "In short"],
        ["EX Multisample", "3 - SD Multisample<br/>5 - WAV Recorder", "16-voice, 3-timbre sample player with velocity layers, round robins, chords, arpeggiator, EQ and delay; "
                                                                        "plus a recorder with an <b>auto-sampler</b> that samples any MIDI/CV instrument into a ready-to-play folder."],
        ["EX Poly Wavetable", "10 - Poly Wavetable", "16-voice wavetable synth: 2 envelopes, filter, LFO, unison, Juno-style chorus and delay. Imports WaveEdit banks."],
        ["EX Poly FM", "23 - Poly FM", "16-voice, 4-timbre 6-operator FM (the Plaits engine). Loads DX7 .syx banks; DX7 editors can edit it over SysEx."],
        ["EX Poly Exciter", "13 - Poly Exciter", "16-voice exciter/resonator synth: plucked, struck and bowed-like sounds from short exciter samples or live input."],
    ], [32 * mm, 36 * mm, 102 * mm]))
    s.append(Spacer(1, 6))
    s.append(P("Each instrument is available as a <b>VST3</b> (all platforms), <b>Audio Unit</b> (macOS), <b>LV2</b> (Linux) and a "
               "<b>standalone app</b>. Settings are saved with your DAW project, and every plug-in also remembers its last state "
               "between sessions."))
    s.append(P("EX-GF is not affiliated with or endorsed by Expert Sleepers; \"disting\" is their trademark and is used "
               "only to describe compatibility. If you own the module, its MicroSD card works directly with these plug-ins.", small))

    # ---------------- install
    s.append(H1("INSTALLING"))
    s.append(P("All downloads are on the release page: <b>github.com/femifleming/ex-gf/releases</b>."))
    s.append(H2("MACOS - HOMEBREW"))
    s.append(C("brew tap femifleming/ex-gf https://github.com/femifleming/ex-gf\n"
               "brew trust femifleming/ex-gf\n"
               "brew install --cask ex-gf            # apps + VST3 + AU\n"
               "brew install --cask ex-gf-sd-card    # optional sample library"))
    s.append(P("Homebrew 7 asks you to <i>trust</i> third-party taps once. To build from source instead (macOS or Linux): "
               "<font face='Courier'>brew install ex-gf</font>, then follow the link instructions it prints."))
    s.append(H2("MACOS - DOWNLOAD"))
    s += B(["<b>EX-GF-1.0.2-macOS.zip</b>: unzip and double-click <b>install.command</b> - apps go to ~/Applications, plug-ins to "
            "~/Library/Audio/Plug-Ins/VST3 and /Components.",
            "<b>EX-GF-1.0.2-macOS.pkg</b>: installs for all users (right-click → Open the first time).",
            "The builds are universal (Apple silicon + Intel) but not notarised. If macOS or your DAW refuses to open them, run:"])
    s.append(C("xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/EX*.vst3 \\\n    ~/Library/Audio/Plug-Ins/Components/EX*.component ~/Applications/EX*.app"))
    s.append(H2("WINDOWS"))
    s.append(P("Unzip <b>EX-GF-1.0.2-Windows-x64.zip</b>, right-click <b>install.ps1</b> → <i>Run with PowerShell</i> (as administrator). "
               "VST3 plug-ins go to C:\\Program Files\\Common Files\\VST3; the apps appear in the Start menu under <b>EX-GF</b>."))
    s.append(H2("LINUX"))
    s.append(C("tar xzf EX-GF-1.0.2-Linux-x86_64.tar.gz\ncd ex-gf-1.0.2-Linux-x86_64 && ./install.sh"))
    s.append(P("VST3 → ~/.vst3, LV2 → ~/.lv2, apps → ~/.local/bin with desktop menu entries."))
    s.append(H2("THE SAMPLE LIBRARY"))
    s.append(P("Download <b>EX-SD-Card.zip</b> and unzip it into your <b>Music</b> folder so you have <b>Music/EX SD Card</b> (the default "
               "location), or pick any folder with the <b>SD</b> button. It holds 18 multisampled instruments, 38 wavetables, 5 exciter "
               "tables, DX7 banks and Scala tunings - all free (CC0 / CC-BY / MIT, see CREDITS.txt inside). A real disting EX MicroSD "
               "card works too."))
    s.append(H2("BUILDING FROM SOURCE"))
    s.append(C("git clone --recursive https://github.com/femifleming/ex-gf\ncd ex-gf\n"
               "cmake -B build -DCMAKE_BUILD_TYPE=Release\ncmake --build build --config Release"))

    # ---------------- interface
    s.append(PageBreak())
    s.append(H1("THE SCREEN"))
    s.append(P("Every plug-in uses the same mirror-symmetrical layout. The window can be resized; it snaps to whole-pixel sizes "
               "(1×, 2×, 3× …) so the pixels stay sharp."))
    s.append(Diagram())
    s.append(Spacer(1, 6))
    s.append(table([
        ["", "Area", "What it does"],
        ["", "Top bar", "<b>SAVE</b> / <b>LOAD</b> presets (stored on the SD card), <b>SD</b> chooses the card folder, <b>MIDI</b> channels, "
                        "<b>PANIC</b> (all notes off), <b>MENU</b> (every option of the module's menus). In EX Multisample the centre holds the algorithm tabs."],
        ["1", "Timbre / voices", "Timbre buttons (multi-timbral plug-ins) choose which timbre the lists edit. The 16 cells show each voice's note and level."],
        ["2", "Module display", "The disting EX's own 128×32 screen: current parameter, live voices, levels, auto-sampler progress. "
                                "Mouse wheel = parameter, shift+wheel = value; ↑↓ and ←→ act like the P and V encoders, Enter = push P (menu), Esc = push V."],
        ["3", "Actions", "Buttons for the algorithm: RECORD, PLAY, AUTO, TEST, IMPORT, WAVEEDIT, NOTES OFF, RESCAN …"],
        ["4", "Status line", "Hover anything for a description. MIDI IN / OUT activity lights sit at either end."],
        ["5/8", "Lists", "Click to load: sample folders and their samples, wavetables and their waves, exciter tables and exciters, FM banks and voices, recordings."],
        ["6", "Section tabs", "Pages of related parameters (e.g. ENV, FILTER, CHORD, FX)."],
        ["7", "Parameter tiles", "Drag up/down or use the wheel to change (hold shift for fine steps); double-click resets to the default. "
                                 "Tiles with a small ▾ open a list of choices when clicked."],
    ], [9 * mm, 30 * mm, 131 * mm]))
    s.append(H2("PLAYING NOTES"))
    s.append(P("Play from MIDI, or click the window and use the computer keyboard: <b>A W S E D F T G Y H U J K O L P ;</b> play a "
               "chromatic octave and a half, <b>Z</b>/<b>X</b> shift the octave."))
    s.append(H2("MIDI"))
    s += B(["The receive channel defaults to 1 (Omni is available under MIDI). Multi-timbral plug-ins listen on consecutive channels "
            "(timbre 1 on the receive channel, timbre 2 on the next …) unless you assign each timbre its own channel.",
            "Recognised: note on/off, velocity, pitch bend, sustain (CC64), All Notes Off (CC123); channel and poly aftertouch in Poly Exciter; "
            "DX7 SysEx in Poly FM. <i>Delay uses MIDI clk</i> (menu) locks delay times to the host tempo."])
    s.append(H2("OUTPUTS"))
    s.append(P("<b>Out 1/2</b> is the main stereo output. <b>Out 3/4</b> is a second stereo bus (enable it in your DAW) carrying per-timbre or "
               "per-voice outputs depending on the <i>Output mode</i>, the paraphonic gate and mono mix (as on the module), and the "
               "auto-sampler's CV/gate."))
    s.append(H2("SAVING AND REMEMBERING"))
    s += B(["In a DAW, each instance is saved with the project.",
            "<b>SAVE</b> writes a preset to <i>SD card/presets/&lt;plug-in&gt;</i>; <b>LOAD</b> lists them (plus <i>Reset to defaults</i>).",
            "Every plug-in and app also remembers its <b>last state</b>: it is written whenever you change something and when the plug-in closes, "
            "and a new instance starts from it. The files live in ~/Library/Application Support/EX-GF (macOS), "
            "%APPDATA%\\EX-GF (Windows) or ~/.config/EX-GF (Linux)."])

    # ---------------- SD card
    s.append(PageBreak())
    s.append(H1("THE SD CARD FOLDER"))
    s.append(P("The plug-ins read a folder laid out exactly like the disting EX's MicroSD card (default <b>Music/EX SD Card</b>)."))
    s.append(table([
        ["Folder", "Contents"],
        ["&lt;any name&gt;/", "A <b>multisample folder</b> of WAV files (see naming below). Every top-level folder that contains WAVs is listed."],
        ["wavetables/", "Each wavetable is a sub-folder of single-cycle WAVs (used alphabetically), or one WAV of concatenated cycles "
                        "(600 samples each; 256 for WaveEdit banks - detected automatically). Optional <i>playlist-wavetable.txt</i> with -wavelength=."],
        ["exciters/", "Each exciter table is a folder of up to 128 short WAVs, or one WAV containing exactly 128 equal-length exciters."],
        ["FMSYX/", "DX7 32-voice bank SysEx files (.syx, 4104 bytes). Without any, three built-in banks are used."],
        ["scl/  kbm/", "Scala scales and keyboard maps for the Scala/MTS and Scala KBM parameters."],
        ["!RECORD/", "WAV Recorder recordings (REC00001.WAV …)."],
        ["AUTO0001/ …", "Auto-sampler output - ordinary multisample folders."],
        ["presets/", "Presets saved with SAVE."],
    ], [30 * mm, 140 * mm]))
    s.append(H2("NAMING MULTISAMPLES"))
    s.append(table([
        ["Token", "Example", "Meaning"],
        ["_&lt;note&gt;", "piano_A2.wav, piano_C#5.wav", "Natural pitch. Sharps only, C4 = MIDI note 60."],
        ["_SW&lt;n&gt;", "piano_A2_SW40.wav", "Switch point: the lowest MIDI note that uses this sample."],
        ["_V&lt;n&gt;", "snare_V1.wav, snare_V2.wav", "Velocity layer; V1 is the softest."],
        ["_RR&lt;n&gt;", "snare_RR1.wav, snare_RR2.wav", "Round-robin variants of the same sample."],
        ["combined", "cello_C3_V2_RR1.wav", "Velocity first, then round robin."],
    ], [22 * mm, 55 * mm, 93 * mm]))
    s.append(Spacer(1, 6))
    s += B(["Files without a note name are mapped chromatically from note 48 (C3) upwards - handy for one-shots.",
            "Without explicit switch points, gaps of up to 3 semitones are covered by pitching the upper sample down; for larger gaps the "
            "lower sample is also pitched up over part of the gap (exactly as on the module).",
            "Loops are read from the WAV's <i>smpl</i> or <i>cue</i> chunks. A sample loops if it has loop points, otherwise it plays once.",
            "An optional <i>playlist.txt</i> (\"disting playlist v1\") can set -natural=, -switch=, -loop= and -endLoopOnRelease= per file."])

    # ---------------- SD multisample
    s.append(PageBreak())
    s.append(H1("EX MULTISAMPLE"))
    s.append(shot("ms"))
    s.append(Spacer(1, 6))
    s.append(Inverted("3 - SD MULTISAMPLE"))
    s.append(P("A 16-voice, 3-timbre sample instrument. Choose a folder in the left list; the right list shows its samples - click one "
               "to hear it. The display's top line shows the file, natural note and switch point of the highest playing voice."))
    s += B(["<b>Timbres</b>: the TIMBRE buttons pick which timbre the folder list sets. Timbres 2 and 3 can follow timbre 1 or use their own "
            "folders, and each has its own note range and arpeggiator.",
            "<b>SOUND</b>: gain, saturation, voice count, round-robin mode, output mode and stereo spread.",
            "<b>ENV</b>: attack/decay/sustain/release; release 127 = infinite (the sample plays to its end or loops forever).",
            "<b>TUNE</b>: octave, transpose, fine tune, bend range, Scala tuning and per-voice detune (an \"old polysynth\" spread).",
            "<b>CHORD</b>: chord generator (key, scale, shape, inversion), arpeggiator per timbre and broken chords (strum time/direction).",
            "<b>FX</b>: stereo or ping-pong delay and bass/treble EQ.  <b>KEYS</b>: sustain behaviour, velocity curve and note ranges.",
            "MENU → SD Multisample → Options: MIDI note range, paraphonic gate mode, <i>Delay uses MIDI clk</i>, <i>Transpose limit</i>."])
    s.append(H2("CHORDS AND ARPEGGIOS"))
    s.append(table([
        ["Scales", "Shapes", "Arpeggio modes"],
        ["Major, Natural Minor, Dominant, Fully Diminished, Dominant Dim, Augmented, Whole Tone, Chromatic, Harmonic Minor",
         "None, Octave, Two Octaves, Root/Fifth, Root/Fifth + 8ve, Triad, Triad + 8ve, Sus4, Sus4 + 8ve, Sixth, Sixth + 8ve, Seventh, Seventh + 8ve, Ninth",
         "Off, Up, Down, Alt, Alt2, Up/Down/Alt/Alt2 -8ve (skip the repeated octave), Step Up, Step Down, Random, Random2 (no repeats), Random3 (permutations), As Played"],
    ], [50 * mm, 60 * mm, 60 * mm]))
    s.append(P("Each new note (gate) advances the arpeggio by one step through the chord built on it; with chords off, the arpeggio runs "
               "over the notes you are holding. <i>Range</i> 2 or 3 adds copies one or two octaves up.", small))
    s.append(CondPageBreak(60 * mm))
    s.append(H2("PARAMETERS"))
    s.append(param_table(alg(3)))

    s.append(PageBreak())
    s.append(shot("rec"))
    s.append(Spacer(1, 6))
    s.append(Inverted("5 - WAV RECORDER AND AUTO-SAMPLER"))
    s.append(P("Switch to the recorder with the <b>5 WAV RECORDER</b> tab. <b>RECORD</b> records the plug-in's audio input to "
               "<i>!RECORD/RECnnnnn.WAV</i> (48 or 96 kHz, 16 or 24-bit, mono or stereo); <b>PLAY</b> plays the recording selected in the "
               "left list. The display shows level meters with peak hold and clip indicators."))
    s.append(H2("AUTO-SAMPLING AN INSTRUMENT"))
    steps = [
        "<b>Connect it.</b> In the standalone app open <i>Options → Audio/MIDI settings</i>: pick the MIDI output that plays your synth and "
        "the audio input it comes back on. (In a DAW, route the plug-in's MIDI out to the synth and the synth's audio into the plug-in - "
        "not every DAW allows MIDI out of an instrument, so the app is easiest.) Modular synths can use the CV/gate on Out 3/4.",
        "<b>Plan it</b> on the AUTO-SAMPLER page: start and end note, note step, number of velocity layers (spread between AS Min and "
        "AS Max velocity), round robins, note length (AS Coarse seconds + AS Fine ms) and AS Gap (time left for the release).",
        "<b>Press TEST.</b> A note plays and the status line shows the measured latency in samples. Set <i>AS Latency</i> a little below it "
        "so each sample starts right at the attack.",
        "<b>Press AUTO.</b> Every note × velocity × round robin is played and recorded into a new <i>AUTOnnnn</i> folder, already named for "
        "SD Multisample (e.g. 060_C4_V1_RR1.wav). Press AUTO again to stop early.",
        "<b>Play it.</b> Click the folder in the right-hand <i>Auto-sampled</i> list and the plug-in switches to SD Multisample with it loaded.",
    ]
    for i, t in enumerate(steps):
        s.append(Paragraph(fix(t), bullet, bulletText=f"{i + 1}."))
    s.append(CondPageBreak(60 * mm))
    s.append(H2("PARAMETERS"))
    s.append(param_table(alg(5)))

    # ---------------- Poly Wavetable
    s.append(PageBreak())
    s.append(H1("EX POLY WAVETABLE"))
    s.append(shot("wt"))
    s.append(Spacer(1, 6))
    s.append(P("A 16-voice wavetable synthesizer. Pick a wavetable in the left list; the right list shows its waves - click one to jump "
               "the <i>Wave offset</i> to it. The position can also follow velocity, pitch, both envelopes and the LFO, and <i>Wave spread</i> "
               "gives every voice a different position. Each voice has two envelopes (with curve shapes; envelope 2's sustain can be negative), "
               "a state-variable filter (low/band/high pass) and an LFO; unison stacks up to 8 of the 16 voices per note."))
    s += B(["<b>OSC</b> wave offset/spread, unison, voices, gain · <b>ENV 1</b>, <b>ENV 2</b> · <b>FILTER</b> type, cutoff (as a MIDI note), resonance and "
            "modulation · <b>MOD</b> routings and LFO speed/retrigger/spread · <b>CHORD</b> · <b>FX</b> Juno-style chorus I/II, delay, output mode "
            "(<i>Per voice</i> sends voices to Out 1-4) · <b>KEYS</b> tuning, sustain, trigger mode (gated, drone, triggered), Scala.",
            "Wavetables are band-limited (mip-mapped) so high notes stay clean."])
    s.append(H2("YOUR OWN WAVETABLES AND WAVEEDIT"))
    s += B(["<b>Drag and drop</b> WAV files or folders onto the window, or press <b>IMPORT</b>.",
            "<b>WAVEEDIT</b> imports every bank saved by the WaveEdit app in <i>Documents/WaveEdit/banks</i>; press it again after editing to update them.",
            "WaveEdit banks (64 waves × 256 samples) and disting tables (600-sample cycles) are recognised automatically; a set of single-cycle "
            "files becomes one table. Each import becomes a folder of single-cycle WAVs in <i>wavetables/</i>, so it also works on a real disting EX."])
    s.append(CondPageBreak(60 * mm))
    s.append(H2("PARAMETERS"))
    s.append(param_table(alg(10)))

    # ---------------- Poly FM
    s.append(PageBreak())
    s.append(H1("EX POLY FM"))
    s.append(shot("fm"))
    s.append(Spacer(1, 6))
    s.append(P("A 16-voice, 4-timbre FM synthesizer using the six-operator engine of Mutable Instruments Plaits - the same engine the "
               "disting EX uses. Banks (left list) are DX7 32-voice SysEx files from the card's <i>FMSYX</i> folder; the right list shows the "
               "bank's voices by name. The TIMBRE buttons choose which timbre the lists set; timbres 2-4 can follow timbre 1."))
    s += B(["<b>T1 … T4</b>: output, gain, pan, <i>Brightness</i> (modulator depth) and <i>Env scale</i> (envelope times) per timbre.",
            "<b>VOICE</b>: max voices, unison, master gain, velocity → brightness, voice transpose and output mode. "
            "<b>CHORD</b>, <b>FX</b> (chorus, delay), <b>KEYS</b>.",
            "Parameters 54-64 are not listed in the module's manual; EX-GF uses them for the voice and effect extras."])
    s.append(H2("EDITING WITH A DX7 EDITOR"))
    s.append(P("Point a DX7 editor/librarian (Dexed, Patch Base, MIDI Quest …) at the plug-in. Single-voice dumps, parameter edits and dump "
               "requests apply to the timbre chosen by <i>SysEx target</i>; the display then shows <b>VOICE EDITED</b>. From MENU → Poly FM: "
               "<i>Write voice</i> stores the edit in the bank (in memory), <i>Write bank to SD</i> saves it, <i>Abandon edit</i> reverts, and "
               "<i>Send</i> dumps the voice or bank back to the editor. <i>Write built-in to SD</i> creates FMSYX with the built-in banks."))
    s.append(CondPageBreak(60 * mm))
    s.append(H2("PARAMETERS"))
    s.append(param_table(alg(23)))

    # ---------------- Poly Exciter
    s.append(PageBreak())
    s.append(H1("EX POLY EXCITER"))
    s.append(shot("ex"))
    s.append(Spacer(1, 6))
    s.append(P("A 16-voice synthesizer that excites a resonating string with a short burst of sound - plucked, struck, scraped or bowed. "
               "The left list holds exciter tables (<i>Internal</i>, or tables from the card's <i>exciters</i> folder); the right list shows "
               "the exciters in the table. Drum hits make great exciters: a kick for a soft tone, a snare for a sharp, noisy attack."))
    s.append(table([
        ["Mode", "Behaviour"],
        ["Strike", "The exciter plays once, for the whole Exciter length."],
        ["Scrape", "As Strike, but the exciter stops when the key is released."],
        ["Strum", "The exciter loops while the key is held; its level follows <b>pressure</b> (aftertouch or the Pressure parameter) - silent without it."],
    ], [25 * mm, 145 * mm]))
    s.append(Spacer(1, 4))
    s.append(table([
        ["Internal exciter", "Sound"],
        ["0 Ramp", "A decaying linear ramp."], ["1 Noise burst", "White noise at a constant level."],
        ["2 Noise ramp", "Noise shaped by a decaying ramp."], ["3 Square", "Constant level: strong plucks at the start and end."],
        ["4 Silence", "For processing external audio only."],
    ], [35 * mm, 135 * mm]))
    s.append(Spacer(1, 4))
    s += B(["<b>EXCITER</b> mode, exciter, length, pressure, <i>Ext exciter input</i> (feed audio into the plug-in to excite the strings live).",
            "<b>RESONATOR</b> tone (brightness), damping, extra damping on release, low-pass filter. <b>VELO</b> velocity/pitch/pressure modulation. "
            "<b>ENV/LFO</b> modulate pitch and exciter choice. <b>CHORD</b>, <b>FX</b>, <b>KEYS</b>."])
    s.append(CondPageBreak(60 * mm))
    s.append(H2("PARAMETERS"))
    s.append(param_table(alg(13)))

    # ---------------- troubleshooting
    s.append(PageBreak())
    s.append(H1("TROUBLESHOOTING"))
    s.append(table([
        ["Problem", "Fix"],
        ["macOS says the app is damaged or from an unidentified developer; the DAW skips the plug-ins",
         "The builds are not notarised. Run the <font face='Courier'>xattr -dr com.apple.quarantine …</font> command from the install chapter, then rescan plug-ins."],
        ["No sound from EX Multisample", "Check the SD folder has sample folders (SD → Show in Finder) and that a folder is selected in the left list."],
        ["Notes hang", "Press PANIC (or MIDI → All notes off)."],
        ["The DAW does not see Out 3/4", "Enable the plug-in's second output bus in the DAW's routing/IO settings."],
        ["Auto-sampled files start late or early", "Run TEST and set AS Latency just below the measured value."],
        ["Large folders take a moment to load", "Samples load into memory in the background; very large libraries (hundreds of MB) take a few seconds."],
        ["A wavetable change does nothing", "On the display, Wavetable needs confirming (PUSH V): press Esc, or choose the table in the list instead."],
    ], [60 * mm, 110 * mm]))

    # ---------------- credits
    s.append(H1("CREDITS AND LICENCE"))
    s += B(["EX-GF is open source under the <b>GNU AGPL-3.0</b>; source code at github.com/femifleming/ex-gf.",
            "Built with JUCE 9 (AGPLv3). Poly FM uses the FM engine of Mutable Instruments Plaits by Émilie Gillet (MIT).",
            "Behaviour follows the disting EX user manual by Expert Sleepers. Not affiliated with Expert Sleepers.",
            "Sample library: VSCO-2 Community Edition, Karoryfer (Emily Guitar, Growlybass, Hadzi-Fia), Virtuosity Drums, AKWF wavetables, "
            "Greg Sullivan CP80 (CC BY 3.0) and generated content - see CREDITS.txt in the library for details."])
    return s


def build():
    # two passes so the contents page knows page numbers
    doc = Doc(OUT)
    doc.toc_prev = [("INTRODUCTION", 3)] * 9
    doc.build(story(doc))
    first = list(doc.toc)
    doc = Doc(OUT)
    doc.toc_prev = first
    doc.build(story(doc))
    print("wrote", OUT, "-", len(doc.toc), "chapters")


build()

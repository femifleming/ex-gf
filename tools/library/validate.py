"""Validate the virtual SD card against the disting-EX conventions.
Multisample name rule: split the basename (no extension) on '_'; the first token is free text, every
later token must be one of  <note>  (A-G, optional '#', octave; C4 = MIDI 60; no flats),
V<n>, RR<n>, SW<midinote>. If both V and RR are present, V must come before RR.
Folders without any note token are 'drum' folders (mapped 48 upward in alphabetical order).
Optional: --pitch runs an f0 estimate on each sample and compares with the name."""
import os, re, sys, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exlib
from exlib import OUT

NOTE_RX = re.compile(r"([A-G]#?)(-?\d+)")
PCS = {n: i for i, n in enumerate(exlib.NOTE_NAMES)}
SPECIAL = {"wavetables", "exciters", "FMSYX", "!RECORD", "scl", "kbm", "MTS"}


def parse_name(base):
    toks = base.split("_")
    info = {"note": None, "V": None, "RR": None, "SW": None, "bad": []}
    order = []
    for t in toks[1:]:
        m = NOTE_RX.fullmatch(t)
        if m:
            if info["note"] is not None:
                info["bad"].append("second note token " + t)
            info["note"] = (int(m.group(2)) + 1) * 12 + PCS[m.group(1)]
            order.append("N")
        elif re.fullmatch(r"V\d+", t):
            info["V"] = int(t[1:]); order.append("V")
        elif re.fullmatch(r"RR\d+", t):
            info["RR"] = int(t[2:]); order.append("RR")
        elif re.fullmatch(r"SW\d+", t):
            info["SW"] = int(t[2:]); order.append("SW")
        else:
            info["bad"].append("unrecognised token '%s'" % t)
    if "V" in order and "RR" in order and order.index("V") > order.index("RR"):
        info["bad"].append("RR before V")
    if re.search(r"_[A-G]b-?\d", base):
        info["bad"].append("flat note name")
    return info


def f0_estimate(x, sr):
    """Autocorrelation f0 on a 0.25 s window starting 0.15 s in (or at start for short files)."""
    st = min(int(0.15 * sr), max(0, len(x) - int(0.3 * sr)))
    seg = x[st: st + int(0.25 * sr)]
    if len(seg) < 2048:
        return None
    seg = seg - seg.mean()
    seg *= np.hanning(len(seg))
    n = 1 << (2 * len(seg) - 1).bit_length()
    F = np.fft.rfft(seg, n)
    ac = np.fft.irfft(np.abs(F) ** 2, n)[: len(seg)]
    lo, hi = int(sr / 4500), int(sr / 25)
    if ac[0] <= 0:
        return None
    a = ac[lo:hi] / ac[0]
    # pick the first strong peak (>= 0.85 of the max) to avoid octave-down errors
    mx = a.max()
    peaks = [i for i in range(1, len(a) - 1) if a[i] >= a[i - 1] and a[i] >= a[i + 1] and a[i] >= 0.85 * mx]
    if not peaks or mx < 0.3:
        return None
    i = peaks[0]
    y0, y1, y2 = a[i - 1], a[i], a[i + 1]
    d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) != 0 else 0
    return sr / (i + lo + d)


def check_multisample(folder, pitch=False):
    files = sorted(f for f in os.listdir(folder) if not f.startswith("."))
    wavs = [f for f in files if f.lower().endswith(".wav")]
    problems = [f + ": not a wav" for f in files if f not in wavs]
    notes = collections.defaultdict(lambda: collections.defaultdict(set))
    fmts = collections.Counter()
    nloop = 0
    pitch_dev = []
    drum = True
    for f in wavs:
        base = f[:-4]
        info = parse_name(base)
        for b in info["bad"]:
            problems.append("%s: %s" % (f, b))
        try:
            w = exlib.read_wav(os.path.join(folder, f))
        except Exception as e:
            problems.append("%s: unreadable (%s)" % (f, e)); continue
        if w.bits not in (16, 24) or w.channels not in (1, 2) or w.sr not in (44100, 48000):
            problems.append("%s: format %d-bit %dch %d Hz" % (f, w.bits, w.channels, w.sr))
        fmts["%dk/%db/%s" % (w.sr // 1000 if w.sr % 1000 == 0 else w.sr / 1000, w.bits, "st" if w.channels == 2 else "mono")] += 1
        if w.smpl and w.smpl["loops"]:
            nloop += 1
            for s, e in w.smpl["loops"]:
                if not (0 <= s < e < w.frames):
                    problems.append("%s: loop %d-%d outside %d frames" % (f, s, e, w.frames))
        if info["note"] is not None:
            drum = False
            notes[info["note"]]["V"].add(info["V"] or 1)
            notes[info["note"]]["RR"].add(info["RR"] or 1)
            if pitch:
                f0 = f0_estimate(w.float().mean(axis=1), w.sr)
                if f0:
                    m = 69 + 12 * np.log2(f0 / 440.0)
                    pitch_dev.append((f, info["note"], m - info["note"]))
    size = sum(os.path.getsize(os.path.join(folder, f)) for f in files)
    if drum:
        summary = "DRUM folder: %d samples -> MIDI 48..%d (alphabetical)" % (len(wavs), 47 + len(wavs))
    else:
        ks = sorted(notes)
        vmax = max(max(v["V"]) for v in notes.values())
        rmax = max(max(v["RR"]) for v in notes.values())
        partial = sum(1 for v in notes.values() if len(v["V"]) < vmax)
        summary = "%d samples, %d notes %s(%d)..%s(%d), velocity layers %d%s, round robins %d" % (
            len(wavs), len(ks), exlib.note_name(ks[0]), ks[0], exlib.note_name(ks[-1]), ks[-1], vmax,
            " (%d notes with fewer)" % partial if partial else "", rmax)
        if any(f[:-4].count("_") == 0 for f in wavs):
            problems.append("mixture of named and un-named samples")
    summary += " | %s | loops in %d | %.1f MB" % (", ".join("%s x%d" % kv for kv in fmts.items()), nloop, size / 1e6)
    return summary, problems, pitch_dev


def check_wavetables(root):
    out, probs = [], []
    for d in sorted(os.listdir(root)):
        p = os.path.join(root, d)
        if d.startswith("."):
            continue
        if os.path.isdir(p):
            fs = sorted(f for f in os.listdir(p) if f.lower().endswith(".wav"))
            for f in fs:
                w = exlib.read_wav(os.path.join(p, f))
                if w.channels != 1 or w.bits != 16:
                    probs.append("%s/%s: %dch %d-bit" % (d, f, w.channels, w.bits))
            out.append(len(fs))
        elif d.lower().endswith(".wav"):
            w = exlib.read_wav(p)
            if w.frames % 600:
                probs.append("%s: %d frames not a multiple of 600" % (d, w.frames))
            out.append(w.frames // 600)
    return "%d tables, %d..%d waves each, %d waves total" % (len(out), min(out), max(out), sum(out)), probs


def check_exciters(root):
    lines, probs = [], []
    for d in sorted(os.listdir(root)):
        p = os.path.join(root, d)
        if d.startswith("."):
            continue
        if os.path.isdir(p):
            fs = sorted(f for f in os.listdir(p) if f.lower().endswith(".wav"))
            lens = []
            for f in fs:
                w = exlib.read_wav(os.path.join(p, f))
                lens.append(w.frames)
                if w.channels != 1:
                    probs.append("%s/%s: not mono" % (d, f))
                if w.frames > 28672:
                    probs.append("%s/%s: %d frames > 28672" % (d, f, w.frames))
            if len(fs) > 128:
                probs.append("%s: %d files > 128" % (d, len(fs)))
            lines.append("  %-22s folder, %3d exciters, %d..%d frames" % (d, len(fs), min(lens), max(lens)))
        elif d.lower().endswith(".wav"):
            w = exlib.read_wav(p)
            if w.channels != 1:
                probs.append(d + ": not mono")
            if w.frames % 128:
                probs.append("%s: %d frames not divisible by 128" % (d, w.frames))
            if w.frames // 128 > 28672:
                probs.append("%s: exciter length > 28672" % d)
            lines.append("  %-22s single file, 128 x %d frames" % (d, w.frames // 128))
    return lines, probs


def check_syx(root):
    probs, n = [], 0
    for f in sorted(os.listdir(root)):
        if f.startswith("."):
            continue
        b = open(os.path.join(root, f), "rb").read()
        n += 1
        data = b[6:4102]
        ok = (len(b) == 4104 and b[:6] == bytes([0xF0, 0x43, 0, 9, 0x20, 0]) and b[-1] == 0xF7
              and max(data) < 128 and ((-sum(data)) & 0x7F) == b[4102])
        if not ok:
            probs.append(f + ": invalid DX7 bank")
    return n, probs


def main():
    pitch = "--pitch" in sys.argv
    allprob = []
    print("== Multisample folders")
    devs_all = []
    for d in sorted(os.listdir(OUT)):
        p = os.path.join(OUT, d)
        if not os.path.isdir(p) or d in SPECIAL or d.startswith("."):
            continue
        s, probs, devs = check_multisample(p, pitch)
        print("%-24s %s" % (d, s))
        for x in probs:
            print("   PROBLEM:", x)
        allprob += probs
        if pitch and devs:
            dv = np.array([x[2] for x in devs])
            octs = np.round(dv / 12)
            cents = (dv - 12 * octs) * 100
            near = np.abs(cents) < 60
            print("   pitch check: %d/%d estimates within 60 cents of named pitch class; octave offsets seen: %s; median |dev| %.0f c" % (
                near.sum(), len(dv), dict(collections.Counter(octs.astype(int).tolist())), np.median(np.abs(cents))))
            devs_all.append((d, devs))
    if os.path.isdir(os.path.join(OUT, "wavetables")):
        s, probs = check_wavetables(os.path.join(OUT, "wavetables"))
        print("== wavetables:", s)
        for x in probs:
            print("   PROBLEM:", x)
        allprob += probs
    if os.path.isdir(os.path.join(OUT, "exciters")):
        lines, probs = check_exciters(os.path.join(OUT, "exciters"))
        print("== exciters:")
        print("\n".join(lines))
        for x in probs:
            print("   PROBLEM:", x)
        allprob += probs
    if os.path.isdir(os.path.join(OUT, "FMSYX")):
        n, probs = check_syx(os.path.join(OUT, "FMSYX"))
        print("== FMSYX: %d banks checked (size 4104, header, checksum, F7)" % n)
        for x in probs:
            print("   PROBLEM:", x)
        allprob += probs
    print("== TOTAL PROBLEMS:", len(allprob))
    if pitch and "--detail" in sys.argv:
        for d, devs in devs_all:
            for f, n, dv in devs:
                if abs(dv - 12 * round(dv / 12)) > 0.6 or round(dv / 12) != 0:
                    print("   %s/%s named %d est %+.2f st" % (d, f, n, dv))


if __name__ == "__main__":
    main()

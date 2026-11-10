"""Create !RECORD, MTS, scl, kbm folders and hand-written Scala/kbm files; validate them."""
import os, sys
from fractions import Fraction
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from exlib import OUT

for d in ("!RECORD", "scl", "kbm", "MTS"):
    os.makedirs(os.path.join(OUT, d), exist_ok=True)

SCL = {
    "12-TET.scl": ("12-tone equal temperament", ["%.5f" % (100.0 * i) for i in range(1, 12)] + ["2/1"]),
    "7-TET.scl": ("7-tone equal temperament", ["%.5f" % (1200.0 * i / 7) for i in range(1, 7)] + ["2/1"]),
    "JI-major.scl": ("Ptolemy's intense diatonic (5-limit just major scale)",
                     ["9/8", "5/4", "4/3", "3/2", "5/3", "15/8", "2/1"]),
    "JI-12-5limit.scl": ("5-limit just intonation chromatic scale (12 notes, on C)",
                         ["16/15", "9/8", "6/5", "5/4", "4/3", "45/32", "3/2", "8/5", "5/3", "9/5", "15/8", "2/1"]),
}
for fn, (desc, pitches) in SCL.items():
    lines = ["! " + fn, "!", desc, " %d" % len(pitches), "!"] + [" " + p for p in pitches]
    open(os.path.join(OUT, "scl", fn), "w", newline="\n").write("\n".join(lines) + "\n")


def kbm(fn, comment, size, first, last, middle, ref, freq, octave, mapping):
    L = ["! " + fn, "! " + comment,
         "! Size of map. The pattern repeats every so many keys:", str(size),
         "! First MIDI note number to retune:", str(first),
         "! Last MIDI note number to retune:", str(last),
         "! Middle note where the first entry of the mapping is mapped to:", str(middle),
         "! Reference note for which frequency is given:", str(ref),
         "! Frequency to tune the above note to (floating point e.g. 440.0):", "%.6f" % freq,
         "! Scale degree to consider as formal octave (determines difference in pitch",
         "! between adjacent mapping patterns):", str(octave),
         "! Mapping.", "! The numbers represent scale degrees mapped to keys. The first entry is for",
         "! the given middle note, the next for subsequent higher keys.",
         "! For an unmapped key, put in an \"x\". At the end, unmapped keys may be left out."]
    L += [str(m) for m in mapping]
    open(os.path.join(OUT, "kbm", fn), "w", newline="\n").write("\n".join(L) + "\n")


kbm("12-TET-identity.kbm", "Identity mapping for 12-note scales: C4 (60) = degree 0, A4 (69) = 440 Hz",
    12, 0, 127, 60, 69, 440.0, 12, range(12))
kbm("7-note-linear-C4.kbm", "Linear mapping for 7-note scales (every key is the next degree), C4 = 261.6256 Hz",
    7, 0, 127, 60, 60, 261.625565, 7, range(7))


# --- validate: re-parse each scl and kbm
def parse_scl(p):
    ls = [l.strip() for l in open(p) if not l.startswith("!")]
    desc, n, rest = ls[0], int(ls[1].split()[0]), ls[2:]
    vals = []
    for t in rest[:n]:
        t = t.split()[0]
        vals.append(float(t) if "." in t else 1200 * __import__("math").log2(float(Fraction(t))))
    assert len(vals) == n and all(b > a for a, b in zip(vals, vals[1:])), p
    return desc, n, vals


for fn in sorted(os.listdir(os.path.join(OUT, "scl"))):
    d, n, v = parse_scl(os.path.join(OUT, "scl", fn))
    print("scl %-18s %2d notes  last=%.1f cents  first=%.2f" % (fn, n, v[-1], v[0]))
for fn in sorted(os.listdir(os.path.join(OUT, "kbm"))):
    vals = [l.strip() for l in open(os.path.join(OUT, "kbm", fn)) if not l.startswith("!") and l.strip()]
    size = int(vals[0])
    assert len(vals) == 7 + size, (fn, len(vals))
    print("kbm %-22s size=%d first=%s last=%s middle=%s ref=%s freq=%s octave=%s map=%s" % (fn, size, *vals[1:7], ",".join(vals[7:])))

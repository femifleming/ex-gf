"""Drum kit folder (Virtuosity Drums, CC0) and a generated looped 'Saw Pad' multisample."""
import os, sys, re, json, shutil, subprocess, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exlib
from exlib import DL, OUT
from build_multi import trim

VIRT = os.path.join(DL, "virt")
TMP = os.path.join(DL, "tmp_build")

# output name -> regex matched against virtuosity file paths (first sorted match wins)
KIT = [
    ("01-Kick", r"Samples/mid/kick/mid_kick_snoff_vl4_rr1\."),
    ("02-Kick-Soft", r"Samples/mid/kick/mid_kick_snoff_vl2_rr1\."),
    ("03-Snare", r"Samples/mid/snare/mid_snare_center_vl30\."),
    ("04-Snare-Soft", r"Samples/mid/snare/mid_snare_center_vl12\."),
    ("05-Rimshot", r"Samples/mid/snare/mid_snare_rimshot_vl10\."),
    ("06-Crossstick", r"Samples/mid/snare/mid_snare_crossstick_vl12\."),
    ("07-HiHat-Closed", r"Samples/mid/hh/mid_hh_closed_vl3_rr1\."),
    ("08-HiHat-Pedal", r"Samples/mid/hh/mid_hh_pedal_vl3_rr1\."),
    ("09-HiHat-Half", r"Samples/mid/hh/mid_hh_half_vl3_rr1\."),
    ("10-HiHat-Open", r"Samples/mid/hh/mid_hh_open_vl3_rr1\."),
    ("11-Tom-High", r"Samples/mid/htom/mid_htom_center_vl12\."),
    ("12-Tom-Low", r"Samples/mid/ltom/mid_ltom_center_vl12\."),
    ("13-Crash", r"Samples/mid/crash/mid_crash_crash_vl3_rr1\."),
    ("14-Ride", r"Samples/mid/ride/mid_ride_ride_vl3_rr1\."),
    ("15-Ride-Bell", r"Samples/mid/ride/mid_ride_bell_vl3_rr1\."),
    ("16-Tambourine", r"Samples/perc/mid/tambourine/Tamb1_Shake_rr1_Mid"),
    ("17-Shaker", r"Samples/perc/mid/shaker/LShaker_Shake1D_rr1_Mid"),
    ("18-Cowbell", r"Samples/perc/mid/cowbell/Cowbell1_Normal_v2_rr1_Mid"),
    ("19-Claves", r"Samples/perc/mid/claves/Claves1_Hit_v2_rr1_Mid"),
    ("20-Conga", r"Samples/perc/mid/conga/Conga_22_HitN_51_100_rr1_Mid"),
    ("21-Bongo-High", r"Samples/perc/mid/bongoh/BongoH_Hit1_v2_rr1_Mid"),
    ("22-Bongo-Low", r"Samples/perc/mid/bongol/BongoL_Hit1_v2_rr1_Mid"),
    ("23-Agogo", r"Samples/perc/mid/agogo/Agogo_High_v2_rr1_Mid"),
    ("24-Triangle", r"Samples/perc/mid/triangle/Triangle1_Hit_v2_rr1_Mid"),
]


def kit_sources():
    t = json.load(open(os.path.join(DL, "tree_virt.json")))
    paths = sorted(x["path"] for x in t["tree"] if x["type"] == "blob")
    out = []
    for name, rx in KIT:
        m = [p for p in paths if re.search(rx, p)]
        if not m:
            raise SystemExit("no match for " + rx)
        out.append((name, m[0]))
    return out


def build_kit():
    src = kit_sources()
    pf = os.path.join(TMP, "virt_paths.txt")
    open(pf, "w").write("\n".join(p for _, p in src) + "\n")
    subprocess.run(["git", "-C", VIRT, "checkout", "-q", "HEAD", "--pathspec-from-file=" + pf], check=True,
                   env=dict(os.environ, GIT_LITERAL_PATHSPECS="1"))
    dest = os.path.join(OUT, "Virtuosity Drum Kit")
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)
    tmp = tempfile.mkdtemp(dir=TMP)
    for name, p in src:
        w = exlib.load_any(os.path.join(VIRT, p), tmp)
        if w.sr not in (44100, 48000):
            w = exlib.resample(w, 48000, tmp)
        w = trim(w, 4.0)
        exlib.write_wav(os.path.join(dest, name + ".wav"), w)
        print("kit", name, "<-", p, w.sr, w.bits, w.channels, round(w.frames / w.sr, 2))
    shutil.rmtree(tmp)


def polyblep_saw(f, n, sr, phase0):
    dt = f / sr
    ph = (phase0 + dt * np.arange(n)) % 1.0
    y = 2 * ph - 1
    t = ph
    m1 = t < dt
    x = t[m1] / dt
    y[m1] -= x + x - x * x - 1
    m2 = t > 1 - dt
    x = (t[m2] - 1) / dt
    y[m2] -= x * x + x + x + 1
    return y


def build_pad():
    sr = 48000
    dest = os.path.join(OUT, "Saw Pad")
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)
    rng = np.random.default_rng(1234)
    total, ls, le, xf = int(4.2 * sr), int(1.2 * sr), int(4.0 * sr), int(0.4 * sr)
    for midi in range(24, 97, 3):  # C1 .. C7, every minor third
        f0 = 440 * 2 ** ((midi - 69) / 12)
        L = np.zeros(total); R = np.zeros(total)
        det = np.linspace(-14, 14, 7)
        for i, c in enumerate(det):
            s = polyblep_saw(f0 * 2 ** (c / 1200), total, sr, rng.random())
            pan = 0.5 + 0.45 * np.sin(i * 2.1)
            L += s * np.sqrt(1 - pan); R += s * np.sqrt(pan)
        # gentle low-pass (frequency domain, 12 dB/oct-ish) keyed to pitch
        fc = min(18000, 2200 + 3 * f0)
        for ch in (L, R):
            S = np.fft.rfft(ch)
            fr = np.fft.rfftfreq(total, 1 / sr)
            S /= 1 + (fr / fc) ** 2
            ch[:] = np.fft.irfft(S, total)
        y = np.stack([L, R], 1)
        # loop crossfade: blend the end of the loop with audio just before loop start
        a = np.linspace(0, 1, xf)[:, None]
        y[le - xf:le] = y[le - xf:le] * (1 - a) + y[ls - xf:ls] * a
        y = y[:le]
        env = np.minimum(1, np.arange(le) / (0.5 * sr))[:, None]
        y *= env
        y *= 0.5 / np.max(np.abs(y))
        w = exlib.from_float(y, sr, 16, smpl={"unity": midi, "loops": [(ls, le - 1)]})
        exlib.write_wav(os.path.join(dest, "Saw-Pad_%s.wav" % exlib.note_name(midi)), w, unity=midi)
    print("pad done")


if __name__ == "__main__":
    what = sys.argv[1:] or ["kit", "pad"]
    if "kit" in what:
        build_kit()
    if "pad" in what:
        build_pad()

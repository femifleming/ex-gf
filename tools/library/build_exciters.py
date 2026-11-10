"""exciters/: drum hits, instrument attack transients, numpy noise bursts, clicks, multiplucks.wav."""
import os, sys, re, json, shutil, subprocess, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exlib
from exlib import DL, OUT

SR = 48000
MAXF = 28672
ROOT = os.path.join(OUT, "exciters")
TMP = os.path.join(DL, "tmp_build")
VIRT = os.path.join(DL, "virt")
rng = np.random.default_rng(20261110)


def to48_mono(path, tmp):
    w = exlib.load_any(path, tmp)
    if w.sr != SR:
        w = exlib.resample(w, SR, tmp)
    return w.float().mean(axis=1)


def finish(x, n_max=MAXF, fade=0.2):
    x = np.asarray(x, dtype=np.float64)[:n_max].copy()
    fl = max(16, int(len(x) * fade))
    x[-fl:] *= np.cos(np.linspace(0, np.pi / 2, fl)) ** 2
    pk = np.max(np.abs(x))
    if pk > 0:
        x *= 0.89 / pk  # -1 dBFS
    return x


def onset(x, rel_db=-36):
    pk = np.max(np.abs(x))
    i = np.argmax(np.abs(x) > pk * 10 ** (rel_db / 20))
    return max(0, i - int(0.002 * SR))


def save(folder, name, x):
    exlib.write_wav(os.path.join(ROOT, folder, name + ".wav"), exlib.from_float(x, SR, 16))


# ------------------------------------------------------------------ drums
DRUM_RX = [
    ("kick-hard", r"mid/kick/mid_kick_snoff_vl4_rr1\."), ("kick-med", r"mid/kick/mid_kick_snoff_vl3_rr2\."),
    ("kick-soft", r"mid/kick/mid_kick_snoff_vl2_rr1\."), ("kick-snares-on", r"mid/kick/mid_kick_snon_vl4_rr1\."),
    ("snare-hard", r"mid/snare/mid_snare_center_vl36\."), ("snare-med", r"mid/snare/mid_snare_center_vl30\."),
    ("snare-soft", r"mid/snare/mid_snare_center_vl12\."), ("snare-ghost", r"mid/snare/mid_snare_center_vl3\."),
    ("snare-offcenter", r"mid/snare/mid_snare_offcenter_vl30\."), ("snare-rimshot", r"mid/snare/mid_snare_rimshot_vl10\."),
    ("snare-crossstick", r"mid/snare/mid_snare_crossstick_vl12\."), ("snare-muted", r"mid/snare/mid_snare_muted_vl9\."),
    ("snare-flam", r"mid/snare/mid_snare_flam_vl9\."), ("snare-off-center", r"mid/snareoff/mid_snareoff_center_vl9\."),
    ("snare-off-stick", r"mid/snareoff/mid_snareoff_stickshot2_vl9\."),
    ("hat-closed-hard", r"mid/hh/mid_hh_closed_vl4_rr1\."), ("hat-closed-med", r"mid/hh/mid_hh_closed_vl3_rr1\."),
    ("hat-closed-soft", r"mid/hh/mid_hh_closed_vl1_rr1\."), ("hat-pedal", r"mid/hh/mid_hh_pedal_vl3_rr1\."),
    ("hat-half", r"mid/hh/mid_hh_half_vl3_rr1\."), ("hat-open", r"mid/hh/mid_hh_open_vl3_rr1\."),
    ("tom-high", r"mid/htom/mid_htom_center_vl12\."), ("tom-high-soft", r"mid/htom/mid_htom_center_vl4\."),
    ("tom-high-offcenter", r"mid/htom/mid_htom_offcenter_vl12\."), ("tom-low", r"mid/ltom/mid_ltom_center_vl12\."),
    ("tom-low-soft", r"mid/ltom/mid_ltom_center_vl4\."), ("crash", r"mid/crash/mid_crash_crash_vl3_rr1\."),
    ("ride", r"mid/ride/mid_ride_ride_vl3_rr1\."), ("ride-bell", r"mid/ride/mid_ride_bell_vl3_rr1\."),
    ("flatride", r"mid/flatride/mid_flatride_ride_vl3_rr1\."),
    ("tambourine", r"perc/mid/tambourine/Tamb1_Shake_rr1_Mid"), ("shaker", r"perc/mid/shaker/LShaker_Shake1D_rr1_Mid"),
    ("cowbell", r"perc/mid/cowbell/Cowbell1_Normal_v2_rr1_Mid"), ("claves", r"perc/mid/claves/Claves1_Hit_v2_rr1_Mid"),
    ("conga", r"perc/mid/conga/Conga_22_HitN_51_100_rr1_Mid"), ("conga-slap", r"perc/mid/conga/Conga_17_HitHM1_71_127_rr1_Mid"),
    ("tumba", r"perc/mid/tumba/Tumba_24_HitN_71_100_rr1_Mid"), ("bongo-high", r"perc/mid/bongoh/BongoH_Hit1_v2_rr1_Mid"),
    ("bongo-low", r"perc/mid/bongol/BongoL_Hit1_v2_rr1_Mid"), ("agogo-high", r"perc/mid/agogo/Agogo_High_v2_rr1_Mid"),
    ("agogo-low", r"perc/mid/agogo/Agogo_Low_v2_rr1_Mid"), ("triangle", r"perc/mid/triangle/Triangle1_Hit_v2_rr1_Mid"),
    ("triangle-muted", r"perc/mid/triangle/Triangle1_HitM_v1_rr1_Mid"), ("cabasa", r"perc/mid/cabasa/Cabasa1_Rub_v2_rr1_Mid"),
    ("sleighbells", r"perc/mid/sleighbells/Sleighbells_Hit_rr1_Mid"), ("vibraslap", r"perc/mid/vibraslap/Vibraslap1_1_Hit_71_127_rr1_Mid"),
    ("woodblock", r"perc/oh/woodblock/.*rr1"), ("timbale", r"perc/oh/timbales/.*rr1"),
]


def build_drums(tmp):
    t = json.load(open(os.path.join(DL, "tree_virt.json")))
    paths = sorted(x["path"] for x in t["tree"] if x["type"] == "blob" and x["path"].startswith("Samples/"))
    picks = []
    for name, rx in DRUM_RX:
        m = [p for p in paths if re.search(rx, p)]
        if m:
            picks.append((name, m[0]))
        else:
            print("  (drum exciter not found:", rx, ")")
    pf = os.path.join(TMP, "virt_exc.txt")
    open(pf, "w").write("\n".join(p for _, p in picks) + "\n")
    subprocess.run(["git", "-C", VIRT, "checkout", "-q", "HEAD", "--pathspec-from-file=" + pf], check=True,
                   env=dict(os.environ, GIT_LITERAL_PATHSPECS="1"))
    for i, (name, p) in enumerate(picks):
        x = to48_mono(os.path.join(VIRT, p), tmp)
        x = x[onset(x):]
        above = np.nonzero(np.abs(x) > np.max(np.abs(x)) * 10 ** (-54 / 20))[0]
        x = x[: above[-1] + int(0.01 * SR)]
        save("drums", "%03d-%s" % (i + 1, name), finish(x, MAXF, 0.25))
    return len(picks)


# ------------------------------------------------------------------ instrument transients
TRANS_SRC = [  # (folder, filename regex) — taken from the instrument folders already built
    ("VSCO Upright Piano", r"_(A0|C#2|F3|A4|C#6|F7)_V3"), ("VSCO Upright Piano", r"_(C#3|A5)_V1"),
    ("CP80 Electric Grand", r"_V3\.wav$"), ("VSCO Harp", r"_(E1|C3|D4|E5|F6|D7)\.wav"),
    ("Emily Guitar", r"_(E2|A3|E4|C6)_V3_RR1"), ("Emily Guitar", r"_(C#2|G3)_V1_RR1"),
    ("Growlybass", r"_V3_RR1"), ("VSCO Cello Pizz", r"_(C2|F3|D5)_V2_RR1"),
    ("VSCO Marimba", r"\.wav$"), ("VSCO Glockenspiel", r"\.wav$"),
    ("VSCO Trumpet", r"_(F3|C6)_V2"), ("VSCO French Horn", r"_(F3|C5)_V3"), ("VSCO Clarinet", r"_(D3|F6)_V3"),
    ("Virtuosity Drum Kit", r"(Claves|Cowbell|Agogo|Triangle)"),
]


def build_transients(tmp):
    out = []
    n = int(0.150 * SR)
    for folder, rx in TRANS_SRC:
        d = os.path.join(OUT, folder)
        for f in sorted(os.listdir(d)):
            if not re.search(rx, f):
                continue
            x = to48_mono(os.path.join(d, f), tmp)
            x = x[onset(x):][:n]
            if len(x) < n // 2:
                continue
            tag = re.sub(r"[^A-Za-z0-9#]+", "-", f[:-4]).strip("-")
            out.append((tag, x))
    for i, (tag, x) in enumerate(out):
        # 150 ms with a short (25 ms) fade-out
        y = finish(x, n, 0.0)
        fl = int(0.025 * SR)
        y[-fl:] *= np.cos(np.linspace(0, np.pi / 2, fl)) ** 2
        save("plucks-mallets", "%03d-%s" % (i + 1, tag), y)
    return out


# ------------------------------------------------------------------ noise
def coloured(n, slope_db_oct):
    X = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / SR)
    f[0] = f[1]
    X *= (f / 1000.0) ** (slope_db_oct / 6.0206)
    return np.fft.irfft(X, n)


def bandpass(x, fc, q):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    X *= 1 / (1 + (q * (f / fc - fc / np.maximum(f, 1))) ** 2) ** 0.5
    return np.fft.irfft(X, len(x))


def build_noise():
    colours = [("white", 0), ("pink", -3), ("brown", -6), ("blue", 3), ("violet", 6)]
    lengths_ms = [2, 5, 10, 25, 50, 100, 250, 500]
    items = []
    for cname, slope in colours:
        for L in lengths_ms:
            n = max(32, int(L / 1000 * SR))
            x = coloured(n + 64, slope)[:n]
            env = np.exp(-np.arange(n) / (n / 4.0))
            items.append(("%s-%04dms" % (cname, L), x * env))
    for fc in (150, 400, 1000, 2500, 6000, 12000):
        for L in (20, 120, 400):
            n = int(L / 1000 * SR)
            x = bandpass(rng.standard_normal(n), fc, 4.0)
            env = np.exp(-np.arange(n) / (n / 5.0)) * np.minimum(1, np.arange(n) / 48)
            items.append(("band-%05dHz-%03dms" % (fc, L), x * env))
    for L in (100, 300, 590):  # swells / reverse-envelope bursts
        n = int(L / 1000 * SR)
        x = coloured(n, -3) * (np.arange(n) / n) ** 2
        items.append(("pink-swell-%03dms" % L, x))
    for i, (name, x) in enumerate(items):
        save("noise-bursts", "%03d-%s" % (i + 1, name), finish(x, MAXF, 0.1))
    return items


# ------------------------------------------------------------------ clicks / impulses
def build_clicks():
    items = []
    n = 256
    x = np.zeros(n); x[2] = 1; items.append(("impulse-1sample", x))
    x = np.zeros(n); x[2] = 1; x[3] = -1; items.append(("doublet", x))
    x = np.zeros(n); x[2:6] = 1; items.append(("pulse-4samples", x))
    x = np.zeros(n); x[2:26] = 1; items.append(("pulse-24samples", x))
    for w_ms in (0.05, 0.1, 0.25, 0.5, 1, 2, 5):
        w = max(2, int(w_ms / 1000 * SR))
        x = np.zeros(w + 64); x[2:2 + w] = np.hanning(w + 2)[1:-1]; items.append(("raised-cosine-%gms" % w_ms, x))
    for fc in (1000, 3000, 8000, 16000):  # band-limited impulses (windowed sinc)
        k = np.arange(-64, 65)
        x = np.sinc(2 * fc / SR * k) * np.hanning(len(k)); items.append(("sinc-%05dHz" % fc, x))
    for f in (200, 800, 2000, 5000, 9000):
        for d_ms in (2, 15):
            nn = int(d_ms * 6 / 1000 * SR)
            t = np.arange(nn) / SR
            items.append(("ping-%04dHz-%02dms" % (f, d_ms), np.sin(2 * np.pi * f * t) * np.exp(-t / (d_ms / 1000))))
    for f0, f1 in ((8000, 100), (100, 8000), (3000, 300)):
        nn = int(0.01 * SR); t = np.arange(nn) / SR
        ph = 2 * np.pi * np.cumsum(np.geomspace(f0, f1, nn)) / SR
        items.append(("chirp-%05d-%05dHz" % (f0, f1), np.sin(ph) * np.hanning(nn)))
    x = np.zeros(int(0.03 * SR))
    for k in range(5):
        x[int(k * 0.006 * SR) + 2] = (-1) ** k * 0.8 ** k
    items.append(("click-train-5", x))
    nn = int(0.004 * SR); items.append(("noise-click-4ms", rng.standard_normal(nn) * np.exp(-np.arange(nn) / (nn / 6))))
    nn = int(0.001 * SR); items.append(("noise-click-1ms", rng.standard_normal(nn) * np.hanning(nn)))
    for i, (name, x) in enumerate(items):
        x = np.concatenate([x, np.zeros(32)])
        pk = np.max(np.abs(x)); x = x * (0.89 / pk)
        save("clicks-impulses", "%03d-%s" % (i + 1, name), x)
    return items


# ------------------------------------------------------------------ multiplucks.wav
def centroid(x):
    S = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    f = np.fft.rfftfreq(len(x), 1 / SR)
    return float((S * f).sum() / (S.sum() + 1e-12))


def build_multiplucks(trans, noise):
    L = 4096
    pool = []
    for tag, x in trans:
        pool.append(finish(x[:L], L, 0.3))
    k = 0
    while len(pool) < 128:  # pad with short filtered-noise plucks (Karplus-style excitations)
        fc = float(np.geomspace(200, 12000, 64)[k % 64])
        n = int(np.geomspace(64, L, 7)[k % 7])
        x = np.zeros(L)
        x[:n] = bandpass(rng.standard_normal(n), fc, 1.5) * np.exp(-np.arange(n) / (n / 4))
        pool.append(finish(x, L, 0.05))
        k += 1
    pool = pool[:128]
    pool.sort(key=centroid)  # dark -> bright, so scanning the table morphs sensibly
    y = np.concatenate(pool)
    assert len(y) == 128 * L
    exlib.write_wav(os.path.join(ROOT, "multiplucks.wav"), exlib.from_float(y, SR, 16))
    return L


if __name__ == "__main__":
    if os.path.isdir(ROOT):
        shutil.rmtree(ROOT)
    os.makedirs(ROOT)
    tmp = tempfile.mkdtemp(dir=TMP)
    nd = build_drums(tmp)
    trans = build_transients(tmp)
    noise = build_noise()
    clicks = build_clicks()
    L = build_multiplucks(trans, noise)
    shutil.rmtree(tmp)
    print("drums %d, transients %d, noise %d, clicks %d, multiplucks 128 x %d" % (nd, len(trans), len(noise), len(clicks), L))

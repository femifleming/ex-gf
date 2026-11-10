"""wavetables/: one folder per AKWF category, <=64 single-cycle 16-bit mono 600-frame waves."""
import os, sys, shutil
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exlib
from exlib import DL, OUT

SRC = os.path.join(DL, "akwf", "AKWF")
CATS = """bw_saw bw_sawbright bw_sawgap bw_sawrounded bw_squ bw_squrounded bw_tri bw_sin bw_blended
aguitar altosax bitreduced cello clarinett clavinet dbass distorted ebass eguitar eorgan epiano flute
fmsynth granular hdrawn hvoice oboe oscchip overtone piano raw sinharm theremin vgame vgamebasic violin
0001 0010""".split()
dest_root = os.path.join(OUT, "wavetables")
if os.path.isdir(dest_root):
    shutil.rmtree(dest_root)
summary = []
for c in CATS:
    src = os.path.join(SRC, "AKWF_" + c)
    files = sorted(f for f in os.listdir(src) if f.lower().endswith(".wav"))
    if len(files) > 64:
        idx = np.unique(np.round(np.linspace(0, len(files) - 1, 64)).astype(int))
        files = [files[i] for i in idx]
    dest = os.path.join(dest_root, "AKWF_" + c)
    os.makedirs(dest)
    conv = 0
    for f in files:
        w = exlib.read_wav(os.path.join(src, f))
        if w.channels == 1 and w.bits == 16 and w.frames == 600:
            shutil.copyfile(os.path.join(src, f), os.path.join(dest, f))
        else:
            x = w.float().mean(axis=1)
            if len(x) != 600:
                x = np.interp(np.linspace(0, len(x), 600, endpoint=False), np.arange(len(x)), x)
            exlib.write_wav(os.path.join(dest, f), exlib.from_float(x, w.sr, 16))
            conv += 1
    summary.append((c, len(files), conv))
for c, n, conv in summary:
    print("AKWF_%-14s %3d waves%s" % (c, n, "  (%d converted)" % conv if conv else ""))
print("categories:", len(summary), "waves:", sum(n for _, n, _ in summary))

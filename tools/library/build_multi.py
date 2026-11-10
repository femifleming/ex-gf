"""Select, fetch and convert multisample instruments into disting-EX naming.
usage: python3 -I build_multi.py plan|fetch|build [instrument-folder ...]"""
import sys, os, re, json, subprocess, shutil, tempfile, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import exlib
from exlib import DL, OUT

V = DL + "/vsco"
VS = DL + "/vsco_sfz"

# name, prefix, repo, sfz (or None), relpath filter regex, min_gap, max_vel, max_rr, max_sec
INSTR = [
    dict(name="VSCO Upright Piano", prefix="Upright-Piano", repo=V, sfz=VS + "/UprightPiano.sfz", gap=3, vel=3, rr=1, sec=10),
    dict(name="CP80 Electric Grand", prefix="CP80", repo=DL + "/gs", sfz=DL + "/gs/CP80/CP80.sfz", relbase="CP80", gap=4, vel=3, rr=1, sec=8,
         velpick=[1, 2, 3]),
    dict(name="VSCO Pipe Organ", prefix="Pipe-Organ", repo=V, sfz=VS + "/OrganLoud.sfz", gap=3, vel=1, rr=1, sec=6),
    dict(name="VSCO Cello Sustain", prefix="Cello-Sus", repo=V, sfz=VS + "/CelloEnsSusVib.sfz", gap=3, vel=2, rr=1, sec=6),
    dict(name="VSCO Cello Pizz", prefix="Cello-Pizz", repo=V, sfz=VS + "/CelloEnsPizz.sfz", gap=3, vel=2, rr=2, sec=3),
    dict(name="VSCO Violins Sustain", prefix="Violins-Sus", repo=V, sfz=VS + "/ViolinEnsSusVib.sfz", gap=3, vel=2, rr=1, sec=6),
    dict(name="VSCO French Horn", prefix="Horn-Sus", repo=V, sfz=VS + "/FHornSus.sfz", gap=3, vel=3, rr=1, sec=6),
    dict(name="VSCO Trumpet", prefix="Trumpet-Sus", repo=V, sfz=VS + "/TrumpetSus.sfz", gap=3, vel=2, rr=1, sec=6),
    dict(name="VSCO Flute", prefix="Flute-Sus", repo=V, sfz=VS + "/FluteSusNV.sfz", gap=3, vel=2, rr=1, sec=6),
    dict(name="VSCO Clarinet", prefix="Clarinet-Sus", repo=V, sfz=VS + "/ClarinetSus.sfz", gap=3, vel=3, rr=1, sec=6),
    dict(name="VSCO Glockenspiel", prefix="Glock", repo=V, sfz=VS + "/Glockenspiel.sfz", transpose=12, gap=1, vel=1, rr=1, sec=4),
    dict(name="VSCO Marimba", prefix="Marimba", repo=V, sfz=VS + "/Marimba.sfz", gap=1, vel=1, rr=1, sec=4),
    dict(name="VSCO Harp", prefix="Harp", repo=V, sfz=VS + "/Harp.sfz", gap=1, vel=1, rr=1, sec=6),
    dict(name="Emily Guitar", prefix="Emily-Guitar", repo=DL + "/karoryfer.emilyguitar", sfz=DL + "/karoryfer.emilyguitar/emily_basic.sfz",
         filt=r"^notes/", gap=3, vel=3, rr=2, sec=5, velpick=[0, 2, 3]),
    dict(name="Growlybass", prefix="Growlybass", repo=DL + "/karoryfer.growlybass", sfz=DL + "/karoryfer.growlybass/growlybass_clean.sfz",
         filt=r"^sustain/", transpose=-12, gap=3, vel=3, rr=2, sec=5, velpick=[0, 2, 3]),
    dict(name="Hadzi-Fia Vocal Ah", prefix="Vocal-Ah", repo=DL + "/legato_vocal_tutorial", sfz=None, transpose=-12, gap=2, vel=1, rr=1, sec=6),
]

TMP = DL + "/tmp_build"


def regions_for(ins):
    """-> list of dict(rel, key, vel(lo,hi), rr_sort)"""
    out = []
    if ins["sfz"]:
        regs = exlib.parse_sfz(ins["sfz"])
        for r in regs:
            if "_relpath" not in r:
                continue
            rel = r["_relpath"]
            if ins.get("relbase"):
                rel = ins["relbase"] + "/" + rel
            if ins.get("filt") and not re.search(ins["filt"], r["_relpath"]):
                continue
            k = exlib.region_key(r)
            if k is None:
                continue
            lv = int(r.get("lovel", 0)); hv = int(r.get("hivel", 127))
            out.append(dict(rel=rel, key=k, vel=(lv, hv),
                            rrs=(int(r.get("seq_position", 1)), float(r.get("lorand", 0)), rel)))
    else:  # vocal: names vowel_a_<note>.wav, scientific (C4 = 60)
        files = subprocess.run(["git", "-C", ins["repo"], "ls-tree", "-r", "--name-only", "HEAD", "Samples/vowel_sustain/a/"],
                               capture_output=True, text=True, check=True).stdout.split()
        for f in files:
            m = re.search(r"vowel_a_([a-g]b?\d)\.wav$", f)
            if m:
                out.append(dict(rel=f, key=exlib.parse_note(m.group(1)), vel=(0, 127), rrs=(1, 0, f)))
    for r in out:  # measured sounding-pitch correction (see validate.py --pitch)
        r["key"] += ins.get("transpose", 0)
    # dedupe identical files (some sfz reuse a sample for several regions)
    seen = {}
    for r in out:
        seen.setdefault(r["rel"], r)
    return list(seen.values())


def select(ins):
    """Per-note velocity layering. A note with >= N layers gets N evenly chosen ones (V1..VN);
    a note with fewer layers gets them placed into N equal velocity bins by range midpoint."""
    regs = regions_for(ins)
    N = ins["vel"]
    keys = sorted({r["key"] for r in regs})
    kept = []
    for k in keys:
        if not kept or k - kept[-1] >= ins["gap"]:
            kept.append(k)
    sel = []
    for k in kept:
        rk = [r for r in regs if r["key"] == k]
        vels = sorted({r["vel"] for r in rk})
        if ins.get("velpick") and len(vels) > max(ins["velpick"]):
            assign = {vels[i]: j + 1 for j, i in enumerate(ins["velpick"])}
        elif len(vels) >= N:
            if N == 1:
                idx = [len(vels) - 1] if len(vels) <= 2 else [len(vels) // 2]
            else:
                idx = sorted({round(i * (len(vels) - 1) / (N - 1)) for i in range(N)})
            assign = {vels[i]: j + 1 for j, i in enumerate(idx)}
        else:
            assign = {}
            for v in reversed(vels):
                b = min(N, int((v[0] + v[1]) / 2 / 128 * N) + 1)
                while b in assign.values() and b > 1:
                    b -= 1
                if b not in assign.values():
                    assign[v] = b
        for v, vi in assign.items():
            group = sorted([r for r in rk if r["vel"] == v], key=lambda r: r["rrs"])
            for i, r in enumerate(group[: ins["rr"]]):
                sel.append(dict(r, V=vi, RR=i + 1, nV=N))
    return sel


def out_name(ins, s, nrr):
    n = "%s_%s" % (ins["prefix"], exlib.note_name(s["key"]))
    if s["nV"] > 1:
        n += "_V%d" % s["V"]
    if nrr > 1:
        n += "_RR%d" % s["RR"]
    return n + ".wav"


def tree_sizes(repo):
    r = subprocess.run(["git", "-C", repo, "ls-tree", "-r", "-l", "HEAD"], capture_output=True, text=True)
    return {}


def fetch(repo, rels):
    pf = os.path.join(TMP, "pathspec.txt")
    os.makedirs(TMP, exist_ok=True)
    with open(pf, "w") as f:
        f.write("\n".join(rels) + "\n")
    env = dict(os.environ, GIT_LITERAL_PATHSPECS="1")
    subprocess.run(["git", "-C", repo, "checkout", "-q", "HEAD", "--pathspec-from-file=" + pf], check=True, env=env)


def trim(w, max_sec, thresh_db=-62.0):
    x = w.float()
    env = np.max(np.abs(x), axis=1)
    loop_end = max((e for s, e in (w.smpl or {}).get("loops", [])), default=0)
    above = np.nonzero(env > 10 ** (thresh_db / 20))[0]
    end = (above[-1] + int(0.05 * w.sr)) if len(above) else w.frames
    end = min(end, w.frames, int(max_sec * w.sr))
    if loop_end:
        end = max(end, min(loop_end + 1, w.frames))
        if loop_end + 1 > int(max_sec * w.sr) * 2:
            w.smpl = None  # absurdly long loop; drop it rather than keep huge file
            end = min(end, int(max_sec * w.sr))
    d = w.data[:end].astype(np.float64)
    if not loop_end or w.smpl is None:
        fl = min(int(0.08 * w.sr), end // 4)
        if fl > 0:
            d[-fl:] *= np.linspace(1, 0, fl)[:, None] ** 2
    w.data = np.round(d).astype(np.int32)
    return w


def build(ins):
    sel = select(ins)
    dest = os.path.join(OUT, ins["name"])
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)
    nrr = max(s["RR"] for s in sel)
    tmp = tempfile.mkdtemp(dir=TMP)
    log = []
    for s in sel:
        src = os.path.join(ins["repo"], s["rel"])
        w = exlib.load_any(src, tmp)
        if w.sr not in (44100, 48000):
            w = exlib.resample(w, 48000, tmp)
        if w.bits not in (16, 24):
            w.bits = 24
        w = trim(w, ins["sec"])
        name = out_name(ins, s, nrr)
        exlib.write_wav(os.path.join(dest, name), w, unity=s["key"])
        log.append((name, s["rel"], w.sr, w.bits, w.channels, round(w.frames / w.sr, 2), bool(w.smpl and w.smpl.get("loops"))))
    shutil.rmtree(tmp)
    return log


def main():
    mode = sys.argv[1]
    only = sys.argv[2:]
    todo = [i for i in INSTR if not only or i["name"] in only]
    if mode == "plan":
        for ins in todo:
            sel = select(ins)
            keys = sorted({s["key"] for s in sel})
            print("%-24s %3d files  notes %s..%s (%d)  V%d RR%d" % (ins["name"], len(sel), exlib.note_name(keys[0]), exlib.note_name(keys[-1]),
                                                                len(keys), max(s["V"] for s in sel), max(s["RR"] for s in sel)))
            json.dump([s["rel"] for s in sel], open(os.path.join(TMP, re.sub(r"\W", "", ins["name"]) + ".json"), "w"))
    elif mode == "fetch":
        by = collections.defaultdict(list)
        for ins in todo:
            by[ins["repo"]] += [s["rel"] for s in select(ins)]
        for repo, rels in by.items():
            print("fetch", repo, len(rels), flush=True)
            fetch(repo, rels)
    elif mode == "build":
        allog = {}
        for ins in todo:
            allog[ins["name"]] = build(ins)
            print("built", ins["name"], len(allog[ins["name"]]), flush=True)
        json.dump(allog, open(os.path.join(TMP, "buildlog_%s.json" % ("all" if not only else re.sub(r"\W", "", only[0]))), "w"), indent=1)


if __name__ == "__main__":
    os.makedirs(TMP, exist_ok=True)
    main()

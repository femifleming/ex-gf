#!/usr/bin/env python3
"""Audit every built plug-in in every format with the test host (EXTest --audit).

    tools/audit.py <EXTest> <work dir> <plug-in> [<plug-in> ...]

Each plug-in file (.vst3, .component, .lv2) is loaded in a fresh process several times, in a
private home folder and a private SD card, and checked for:

  load      the format's host can create it
  defaults  every parameter starts on the default the source defines
  repeat    two fresh instances report the same parameters and render the same audio
  clean     an instance opened after one whose parameters were all changed still starts on defaults
  state     all parameters changed -> state saved -> new instance -> state loaded -> same values
  audio     no NaN, something comes out, nothing above full scale at the defaults
  midi      instruments play a chord; effects that take MIDI sound different when they get notes
  stereo    the two outputs differ; for effects, left-only and right-only input stay on their side
  out34     with the second stereo output (Out 3/4) switched on, Out 1/2 is unchanged and Out 3/4 is clean

Audio Units must be installed (macOS only finds registered components). Exit status 1 on any failure.
"""
import json, os, shutil, struct, subprocess, sys, wave

# what to switch on so each effect really processes (parameter numbers of the algorithm)
ACTIVE = {
    "EX Granulator": "9=-40,11=1,42=1,21=250",        # wet only, record, drone 1, short delay
    "EX Multi-FX": "11=-40",                           # wet only
    "EX Resonator": "20=-40",                          # wet only
    "EX Spectral Freeze": "21=-40,54=1,27=-100,28=-50,29=50,30=100",   # wet only, live processing, the four voices panned apart
}


def run(extest, env, args, out):
    try:
        r = subprocess.run([extest, "--audit"] + args, env=env, capture_output=True, timeout=300)
    except subprocess.TimeoutExpired:
        return {"error": "timed out"}
    if not os.path.exists(out):
        return {"error": "crashed or wrote nothing (exit %d) %s" % (r.returncode, r.stderr.decode(errors="replace")[-300:])}
    with open(out) as f:
        return json.load(f)


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    pos = data.find(b"data")
    n = struct.unpack("<I", data[pos + 4:pos + 8])[0]
    return struct.unpack("<%df" % (n // 4), data[pos + 8:pos + 8 + n])


def same_params(a, b, tol=1e-5):
    """Parameters are matched by name (hosts list Audio Unit parameters in their own order) and compared by value:
    one step of the finest parameter is 3e-5. The text is not compared, it can depend on files that load later."""
    bad = []
    other = {p["name"]: p for p in b}
    for x in a:
        y = other.get(x["name"])
        if y is None:
            bad.append("%s: missing" % x["name"])
        elif abs(x["value"] - y["value"]) > tol:
            bad.append("%s: %s / %s" % (x["name"], x["text"], y["text"]))
    if len(a) != len(b):
        bad.append("parameter count %d / %d" % (len(a), len(b)))
    return bad


def audit_one(extest, work, plugin):
    name = os.path.basename(plugin)
    d = os.path.join(work, name.replace(" ", "_"))
    shutil.rmtree(d, ignore_errors=True)
    home = os.path.join(d, "home")
    os.makedirs(os.path.join(home, "Music"))
    env = dict(os.environ, HOME=home, CFFIXED_USER_HOME=home, XDG_CONFIG_HOME=os.path.join(home, ".config"),
               APPDATA=home, TMPDIR=os.path.join(d, "tmp") + "/", TEMP=os.path.join(d, "tmp"), TMP=os.path.join(d, "tmp"))
    env.pop("EX_SD_ROOT", None)
    os.makedirs(os.path.join(d, "tmp"))
    j = lambda n: os.path.join(d, n)
    res = {"plugin": name, "notes": []}
    note = res["notes"].append

    f1 = run(extest, env, [plugin, j("fresh1.json"), "--render", j("fresh1.wav")], j("fresh1.json"))
    if "error" in f1:
        res.update(load="FAIL", error=f1["error"])
        return res
    res.update(format=f1["format"], name=f1["name"], version=f1["version"], load="ok", params=len(f1["params"]),
               instrument=f1["instrument"], buses=f1["buses"], layouts=f1["layouts"])

    # defaults: the host's values against the host's defaults and against the source's
    bad = []
    exp = {e["name"]: e for e in f1["expected"]}
    if len(exp) != len(f1["params"]):
        bad.append("%d parameters, the source defines %d" % (len(f1["params"]), len(exp)))
    for p in f1["params"]:
        e = exp.get(p["name"])
        if e is None:
            bad.append("%s is not a parameter of the source" % p["name"])
        elif abs(p["value"] - p["default"]) > 1e-5:
            bad.append("%s is %s, not the default the host reports" % (p["name"], p["text"]))
        elif abs(p["value"] - e["norm"]) > 1e-5:
            bad.append("%s is %s, the source says %d" % (p["name"], p["text"], e["def"]))
    res["defaults"] = "ok" if not bad else "FAIL (%d)" % len(bad)
    for b in bad[:6]:
        note("defaults: " + b)

    f2 = run(extest, env, [plugin, j("fresh2.json"), "--render", j("fresh2.wav")], j("fresh2.json"))
    bad = same_params(f1["params"], f2.get("params", []))
    same_audio = "render" in f2 and f1["render"]["hash"] == f2["render"]["hash"]
    res["repeat"] = "ok" if not bad and same_audio else "FAIL"
    if bad:
        note("repeat: parameters differ: " + "; ".join(bad[:4]))
    if not same_audio:
        note("repeat: the two renders differ")

    act = actmidi = None
    if f1["name"] in ACTIVE:
        act = run(extest, env, [plugin, j("active.json"), "--set", ACTIVE[f1["name"]], "--render", j("active.wav")], j("active.json"))
        if f1["acceptsMidi"]:   # the same again with a chord: an effect that listens to notes must sound different
            actmidi = run(extest, env, [plugin, j("actmidi.json"), "--set", ACTIVE[f1["name"]], "--midi", "--render", j("actmidi.wav")], j("actmidi.json"))

    # the second stereo output (Out 3/4) switched on as well: the main pair must not change, and 3/4 must be clean
    ax = run(extest, env, [plugin, j("aux.json"), "--aux", "--render", j("aux.wav")] + (["--set", ACTIVE[f1["name"]]] if f1["name"] in ACTIVE else []), j("aux.json"))

    sa = run(extest, env, [plugin, j("setall.json"), "--set-all", "--state-out", j("state.bin")], j("setall.json"))
    f3 = run(extest, env, [plugin, j("fresh3.json")], j("fresh3.json"))
    bad = same_params(f1["params"], f3.get("params", []))
    res["clean"] = "ok" if not bad and "error" not in f3 else "FAIL (%d)" % len(bad)
    for b in bad[:4]:
        note("clean: after an instance with changed values, a new one starts with " + b)

    rs = run(extest, env, [plugin, j("restore.json"), "--state-in", j("state.bin")], j("restore.json"))
    if "error" in sa or "error" in rs:
        res["state"] = "FAIL"
        note("state: " + sa.get("error", rs.get("error", "")))
    else:
        moved = len(same_params(f1["params"], sa["paramsAfterSet"]))
        bad = same_params(sa["paramsAfterSet"], rs["params"])
        res["state"] = "ok (%d/%d changed)" % (moved, len(f1["params"])) if not bad and moved == len(f1["params"]) else "FAIL (%d)" % max(len(bad), 1)
        if moved != len(f1["params"]):
            note("state: only %d of %d parameters took a new value" % (moved, len(f1["params"])))
        for b in bad[:6]:
            note("state: " + b)

    # audio at the defaults
    r = f1["render"]
    a = r["all"]
    problems = []
    if r["nan"]:
        problems.append("NaN")
    if a["peak"] < 0.01:
        problems.append("silent")
    if a["peak"] > 1.0:
        problems.append("peak %.2f" % a["peak"])
    res["peak"] = round(a["peak"], 3)
    res["audio"] = "ok" if not problems else "FAIL " + ", ".join(problems)

    if act:
        if "error" in act or act["render"]["nan"] or act["render"]["both"]["peak"] < 0.003:
            res["audio"] = "FAIL wet path silent" if "error" not in act else "FAIL " + act["error"]
        else:
            res["wetPeak"] = round(act["render"]["all"]["peak"], 3)
            if act["render"]["all"]["peak"] > 1.0 and res["audio"] == "ok":
                res["audio"] = "FAIL wet peak %.2f" % act["render"]["all"]["peak"]

    # stereo
    if f1["instrument"]:
        res["stereo"] = "ok" if a["rmsL"] > 1e-4 and a["rmsR"] > 1e-4 else "FAIL one side silent"
        res["lr"] = "L %.3f R %.3f corr %.2f" % (a["rmsL"], a["rmsR"], a["corr"])
        if a["maxLRdiff"] < 1e-6:
            res["lr"] += " (identical: centred mono at the defaults)"
    else:
        lo, ro, both = r["leftOnly"], r["rightOnly"], r["both"]
        # each input must come out mainly on its own side (delays and reverbs spread a little to the other one)
        ok = lo["rmsL"] > 2 * lo["rmsR"] and ro["rmsR"] > 2 * ro["rmsL"] and both["maxLRdiff"] > 1e-3
        res["stereo"] = "ok" if ok else "FAIL"
        res["lr"] = "L in: L %.3f R %.3f | R in: L %.3f R %.3f | both: corr %.2f" % (lo["rmsL"], lo["rmsR"], ro["rmsL"], ro["rmsR"], both["corr"])
        if act and "error" not in act:
            b = act["render"]["both"]
            res["lrWet"] = "wet only: L %.3f R %.3f corr %.2f" % (b["rmsL"], b["rmsR"], b["corr"])
            if b["maxLRdiff"] < 1e-5:
                res["stereo"] = "FAIL wet is mono"
    if "error" in ax or not ax.get("aux") or "auxRms3" not in ax["render"]:
        res["out34"] = "FAIL " + ax.get("error", "Out 3/4 could not be switched on")
    else:
        same = ax["render"]["hash"] == (act if act else f1)["render"]["hash"]
        res["out34"] = "ok" if same and not ax["render"]["auxNan"] else "FAIL" + ("" if same else " (Out 1/2 changes when Out 3/4 is on)")
        res["aux"] = "Out 3 rms %.3f, Out 4 rms %.3f" % (ax["render"]["auxRms3"], ax["render"]["auxRms4"])
    if f1["instrument"]:
        res["midi"] = "ok" if a["peak"] >= 0.01 else "FAIL"          # the render is a played chord
    elif actmidi is None:
        res["midi"] = "no MIDI input"
    else:
        res["midi"] = "ok" if "error" not in actmidi and "error" not in act and actmidi["render"]["hash"] != act["render"]["hash"] else "FAIL notes change nothing"
    res["dir"] = d
    return res


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    extest, work, plugins = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), sys.argv[3:]
    os.makedirs(work, exist_ok=True)
    results = [audit_one(extest, work, os.path.abspath(p)) for p in plugins]

    # the same plug-in in two formats must give the same audio, channel by channel
    by_name = {}
    for r in results:
        if r.get("load") == "ok":
            by_name.setdefault(r["name"], []).append(r)
    for name, rs in by_name.items():
        for other in rs[1:]:
            a, b = read_wav(os.path.join(rs[0]["dir"], "fresh1.wav")), read_wav(os.path.join(other["dir"], "fresh1.wav"))
            dl = max(abs(x - y) for x, y in zip(a[0::2], b[0::2]))
            dr = max(abs(x - y) for x, y in zip(a[1::2], b[1::2]))
            other["vsFirst"] = "%s vs %s: max difference L %.2g R %.2g" % (other["format"], rs[0]["format"], dl, dr)
            if max(dl, dr) > 1e-4:
                other["notes"].append("formats: output differs from " + rs[0]["format"])
                other["stereo"] = "FAIL differs from " + rs[0]["format"]

    cols = ["load", "defaults", "repeat", "clean", "state", "audio", "midi", "stereo", "out34"]
    print("| plug-in | format | version | params | " + " | ".join(cols) + " |")
    print("|---|---|---|---|" + "---|" * len(cols))
    failed = 0
    for r in results:
        print("| %s | %s | %s | %s | " % (r.get("name", r["plugin"]), r.get("format", "?"), r.get("version", "?"), r.get("params", "?"))
              + " | ".join(str(r.get(c, "-")) for c in cols) + " |")
        failed += sum(1 for c in cols if str(r.get(c, "FAIL")).startswith("FAIL"))
    print()
    for r in results:
        head = "%s %s" % (r.get("name", r["plugin"]), r.get("format", ""))
        for k in ("error", "lr", "lrWet", "aux", "vsFirst"):
            if k in r:
                print("%s: %s" % (head, r[k]))
        if "peak" in r:
            print("%s: peak %.3f%s" % (head, r["peak"], ", wet only %.3f" % r["wetPeak"] if "wetPeak" in r else ""))
        if "layouts" in r:
            print("%s: layouts %s; buses %s" % (head, ", ".join(k for k, v in r["layouts"].items() if v),
                  ", ".join("%s %s %dch%s" % (b["dir"], b["name"], b["channels"], "" if b["enabled"] else " (off)") for b in r["buses"])))
        for n in r["notes"]:
            print("%s: %s" % (head, n))
    with open(os.path.join(work, "audit.json"), "w") as f:
        json.dump(results, f, indent=1)
    print()
    print("AUDIT PASSED" if failed == 0 else "%d AUDIT FAILURES" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

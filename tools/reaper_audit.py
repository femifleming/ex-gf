#!/usr/bin/env python3
"""Audit the plug-ins inside a real DAW: REAPER, driven by ReaScript and its command line.

    tools/reaper_audit.py <EXTest> <work dir> <folder with the .vst3 bundles> [--au] [--only "EX Granulator"]

REAPER runs with its own configuration (in "reaper-cfg" next to the work folder; it never touches your REAPER
settings), opens no audio device (audio is checked with offline renders) and the plug-ins get a private home folder
and a small test SD card. Run one at a time. The VST3s are taken from the given
folder; Audio Units are the installed ones (--au). For every plug-in and format it

  1. adds the plug-in to a new track and reads every parameter                      -> defaults
  2. saves a project that plays 6 s of audio / a chord through it, and renders it   -> audio, stereo, VST3 against AU
  3. sets every parameter to another value and saves the project                    -> (state is written)
  4. copies the track's state onto a new track, as loading a preset does            -> preset
  5. removes it all and adds a new instance                                         -> next instance on defaults
  6. quits REAPER, starts it again, opens the saved project                         -> project recall

Exit status 1 on any failure.
"""
import json, math, os, shutil, struct, subprocess, sys, time

REAPER = "/Applications/REAPER.app/Contents/MacOS/REAPER"
ACTIVE = {  # what to switch on so each effect really processes (parameter name -> plain value)
    "EX Granulator": {"9 Dry gain": -40, "11 Record": 1, "42 Drone 1 enable": 1, "21 Delay mean": 250},
    "EX Multi-FX": {"11 Dry gain": -40},
    "EX Resonator": {"20 Dry gain": -40},
    "EX Spectral Freeze": {"21 Dry gain": -40, "54 Live process": 1, "27 Voice 1 pan": -100, "28 Voice 2 pan": -50, "29 Voice 3 pan": 50, "30 Voice 4 pan": 100},
}
INSTRUMENTS = {"EX Multisample", "EX Poly Wavetable", "EX Poly FM", "EX Poly Exciter", "EX Macro Oscillator 2"}
NO_NOTES = {"EX Multi-FX", "EX Spectral Freeze"}   # insert effects: Audio Unit hosts do not pass them MIDI, so no notes for the VST3 either

LUA_COMMON = r'''
local D = dofile(DATA)
local out = io.open(D.out, "w")
local function log(...)
  local t = {...}
  for i = 1, #t do t[i] = tostring(t[i]) end
  out:write(table.concat(t, "\t"), "\n"); out:flush()
end
local function dump(tag, key, tr, fx)
  local n = reaper.TrackFX_GetNumParams(tr, fx)
  for i = 0, n - 1 do
    local _, name = reaper.TrackFX_GetParamName(tr, fx, i, "")
    local v = reaper.TrackFX_GetParamNormalized(tr, fx, i)
    local _, txt = reaper.TrackFX_GetFormattedParamValue(tr, fx, i, "")
    log("P", key, tag, name, string.format("%.9f", v), txt)
  end
end
local function setByName(tr, fx, values)
  local n = reaper.TrackFX_GetNumParams(tr, fx)
  for i = 0, n - 1 do
    local _, name = reaper.TrackFX_GetParamName(tr, fx, i, "")
    if values[name] ~= nil then reaper.TrackFX_SetParamNormalized(tr, fx, i, values[name]) end
  end
end
local co
local wakeAt = 0
local function wait(sec) wakeAt = reaper.time_precise() + sec; coroutine.yield() end
local function pumpCo()
  if reaper.time_precise() < wakeAt then reaper.defer(pumpCo) return end
  local ok, err = coroutine.resume(co)
  if not ok then log("ERROR", err) end
  if coroutine.status(co) ~= "dead" then reaper.defer(pumpCo) end
end
local function clearTracks()
  while reaper.CountTracks(0) > 0 do reaper.DeleteTrack(reaper.GetTrack(0, 0)) end
end
'''

LUA_A = LUA_COMMON + r'''
local function addMedia(tr, job)
  if job.audio then
    reaper.SetOnlyTrackSelected(tr)
    reaper.SetEditCurPos(0, false, false)
    reaper.InsertMedia(D.audio, 0)
  end
  if job.notes then
    local item = reaper.CreateNewMIDIItemInProj(tr, 0, D.length, false)
    local take = reaper.GetActiveTake(item)
    for _, note in ipairs({48, 55, 60, 64}) do
      reaper.MIDI_InsertNote(take, false, false, reaper.MIDI_GetPPQPosFromProjTime(take, job.noteOn),
                             reaper.MIDI_GetPPQPosFromProjTime(take, job.noteOff), 0, note, 100, true)
    end
    reaper.MIDI_Sort(take)
  end
  reaper.SetEditCurPos(0, false, false)
end
local function renderSettings(file)
  reaper.GetSetProjectInfo(0, "PROJECT_SRATE", 48000, true)
  reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
  reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)      -- master mix
  reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 0, true)    -- custom time bounds
  reaper.GetSetProjectInfo(0, "RENDER_STARTPOS", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_ENDPOS", D.length, true)
  reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
  reaper.GetSetProjectInfo(0, "RENDER_SRATE", 48000, true)
  reaper.GetSetProjectInfo(0, "RENDER_TAILFLAG", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_ADDTOPROJ", 0, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_FILE", D.dir, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", file, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "ZXZhdxgAAA==", true)   -- WAV, 24 bit
end
co = coroutine.create(function()
  wait(1.0)
  local _, devIn = reaper.GetAudioDeviceInfo("IDENT_IN", "")
  local _, devOut = reaper.GetAudioDeviceInfo("IDENT_OUT", "")
  log("AUDIO", reaper.Audio_IsRunning(), devIn, devOut)
  for _, job in ipairs(D.jobs) do
    clearTracks()
    reaper.InsertTrackAtIndex(0, false)
    local tr = reaper.GetTrack(0, 0)
    local fx = reaper.TrackFX_AddByName(tr, job.add, false, -1)
    log("FX", job.key, fx)
    if fx >= 0 then
      local _, nm = reaper.TrackFX_GetFXName(tr, fx, "")
      local _, ins, outs = reaper.TrackFX_GetIOSize(tr, fx)
      log("NAME", job.key, nm, ins, outs)
      wait(2.5)
      dump("fresh", job.key, tr, fx)
      addMedia(tr, job)
      renderSettings("render_" .. job.key)
      reaper.Main_SaveProjectEx(0, D.dir .. "/render_" .. job.key .. ".rpp", 0)
      if job.active then
        setByName(tr, fx, job.active)
        wait(1.0)
        renderSettings("renderact_" .. job.key)
        reaper.Main_SaveProjectEx(0, D.dir .. "/renderact_" .. job.key .. ".rpp", 0)
      end
      setByName(tr, fx, job.set)
      wait(1.5)
      dump("set", job.key, tr, fx)
      reaper.Main_SaveProjectEx(0, D.dir .. "/state_" .. job.key .. ".rpp", 0)
      -- the track's state on a second track: a new instance that is handed the saved state, like a preset
      local _, chunk = reaper.GetTrackStateChunk(tr, "", false)
      reaper.InsertTrackAtIndex(1, false)
      local tr2 = reaper.GetTrack(0, 1)
      chunk = chunk:gsub("TRACKID {[^}]*}", "", 1):gsub("FXID {[^}]*}", "")
      reaper.SetTrackStateChunk(tr2, chunk, false)
      wait(2.5)
      log("COPYFX", job.key, reaper.TrackFX_GetCount(tr2))
      if reaper.TrackFX_GetCount(tr2) > 0 then dump("copied", job.key, tr2, 0) end
      clearTracks()
      wait(2.5)
      reaper.InsertTrackAtIndex(0, false)
      tr = reaper.GetTrack(0, 0)
      fx = reaper.TrackFX_AddByName(tr, job.add, false, -1)
      wait(2.5)
      if fx >= 0 then dump("fresh2", job.key, tr, fx) end
    end
  end
  clearTracks()
  reaper.Main_SaveProjectEx(0, D.dir .. "/_scratch.rpp", 8)
  log("DONE")
  out:close()
  reaper.Main_OnCommand(40004, 0)   -- File: Quit REAPER
end)
reaper.defer(pumpCo)
'''

LUA_B = LUA_COMMON + r'''
co = coroutine.create(function()
  wait(1.0)
  for _, job in ipairs(D.jobs) do
    reaper.Main_openProject("noprompt:" .. D.dir .. "/state_" .. job.key .. ".rpp")
    wait(3.5)
    local tr = reaper.GetTrack(0, 0)
    local n = tr and reaper.TrackFX_GetCount(tr) or 0
    log("FX", job.key, n > 0 and 0 or -1)
    if n > 0 then
      local _, nm = reaper.TrackFX_GetFXName(tr, 0, "")
      log("NAME", job.key, nm, 0, 0)
      dump("reopened", job.key, tr, 0)
    end
  end
  log("DONE")
  out:close()
  reaper.Main_OnCommand(40859, 0)   -- new project tab, nothing to save
  reaper.Main_OnCommand(40004, 0)
end)
reaper.defer(pumpCo)
'''


def lua(v, ind=""):
    if isinstance(v, dict):
        return "{\n" + "".join('%s  [%s] = %s,\n' % (ind, json.dumps(k), lua(x, ind + "  ")) for k, x in v.items()) + ind + "}"
    if isinstance(v, list):
        return "{\n" + "".join("%s  %s,\n" % (ind, lua(x, ind + "  ")) for x in v) + ind + "}"
    if isinstance(v, bool):
        return "true" if v else "false"
    if v is None:
        return "nil"
    return json.dumps(v)


def write_input(path, length=6.0, sr=48000):
    """Left only for 2 s, right only for 2 s, then both - different notes and noise bursts on each side."""
    n = int(length * sr)
    frames = bytearray()
    seed = [1234567, 7654321]

    def noise(c):
        seed[c] = (seed[c] * 1664525 + 1013904223) & 0xFFFFFFFF
        return (seed[c] >> 8) * (2.0 / 16777216.0) - 1.0
    for i in range(n):
        t = i / sr
        pl, pr = math.fmod(t, 0.5), math.fmod(t, 0.37)
        l = 0.25 * math.exp(-pl * 5) * (math.sin(2 * math.pi * 220 * t) + math.sin(2 * math.pi * 277.2 * t)) + (0.2 * noise(0) if pl < 0.01 else 0)
        r = 0.25 * math.exp(-pr * 5) * (math.sin(2 * math.pi * 330 * t) + math.sin(2 * math.pi * 415.3 * t)) + (0.2 * noise(1) if pr < 0.01 else 0)
        if t < 2:
            r = 0
        elif t < 4:
            l = 0
        for v in (l, r):
            frames += struct.pack("<i", int(max(-1, min(1, v)) * 8388607))[:3]
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(frames)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, sr, sr * 6, 6, 24)
                + b"data" + struct.pack("<I", len(frames)) + frames)


def read_wav(path):
    with open(path, "rb") as f:
        d = f.read()
    fmt = d.find(b"fmt ")
    tag, ch, sr, _, _, bits = struct.unpack("<HHIIHH", d[fmt + 8:fmt + 24])
    pos = d.find(b"data")
    n = min(struct.unpack("<I", d[pos + 4:pos + 8])[0], len(d) - pos - 8)
    raw = d[pos + 8:pos + 8 + n]
    bps = bits // 8
    if tag == 3:
        vals = struct.unpack("<%df" % (n // 4), raw[:n - n % 4])
    else:
        vals = [int.from_bytes(raw[i:i + bps], "little", signed=True) / float(1 << (bits - 1)) for i in range(0, n - n % bps, bps)]
    return [vals[c::ch] for c in range(ch)], sr


def stats(ch, a, b, sr):
    l, r = ch[0][int(a * sr):int(b * sr)], ch[1][int(a * sr):int(b * sr)]
    n = max(1, len(l))
    sl, s_r, slr = sum(x * x for x in l), sum(x * x for x in r), sum(x * y for x, y in zip(l, r))
    return {"rmsL": math.sqrt(sl / n), "rmsR": math.sqrt(s_r / n), "corr": slr / math.sqrt(sl * s_r) if sl > 0 and s_r > 0 else 0.0,
            "diff": max((abs(x - y) for x, y in zip(l, r)), default=0.0), "peak": max((max(abs(x), abs(y)) for x, y in zip(l, r)), default=0.0)}


def run_reaper(cfg, script, log, timeout):
    if os.path.exists(log):
        os.remove(log)
    p = subprocess.Popen([REAPER, "-newinst", "-cfgfile", cfg, "-nosplash", "-new", "-ignoreerrors", script], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    done = False
    while time.time() - t0 < timeout:
        if p.poll() is not None:
            break
        if not done and os.path.exists(log) and "DONE" in open(log, errors="replace").read()[-200:]:
            done, t0, timeout = True, time.time(), 15     # give it a moment to quit by itself
        time.sleep(1)
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(10)
        except subprocess.TimeoutExpired:
            p.kill()
    return open(log, errors="replace").read().splitlines() if os.path.exists(log) else []


def parse(lines):
    res = {}
    for ln in lines:
        f = ln.split("\t")
        if f[0] == "FX":
            res.setdefault(f[1], {})["fx"] = int(f[2])
        elif f[0] == "NAME":
            res.setdefault(f[1], {}).update(name=f[2], pins=(f[3], f[4]))
        elif f[0] == "COPYFX":
            res.setdefault(f[1], {})["copyfx"] = int(f[2])
        elif f[0] == "P":
            res.setdefault(f[1], {}).setdefault(f[2], {})[f[3]] = (float(f[4]), f[5] if len(f) > 5 else "")
        elif f[0] == "AUDIO":
            res["_audio"] = f[1:]
        elif f[0] == "ERROR":
            res.setdefault("_errors", []).append(ln)
    res["_done"] = any(ln == "DONE" for ln in lines)
    return res


def main():
    args = sys.argv[1:]
    use_au = "--au" in args
    only = args[args.index("--only") + 1] if "--only" in args else None
    args = [a for a in args if not a.startswith("--") and a != only]
    if len(args) < 3:
        print(__doc__)
        return 2
    extest, work, vstdir = os.path.abspath(args[0]), os.path.abspath(args[1]), os.path.abspath(args[2])
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    # REAPER's configuration is kept next to the work folder and reused: its first start scans every VST on the
    # machine (REAPER adds the standard folders to the path itself), which takes minutes
    cfgdir = os.path.join(os.path.dirname(work), "reaper-cfg")
    first_run = not os.path.exists(os.path.join(cfgdir, "reaper-vstplugins_arm64.ini"))
    os.makedirs(cfgdir, exist_ok=True)
    cfg = os.path.join(cfgdir, "reaper.ini")
    user = os.path.expanduser("~/Library/Application Support/REAPER")
    for f in ("reaper-reginfo2.ini",):           # the licence, so no evaluation window blocks the scripts
        if os.path.exists(os.path.join(user, f)) and not os.path.exists(os.path.join(cfgdir, f)):
            shutil.copy(os.path.join(user, f), os.path.join(cfgdir, f))
    paths = vstdir + ";/Library/Audio/Plug-Ins/VST3;~/Library/Audio/Plug-Ins/VST3"
    # No audio device: the device names match nothing and REAPER keeps the device closed while stopped (the scripts
    # never start playback; audio is checked with offline renders). The first run logs what REAPER has open.
    ours = {"vstpath_arm64": paths, "coreaudioindevnew": "Dummy Audio", "coreaudiooutdevnew": "Dummy Audio", "audioclosestop": "1",
            "audiocloseinactive": "1", "coreaudiosrateuse": "0", "coreaudiobsuse": "0", "loadlastproj": "0", "verchk": "0", "splash": "0"}
    lines = open(cfg).read().splitlines() if os.path.exists(cfg) else ["[REAPER]", "[verchk]", "lastt=9999999999"]
    lines = [ln for ln in lines if ln.split("=")[0] not in ours and not ln.startswith("vstpath")]
    at = lines.index("[REAPER]") + 1
    lines[at:at] = ["%s=%s" % kv for kv in ours.items()]
    open(cfg, "w").write("\n".join(lines) + "\n")

    # the plug-ins get a private home folder and a small test SD card, like REAPER gets a private configuration
    home = os.path.join(work, "home")
    os.makedirs(os.path.join(home, "Music"))
    card = os.environ.get("EX_SD_ROOT") or os.path.join(work, "card")
    if not os.environ.get("EX_SD_ROOT"):
        subprocess.run([extest, "--make-card", card], check=True, capture_output=True)
    os.environ.update(HOME=home, CFFIXED_USER_HOME=home, EX_SD_ROOT=card)

    params_json = os.path.join(work, "params.json")
    subprocess.run([extest, "--dump-params", params_json], check=True, capture_output=True)
    algs = json.load(open(params_json))
    plugins = {}
    for a in algs:
        plugins.setdefault(a["plugin"], []).append(a)
    expected, setall = {}, {}
    for name, al in plugins.items():
        exp, sa, i = {}, {}, 0
        for a in al:
            for p in a["params"]:
                host = ("%d." % a["number"] if len(al) > 1 else "") + "%d %s" % (p["n"], p["name"])
                rng = p["max"] - p["min"]
                exp[host] = (p["def"] - p["min"]) / rng if rng else 0.0
                if rng:
                    v = p["min"] + (p["def"] - p["min"] + 1 + (i * 7) % rng) % (rng + 1)
                    sa[host] = (v - p["min"]) / rng
                i += 1
        expected[name], setall[name] = exp, sa
    ranges = {name: {("%d." % a["number"] if len(al) > 1 else "") + "%d %s" % (p["n"], p["name"]): (p["min"], p["max"]) for a in al for p in a["params"]}
              for name, al in plugins.items()}

    write_input(os.path.join(work, "input.wav"))
    jobs = []
    for name in plugins:
        if only and name != only:
            continue
        for fmt in (["VST3"] + (["AU"] if use_au else [])):
            inst = name in INSTRUMENTS
            job = {"key": fmt.lower() + "_" + name.replace(" ", "_").replace("-", "_"), "plugin": name, "format": fmt,
                   "add": "%s:%s" % (fmt, name), "audio": not inst,
                   "noteOn": 0.1 if inst else 4.1, "noteOff": 2.0 if inst else 5.5, "notes": name not in NO_NOTES, "set": setall[name]}
            if name in ACTIVE:
                job["active"] = {k: (v - ranges[name][k][0]) / (ranges[name][k][1] - ranges[name][k][0]) for k, v in ACTIVE[name].items()}
            jobs.append(job)
    data = {"dir": work, "audio": os.path.join(work, "input.wav"), "length": 6.0, "jobs": jobs}
    for tag, src in (("a", LUA_A), ("b", LUA_B)):
        d = dict(data, out=os.path.join(work, "log_%s.txt" % tag))
        with open(os.path.join(work, "data_%s.lua" % tag), "w") as f:
            f.write("return " + lua(d) + "\n")
        with open(os.path.join(work, "phase_%s.lua" % tag), "w") as f:
            f.write("local DATA = %s\n" % json.dumps(os.path.join(work, "data_%s.lua" % tag)) + src)

    a = parse(run_reaper(cfg, os.path.join(work, "phase_a.lua"), os.path.join(work, "log_a.txt"), (900 if first_run else 90) + 45 * len(jobs)))
    b = parse(run_reaper(cfg, os.path.join(work, "phase_b.lua"), os.path.join(work, "log_b.txt"), 90 + 15 * len(jobs)))
    for job in jobs:
        for kind in ("render", "renderact") if "active" in job else ("render",):
            proj = os.path.join(work, "%s_%s.rpp" % (kind, job["key"]))
            if os.path.exists(proj):
                try:
                    subprocess.run([REAPER, "-newinst", "-cfgfile", cfg, "-nosplash", "-ignoreerrors", "-renderproject", proj], timeout=180,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except subprocess.TimeoutExpired:
                    pass

    def differing(x, y, names):
        return [n for n in names if n not in x or n not in y or abs(x[n][0] - y[n][0]) > 1e-5]

    cols = ["load", "defaults", "preset", "next instance", "project recall", "audio", "stereo"]
    rows, notes, failed, wavs = [], [], 0, {}
    for job in jobs:
        k, name = job["key"], job["plugin"]
        ja, jb = a.get(k, {}), b.get(k, {})
        r = dict.fromkeys(cols, "-")
        exp = expected[name]
        names = list(exp)
        if ja.get("fx", -1) < 0 or "fresh" not in ja:
            r["load"] = "FAIL"
            rows.append((job, r))
            failed += 1
            continue
        r["load"] = "ok"
        fresh = ja["fresh"]
        bad = [n for n in names if n not in fresh or abs(fresh[n][0] - exp[n]) > 1e-5]
        r["defaults"] = "ok (%d)" % len(names) if not bad else "FAIL (%d)" % len(bad)
        notes += ["%s %s: default: %s is %s" % (name, job["format"], n, fresh.get(n, ("", "missing"))[1]) for n in bad[:5]]
        st = ja.get("set", {})
        moved = [n for n in names if n in st and abs(st[n][0] - exp[n]) > 1e-5]
        if "copied" in ja:
            bad = differing(st, ja["copied"], names)
            r["preset"] = "ok (%d)" % len(moved) if not bad and len(moved) == len(names) else "FAIL (%d lost, %d changed)" % (len(bad), len(moved))
            notes += ["%s %s: preset: %s was %s, came back %s" % (name, job["format"], n, st.get(n, ("", "?"))[1], ja["copied"].get(n, ("", "missing"))[1]) for n in bad[:5]]
        else:
            r["preset"] = "FAIL (no copy)"
        if "fresh2" in ja:
            bad = [n for n in names if n not in ja["fresh2"] or abs(ja["fresh2"][n][0] - exp[n]) > 1e-5]
            r["next instance"] = "ok" if not bad else "FAIL (%d)" % len(bad)
            notes += ["%s %s: next instance: %s starts on %s" % (name, job["format"], n, ja["fresh2"].get(n, ("", "missing"))[1]) for n in bad[:5]]
        else:
            r["next instance"] = "FAIL"
        if "reopened" in jb:
            bad = differing(st, jb["reopened"], names)
            r["project recall"] = "ok (%d)" % len(moved) if not bad and len(moved) == len(names) else "FAIL (%d lost)" % len(bad)
            notes += ["%s %s: recall: %s was %s, reopened %s" % (name, job["format"], n, st.get(n, ("", "?"))[1], jb["reopened"].get(n, ("", "missing"))[1]) for n in bad[:5]]
        else:
            r["project recall"] = "FAIL (not reopened)"

        wav = os.path.join(work, "render_%s.wav" % k)
        if not os.path.exists(wav):
            r["audio"] = "FAIL (no render)"
        else:
            ch, sr = read_wav(wav)
            wavs[k] = ch
            allst = stats(ch, 0, 6, sr)
            prob = []
            if any(math.isnan(x) for c in ch for x in c):
                prob.append("NaN")
            if allst["peak"] < 0.01:
                prob.append("silent")
            if allst["peak"] > 1.0:
                prob.append("peak %.2f" % allst["peak"])
            info = "peak %.3f" % allst["peak"]
            if name in INSTRUMENTS:
                r["stereo"] = "ok" if allst["rmsL"] > 1e-4 and allst["rmsR"] > 1e-4 else "FAIL"
                notes.append("%s %s: L %.3f R %.3f corr %.2f%s" % (name, job["format"], allst["rmsL"], allst["rmsR"], allst["corr"],
                                                                   " (identical: centred at the defaults)" if allst["diff"] < 1e-6 else ""))
            else:
                lo, ro, both = stats(ch, 0, 2, sr), stats(ch, 2, 4, sr), stats(ch, 4, 6, sr)
                ok = lo["rmsL"] > 2 * lo["rmsR"] and ro["rmsR"] > 2 * ro["rmsL"] and both["diff"] > 1e-3
                r["stereo"] = "ok" if ok else "FAIL"
                notes.append("%s %s: L in: L %.3f R %.3f | R in: L %.3f R %.3f | both: corr %.2f" % (name, job["format"], lo["rmsL"], lo["rmsR"], ro["rmsL"], ro["rmsR"], both["corr"]))
                wact = os.path.join(work, "renderact_%s.wav" % k)
                if os.path.exists(wact):
                    ach, _ = read_wav(wact)
                    bs = stats(ach, 4, 6, sr)
                    info += ", wet only %.3f" % stats(ach, 0, 6, sr)["peak"]
                    notes.append("%s %s: wet only: L %.3f R %.3f corr %.2f" % (name, job["format"], bs["rmsL"], bs["rmsR"], bs["corr"]))
                    if bs["peak"] < 0.003:
                        prob.append("wet silent")
                    if bs["diff"] < 1e-5:
                        r["stereo"] = "FAIL wet is mono"
                else:
                    prob.append("no wet render")
            r["audio"] = "ok (%s)" % info if not prob else "FAIL " + ", ".join(prob)
        rows.append((job, r))
        failed += sum(1 for c in cols if r[c].startswith("FAIL"))

    for job, r in rows:   # the same plug-in as VST3 and AU must render the same audio
        if job["format"] == "AU":
            v = "vst3_" + job["key"][3:]
            if v in wavs and job["key"] in wavs:
                x, y = wavs[v], wavs[job["key"]]
                dl = max(abs(p - q) for p, q in zip(x[0], y[0]))
                dr = max(abs(p - q) for p, q in zip(x[1], y[1]))
                notes.append("%s: AU against VST3: max difference L %.2g R %.2g" % (job["plugin"], dl, dr))
                if max(dl, dr) > 1e-4:
                    notes.append("%s: AU and VST3 renders differ" % job["plugin"])

    print("| plug-in | format | " + " | ".join(cols) + " |")
    print("|---|---|" + "---|" * len(cols))
    for job, r in rows:
        print("| %s | %s | " % (job["plugin"], job["format"]) + " | ".join(r[c] for c in cols) + " |")
    print()
    for n in notes:
        print(n)
    audio = a.get("_audio", ["?"])
    print("REAPER audio device: %s" % ("none open" if audio[0] == "0" and not "".join(audio[1:]) else "OPEN: " + " / ".join(audio)))
    if not (audio[0] == "0" and not "".join(audio[1:])):
        failed += 1
    for res, tag in ((a, "first run"), (b, "second run")):
        if not res.get("_done"):
            print("REAPER %s did not finish" % tag)
            failed += 1
        for e in res.get("_errors", []):
            print(e)
            failed += 1
    print()
    print("REAPER AUDIT PASSED" if failed == 0 else "%d REAPER AUDIT FAILURES" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

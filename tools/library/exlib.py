"""Shared helpers: RIFF/WAV read+write with smpl chunks, SFZ parsing, note names."""
import os, re, struct, subprocess, tempfile
import numpy as np

# Override with the EX_LIBRARY_WORK / EX_LIBRARY_OUT environment variables.
DL = os.environ.get("EX_LIBRARY_WORK", os.path.join(tempfile.gettempdir(), "ex-library-downloads"))
OUT = os.environ.get("EX_LIBRARY_OUT", os.path.join(os.path.expanduser("~"), "Music", "EX SD Card"))

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
_PC = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


def note_name(midi):
    """MIDI note -> sharps-only name, C4 = 60."""
    return "%s%d" % (NOTE_NAMES[midi % 12], midi // 12 - 1)


def parse_note(s, c4=60):
    """Parse e.g. 'C#4', 'db2', 'Bb3', 'A0'. c4 is the MIDI number that 'C4' means."""
    m = re.fullmatch(r"([A-Ga-g])([#b]?)(-?\d+)", s)
    if not m:
        return None
    pc = _PC[m.group(1).lower()] + {"#": 1, "b": -1, "": 0}[m.group(2)]
    return c4 + (int(m.group(3)) - 4) * 12 + pc


# ---------------------------------------------------------------- WAV I/O
class Wav:
    def __init__(self, data, sr, bits, smpl=None):
        self.data = data          # int32 array, shape (frames, channels), scaled to `bits`
        self.sr = sr
        self.bits = bits
        self.smpl = smpl          # dict(loops=[(start,end)], unity=int) or None

    @property
    def frames(self):
        return self.data.shape[0]

    @property
    def channels(self):
        return self.data.shape[1]

    def float(self):
        return self.data.astype(np.float64) / float(1 << (self.bits - 1))


def read_wav(path):
    with open(path, "rb") as f:
        b = f.read()
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        raise ValueError("not RIFF/WAVE: " + path)
    pos = 12
    fmt = None
    data = None
    smpl = None
    while pos + 8 <= len(b):
        cid = b[pos:pos + 4]
        size = struct.unpack("<I", b[pos + 4:pos + 8])[0]
        body = b[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, ch, sr, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            if tag == 0xFFFE and len(body) >= 26:
                tag = struct.unpack("<H", body[24:26])[0]
            fmt = (tag, ch, sr, bits)
        elif cid == b"data":
            data = body
        elif cid == b"smpl" and len(body) >= 36:
            v = struct.unpack("<9I", body[:36])
            unity, nloops = v[3], v[7]
            loops = []
            for i in range(nloops):
                o = 36 + i * 24
                if o + 24 <= len(body):
                    _, ltype, st, en, _, _ = struct.unpack("<6I", body[o:o + 24])
                    loops.append((st, en))
            smpl = {"unity": unity, "loops": loops}
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError("missing fmt/data: " + path)
    tag, ch, sr, bits = fmt
    if tag == 3:
        arr = np.frombuffer(data[: len(data) // (4 * ch) * 4 * ch], dtype="<f4" if bits == 32 else "<f8")
        arr = np.clip(arr.astype(np.float64), -1, 1 - 2 ** -23)
        ints = np.round(arr * (1 << 23)).astype(np.int32)
        bits = 24
    elif tag == 1 and bits == 16:
        ints = np.frombuffer(data[: len(data) // (2 * ch) * 2 * ch], dtype="<i2").astype(np.int32)
    elif tag == 1 and bits == 24:
        n = len(data) // (3 * ch) * 3 * ch
        u = np.frombuffer(data[:n], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        ints = u[:, 0] | (u[:, 1] << 8) | (u[:, 2] << 16)
        ints = np.where(ints & 0x800000, ints - 0x1000000, ints)
    elif tag == 1 and bits == 32:
        ints = (np.frombuffer(data[: len(data) // (4 * ch) * 4 * ch], dtype="<i4") >> 8).astype(np.int32)
        bits = 24
    elif tag == 1 and bits == 8:
        ints = (np.frombuffer(data, dtype=np.uint8).astype(np.int32) - 128) << 8
        bits = 16
    else:
        raise ValueError("unsupported format %r in %s" % (fmt, path))
    return Wav(ints.reshape(-1, ch), sr, bits, smpl)


def write_wav(path, w, unity=None):
    ch = w.channels
    bits = w.bits
    d = w.data
    if bits == 16:
        raw = np.clip(d, -32768, 32767).astype("<i2").tobytes()
    elif bits == 24:
        x = np.clip(d, -(1 << 23), (1 << 23) - 1).astype(np.int32).reshape(-1)
        u = np.empty((x.size, 3), dtype=np.uint8)
        u[:, 0] = x & 0xFF
        u[:, 1] = (x >> 8) & 0xFF
        u[:, 2] = (x >> 16) & 0xFF
        raw = u.tobytes()
    else:
        raise ValueError(bits)
    ba = ch * bits // 8
    fmt = struct.pack("<HHIIHH", 1, ch, w.sr, w.sr * ba, ba, bits)
    chunks = [b"fmt " + struct.pack("<I", len(fmt)) + fmt]
    if w.smpl and w.smpl.get("loops"):
        loops = w.smpl["loops"]
        u = unity if unity is not None else w.smpl.get("unity", 60)
        period = int(round(1e9 / w.sr))
        body = struct.pack("<9I", 0, 0, period, u, 0, 0, 0, len(loops), 0)
        for i, (st, en) in enumerate(loops):
            body += struct.pack("<6I", i, 0, st, en, 0, 0)
        chunks.append(b"smpl" + struct.pack("<I", len(body)) + body)
    chunks.append(b"data" + struct.pack("<I", len(raw)) + raw + (b"\0" if len(raw) & 1 else b""))
    payload = b"WAVE" + b"".join(chunks)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(payload)) + payload)


def from_float(x, sr, bits=16, smpl=None):
    if x.ndim == 1:
        x = x[:, None]
    s = float(1 << (bits - 1))
    return Wav(np.round(np.clip(x, -1, 1 - 1 / s) * s).astype(np.int32), sr, bits, smpl)


def load_any(path, tmpdir):
    """Read wav directly; decode flac/other with sox into tmpdir first (keeps bit depth)."""
    if path.lower().endswith(".wav"):
        try:
            return read_wav(path)
        except ValueError:
            pass
    out = os.path.join(tmpdir, "decoded.wav")
    subprocess.run(["sox", path, out], check=True, capture_output=True)
    return read_wav(out)


def resample(w, sr_out, tmpdir):
    if w.sr == sr_out:
        return w
    a = os.path.join(tmpdir, "rs_in.wav")
    b = os.path.join(tmpdir, "rs_out.wav")
    write_wav(a, Wav(w.data, w.sr, w.bits))
    subprocess.run(["sox", a, "-b", str(w.bits), b, "rate", "-v", str(sr_out)], check=True, capture_output=True)
    r = read_wav(b)
    if w.smpl:
        f = sr_out / w.sr
        r.smpl = {"unity": w.smpl.get("unity", 60),
                  "loops": [(int(round(s * f)), int(round(e * f))) for s, e in w.smpl["loops"]]}
    return r


# ---------------------------------------------------------------- SFZ
def parse_sfz(path, defines=None):
    """Very small SFZ parser -> list of region dicts (opcodes inherited from headers).
    Handles <control> default_path, #define, #include (relative to the sfz dir)."""
    defines = dict(defines or {})
    base = os.path.dirname(path)

    def load(p):
        txt = open(p, encoding="utf-8", errors="replace").read()
        txt = re.sub(r"/\*.*?\*/", "", txt, flags=re.S)
        out = []
        for line in txt.splitlines():
            line = re.sub(r"(^|\s)//.*$", "", line)
            m = re.match(r'\s*#define\s+(\$\w+)\s+(.*\S)', line)
            if m:
                defines[m.group(1)] = m.group(2)
                continue
            m = re.match(r'\s*#include\s+"([^"]+)"', line)
            if m:
                inc = os.path.join(base, m.group(1).replace("\\", "/"))
                if os.path.exists(inc):
                    out.extend(load(inc))
                continue
            for k in sorted(defines, key=len, reverse=True):
                line = line.replace(k, defines[k])
            out.append(line)
        return out

    lines = load(path)
    text = "\n".join(lines)
    tokens = re.split(r"(<\w+>)", text)
    levels = {"control": {}, "global": {}, "master": {}, "group": {}}
    order = ["control", "global", "master", "group", "region"]

    def opcodes(s):
        d = {}
        # sample= may contain spaces; split on whitespace followed by key=
        for m in re.finditer(r"(\w+)=(.*?)(?=\s+\w+=|\s*$)", s.replace("\n", " \n ").strip(), flags=re.S):
            d[m.group(1)] = m.group(2).strip()
        return d

    # rebuild with inheritance: second pass
    regions = []
    levels = {"control": {}, "global": {}, "master": {}, "group": {}}
    cur = None
    for t in tokens:
        m = re.fullmatch(r"<(\w+)>", t)
        if m:
            cur = m.group(1)
            if cur in levels:
                idx = order.index(cur)
                for lower in order[idx + 1:-1]:
                    levels[lower] = {}
                if cur != "control":
                    levels[cur] = {}
            continue
        if cur is None:
            continue
        ops = {}
        for line in t.split("\n"):
            ops.update(opcodes(line))
        if cur == "region":
            r = {}
            for lv in ("global", "master", "group"):
                r.update(levels[lv])
            r.update(ops)
            dp = levels["control"].get("default_path", "")
            if "sample" in r:
                r["_path"] = os.path.normpath(os.path.join(base, (dp + r["sample"]).replace("\\", "/")))
                r["_relpath"] = (dp + r["sample"]).replace("\\", "/")
            regions.append(r)
        elif cur in levels:
            levels[cur].update(ops)
    return regions


def region_key(r):
    for k in ("pitch_keycenter", "key"):
        if k in r:
            v = r[k]
            n = int(v) if re.fullmatch(r"-?\d+", v) else parse_note(v)
            return n
    return None

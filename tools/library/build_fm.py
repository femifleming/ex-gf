"""Build FMSYX: Plaits banks from resources.cc + Dexed builtin banks; validate all."""
import os, re, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from exlib import DL, OUT

dest = os.path.join(OUT, "FMSYX")
os.makedirs(dest, exist_ok=True)


def make_syx(data):
    assert len(data) == 4096, len(data)
    assert all(0 <= b < 128 for b in data)
    ck = (-sum(data)) & 0x7F
    return bytes([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00]) + bytes(data) + bytes([ck, 0xF7])


def voice_names(syx):
    return [syx[6 + i * 128 + 118: 6 + i * 128 + 128].decode("ascii", "replace") for i in range(32)]


def validate(path):
    b = open(path, "rb").read()
    errs = []
    if len(b) != 4104:
        errs.append("size %d" % len(b))
    if b[:6] != bytes([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00]):
        errs.append("header %s" % b[:6].hex())
    if b[-1] != 0xF7:
        errs.append("no F7")
    data = b[6:4102]
    if any(x > 127 for x in data):
        errs.append("data byte >127")
    if ((-sum(data)) & 0x7F) != b[4102]:
        errs.append("checksum")
    return errs, b


src = open(os.path.join(DL, "plaits", "resources.cc")).read()
for i in range(3):
    m = re.search(r"const uint8_t syx_bank_%d\[\] = \{(.*?)\};" % i, src, re.S)
    data = [int(x) for x in re.findall(r"\d+", m.group(1))]
    open(os.path.join(dest, "plaits_%d.syx" % (i + 1)), "wb").write(make_syx(data))

z = zipfile.ZipFile(os.path.join(DL, "dexed", "builtin_pgm.zip"))
for n in z.namelist():
    if n.lower().endswith(".syx") and "/" not in n:
        b = z.read(n)
        out = os.path.join(dest, "dexed_" + os.path.basename(n))
        open(out, "wb").write(b)

ok = 0
for f in sorted(os.listdir(dest)):
    errs, b = validate(os.path.join(dest, f))
    if errs:
        print("BAD", f, errs)
        os.remove(os.path.join(dest, f))
    else:
        ok += 1
        if f.startswith("plaits") or f == "dexed_Dexed_01.syx":
            print("OK ", f, "|", " ".join(n.strip() for n in voice_names(b)[:8]), "...")
print("valid banks:", ok)

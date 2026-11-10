# Collect unique 6x8 glyph cells from disting EX manual screenshots (grid-aligned text lines)
import sys, glob, collections
from PIL import Image
imdir = sys.argv[1]; out = sys.argv[2]
cnt = collections.Counter()
for f in sorted(glob.glob(imdir + '/i-*.png')):
    im = Image.open(f)
    if im.size != (128, 32): continue
    im = im.convert('L')
    px = im.load()
    for row in range(2):
        for col in range(21):
            bits = []
            for y in range(8):
                v = 0
                for x in range(6):
                    xx = col*6+x
                    v = v*2 + (1 if xx < 128 and px[xx, row*8+y] > 128 else 0)
                bits.append(v)
            # require blank right column & bottom row typical? keep all non-empty
            if any(bits) and all((b & 1) == 0 for b in bits):
                cnt[tuple(bits)] += 1
glyphs = [g for g, c in cnt.most_common() if c >= 12]
print(len(glyphs))
S = 4; per = 16
sheet = Image.new('RGB', (per*(6*S+14), ((len(glyphs)+per-1)//per)*(8*S+16)), (40,40,40))
from PIL import ImageDraw
d = ImageDraw.Draw(sheet)
for i, g in enumerate(glyphs):
    ox = (i % per)*(6*S+14); oy = (i//per)*(8*S+16)
    d.text((ox, oy), str(i), fill=(255,200,0))
    for y in range(8):
        for x in range(6):
            if g[y] >> (5-x) & 1:
                d.rectangle([ox+x*S, oy+10+y*S, ox+x*S+S-1, oy+10+y*S+S-1], fill=(0,255,255))
            else:
                d.rectangle([ox+x*S, oy+10+y*S, ox+x*S+S-1, oy+10+y*S+S-1], fill=(0,0,0))
sheet.save(out + '.png')
import json; json.dump([list(g) for g in glyphs], open(out + '.json', 'w'))

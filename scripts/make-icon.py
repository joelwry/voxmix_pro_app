# Generates the VoxMix Pro launcher icons (legacy + adaptive + monochrome) with Pillow.
import os, sys, math
from PIL import Image, ImageDraw, ImageFilter

RES = sys.argv[1]
S = 1024  # working size for one 108 dp adaptive layer

def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))

def gradient(size, c1, c2, diagonal=True):
    img = Image.new('RGBA', (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            t = ((x + y) / (2 * size)) if diagonal else y / size
            px[x, y] = lerp(c1, c2, t) + (255,)
    return img

def background():
    bg = gradient(S, (18, 16, 48), (76, 29, 149))
    glow = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(glow)
    d.ellipse([S*0.18, S*0.12, S*0.82, S*0.76], fill=(34, 211, 238, 70))
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.09))
    return Image.alpha_composite(bg, glow)

def foreground(color=None):
    """Microphone with waveform bars, drawn inside the 66 dp safe zone (61 % of the layer)."""
    k = 4  # supersampling
    W = S * k
    layer = Image.new('RGBA', (W, W), (0, 0, 0, 0))
    mask = Image.new('L', (W, W), 0)
    d = ImageDraw.Draw(mask)
    cx = W / 2
    # capsule
    cw, ch = W * 0.17, W * 0.30
    top = W * 0.24
    d.rounded_rectangle([cx - cw/2, top, cx + cw/2, top + ch], radius=cw/2, fill=255)
    # holder arc (U shape)
    lw = int(W * 0.028)
    hw = W * 0.135
    d.arc([cx - hw - cw*0.18, top + ch*0.35, cx + hw + cw*0.18, top + ch + W*0.075], start=0, end=180, fill=255, width=lw)
    # stem + base
    stem_top = top + ch + W * 0.075
    d.rectangle([cx - lw/2, stem_top - lw*0.3, cx + lw/2, stem_top + W*0.075], fill=255)
    d.rounded_rectangle([cx - W*0.085, stem_top + W*0.07, cx + W*0.085, stem_top + W*0.07 + lw], radius=lw/2, fill=255)
    # waveform bars either side
    bars = [0.10, 0.17, 0.25, 0.14]
    bw = W * 0.026
    for side in (-1, 1):
        for i, h in enumerate(bars):
            x = cx + side * (W * 0.150 + i * W * 0.044)
            yc = top + ch * 0.55
            hh = W * h
            d.rounded_rectangle([x - bw/2, yc - hh/2, x + bw/2, yc + hh/2], radius=bw/2, fill=255)
    if color is not None:
        fill = Image.new('RGBA', (W, W), color + (255,))
    else:
        fill = gradient(W, (34, 211, 238), (167, 139, 250))
    layer.paste(fill, (0, 0), mask)
    if color is None:
        # grille lines on the capsule
        g = ImageDraw.Draw(layer)
        for i in range(1, 5):
            y = top + ch * i / 5.2
            g.line([cx - cw*0.32, y, cx + cw*0.32, y], fill=(18, 16, 48, 150), width=int(W*0.008))
    return layer.resize((S, S), Image.LANCZOS)

bg = background()
fg = foreground()
mono = foreground(color=(255, 255, 255))

dens = {'mdpi': 1, 'hdpi': 1.5, 'xhdpi': 2, 'xxhdpi': 3, 'xxxhdpi': 4}
full = Image.alpha_composite(bg, fg)
for name, f in dens.items():
    d = os.path.join(RES, f'mipmap-{name}')
    os.makedirs(d, exist_ok=True)
    # adaptive layers: 108 dp
    n = int(108 * f)
    fg.resize((n, n), Image.LANCZOS).save(os.path.join(d, 'ic_launcher_foreground.png'))
    bg.resize((n, n), Image.LANCZOS).convert('RGB').save(os.path.join(d, 'ic_launcher_background.png'))
    mono.resize((n, n), Image.LANCZOS).save(os.path.join(d, 'ic_launcher_monochrome.png'))
    # legacy icons: 48 dp, the visible 72 dp centre of the layers scaled to fit
    m = int(48 * f)
    crop = full.crop((int(S*18/108), int(S*18/108), int(S*90/108), int(S*90/108))).resize((m, m), Image.LANCZOS)
    sq = Image.new('L', (m, m), 0)
    ImageDraw.Draw(sq).rounded_rectangle([0, 0, m - 1, m - 1], radius=int(m * 0.22), fill=255)
    out = Image.new('RGBA', (m, m), (0, 0, 0, 0)); out.paste(crop, (0, 0), sq)
    out.save(os.path.join(d, 'ic_launcher.png'))
    rd = Image.new('L', (m, m), 0)
    ImageDraw.Draw(rd).ellipse([0, 0, m - 1, m - 1], fill=255)
    outr = Image.new('RGBA', (m, m), (0, 0, 0, 0)); outr.paste(crop, (0, 0), rd)
    outr.save(os.path.join(d, 'ic_launcher_round.png'))
# store-size preview
full.crop((int(S*18/108), int(S*18/108), int(S*90/108), int(S*90/108))).resize((512, 512), Image.LANCZOS).save(os.path.join(sys.argv[2], 'icon-512.png'))
print('ok')

"""Original Winlator S11 wallpaper: a calm gradient with soft light ribbons.

Usage: python -I make_wallpaper.py <light|dark> <W> <H> <out.png|out.bmp>
"""
import sys

import numpy as np
from PIL import Image

mode, W, H, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]

y, x = np.mgrid[0:H, 0:W].astype(np.float32)
u, v = x / W, y / H  # 0..1
aspect = W / H


def rgb(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], np.float32) / 255.0


def smoothstep(e0, e1, t):
    t = np.clip((t - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


if mode == "dark":
    top, bottom = rgb("#071228"), rgb("#0d2a57")
    glows = [((0.80, 0.28), 0.55, rgb("#2d6ee6"), 0.55),
             ((0.12, 0.88), 0.50, rgb("#138f95"), 0.40),
             ((0.98, 0.98), 0.45, rgb("#5b3fd6"), 0.40)]
    cores = [rgb("#a8d0ff"), rgb("#c3b5ff"), rgb("#8ff3e9")]
    halos = [rgb("#2f7bff"), rgb("#6f55ff"), rgb("#19b5ab")]
    core_gain, halo_gain = 0.85, 0.45
else:
    top, bottom = rgb("#f1f6fd"), rgb("#cfdff5")
    glows = [((0.80, 0.22), 0.55, rgb("#ffffff"), 0.70),
             ((0.10, 0.92), 0.55, rgb("#a3c6f1"), 0.50),
             ((0.98, 0.98), 0.45, rgb("#c4b5f4"), 0.45)]
    cores = [rgb("#1e6fd9"), rgb("#6553e0"), rgb("#0e9c99")]
    halos = [rgb("#ffffff"), rgb("#ffffff"), rgb("#ffffff")]
    core_gain, halo_gain = 0.75, 0.75

img = top * (1 - v[..., None]) + bottom * v[..., None]
for (cx, cy), radius, color, strength in glows:
    a = strength * np.exp(-(((u - cx) * aspect) ** 2 + (v - cy) ** 2) / radius ** 2)
    img = img * (1 - a[..., None]) + color * a[..., None]

# Ribbons sweep from the lower left to the upper right along slow sine curves:
# a wide halo first, then a thin bright core on top.
ribbons = [  # (y at x=0, slope, amplitude, frequency, phase, core width, colour)
    (0.90, -0.55, 0.06, 1.3, 0.2, 0.0045, 0),
    (0.82, -0.48, 0.08, 1.1, 1.4, 0.0035, 1),
    (0.97, -0.62, 0.05, 1.6, 2.2, 0.0030, 2),
    (0.76, -0.40, 0.07, 0.9, 3.1, 0.0025, 0),
    (0.86, -0.35, 0.10, 0.7, 4.0, 0.0020, 2),
]
fade = 0.45 + 0.55 * smoothstep(-0.05, 0.55, u)  # a little fainter on the left
for base, slope, amp, freq, phase, width, ci in ribbons:
    cy = base + slope * u + amp * np.sin(2 * np.pi * freq * u + phase)
    d = np.abs(v - cy)
    halo = halo_gain * 0.5 * np.exp(-(d / (width * 14)) ** 2) * fade
    img = img * (1 - halo[..., None]) + halos[ci] * halo[..., None]
    core = core_gain * np.exp(-(d / width) ** 2) * fade
    col = cores[ci] * (1 - u[..., None]) + cores[(ci + 1) % 3] * u[..., None]
    img = img * (1 - core[..., None]) + col * core[..., None]

# A little noise so the 8-bit gradients do not band
rng = np.random.default_rng(11)
img = np.clip(img * 255 + rng.uniform(-0.7, 0.7, img.shape), 0, 255).astype(np.uint8)
Image.fromarray(img, "RGB").save(out)
print(out, (W, H))

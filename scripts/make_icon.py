#!/usr/bin/env python3
"""
Genera resources/app.ico — icona di meetingrec (32-bit, dimensioni multiple).
Nessuna dipendenza esterna (solo stdlib). Uso: python3 scripts/make_icon.py
"""
import math
import os
import struct

SIZES = [16, 24, 32, 48, 64, 128, 256]
SS = 3  # supersampling per anti-alias
OUT = os.path.join(os.path.dirname(__file__), "..", "resources", "app.ico")


def clamp(v, a, b):
    return max(a, min(b, v))


def mix(c1, c2, t):
    return tuple(int(round(c1[i] + (c2[i] - c1[i]) * t)) for i in range(3))


def sd_rounded_rect(px, py, cx, cy, hw, hh, r):
    qx = abs(px - cx) - (hw - r)
    qy = abs(py - cy) - (hh - r)
    ax, ay = max(qx, 0.0), max(qy, 0.0)
    return math.hypot(ax, ay) + min(max(qx, qy), 0.0) - r


def sd_circle(px, py, cx, cy, r):
    return math.hypot(px - cx, py - cy) - r


def cov(d):
    return clamp(0.5 - d, 0.0, 1.0)


def draw(size):
    """Ritorna lista di righe (top-down) di pixel BGRA."""
    c = size / 2.0
    bg1 = (30, 58, 138)     # blue-900
    bg2 = (124, 58, 237)    # violet-600
    white = (255, 255, 255)

    # geometria del microfono (coordinate normalizzate al centro)
    body_hw, body_hh, body_r = size * 0.15, size * 0.17, size * 0.085
    ring_cy = c + size * 0.28
    ring_r = size * 0.165
    ring_t = size * 0.055
    base_hw, base_hh, base_r = size * 0.17, size * 0.032, size * 0.02
    base_cy = ring_cy + ring_r + size * 0.025

    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            acc = [0.0] * 4
            for dy in range(SS):
                for dx in range(SS):
                    px = x + (dx + 0.5) / SS
                    py = y + (dy + 0.5) / SS

                    d_bg = sd_rounded_rect(px, py, c, c, size * 0.47, size * 0.47, size * 0.13)
                    cov_bg = cov(d_bg)
                    if cov_bg <= 0.0:
                        continue

                    d_body = sd_rounded_rect(px, py, c, c, body_hw, body_hh, body_r)
                    d_ring = abs(sd_circle(px, py, c, ring_cy, ring_r)) - ring_t / 2.0
                    d_base = sd_rounded_rect(px, py, c, base_cy, base_hw, base_hh, base_r)

                    mic = max(cov(d_body), cov(d_ring), cov(d_base))
                    if mic > 0.0:
                        r, g, b = white
                    else:
                        t = clamp(py / size, 0.0, 1.0)
                        r, g, b = mix(bg1, bg2, t)
                        # leggera vignettatura
                        shade = 1.0 - 0.18 * clamp(abs(py - c) / (size * 0.5), 0.0, 1.0)
                        r, g, b = int(r * shade), int(g * shade), int(b * shade)

                    a = cov_bg * 255.0
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = SS * SS
            if acc[3] > 0:
                r = int(round(acc[0] / acc[3]))
                g = int(round(acc[1] / acc[3]))
                b = int(round(acc[2] / acc[3]))
                a = int(round(acc[3] / n))
            else:
                r = g = b = a = 0
            row.append((b, g, r, a))  # BGRA
        rows.append(row)
    return rows


def bmp_data(size, rows):
    """Codifica BITMAPINFOHEADER + pixel BGRA bottom-up + AND mask."""
    header = struct.pack(
        "<IiiHHIIiiII",
        40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0,
    )
    pixels = b""
    for y in range(size - 1, -1, -1):  # bottom-up
        for x in range(size):
            pixels += bytes(rows[y][x])
    and_mask = b"\x00" * ((size + 31) // 32 * 4) * size
    return header + pixels + and_mask


def main():
    images = []
    for s in SIZES:
        rows = draw(s)
        data = bmp_data(s, rows)
        images.append((s, data))

    count = len(images)
    icondir = struct.pack("<HHH", 0, 1, count)
    offset = 6 + 16 * count
    entries = b""
    for s, data in images:
        entries += struct.pack(
            "<BBBBHHII",
            s % 256, s % 256, 0, 0, 1, 32, len(data), offset,
        )
        offset += len(data)

    with open(OUT, "wb") as f:
        f.write(icondir + entries)
        for _, data in images:
            f.write(data)

    print(f"OK: {os.path.abspath(OUT)} ({count} dimensioni: {SIZES})")


if __name__ == "__main__":
    main()

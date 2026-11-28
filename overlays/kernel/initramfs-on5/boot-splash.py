#!/usr/bin/env python3
import sys

WIDTH, HEIGHT = 720, 1280
glyphs = {
    "Z": ("11110", "00010", "00100", "01000", "11110"),
    "I": ("11111", "00100", "00100", "00100", "11111"),
    "R": ("11110", "10001", "11110", "10100", "10010"),
    "A": ("01110", "10001", "11111", "10001", "10001"),
    "C": ("01111", "10000", "10000", "10000", "01111"),
    "H": ("10001", "10001", "11111", "10001", "10001"),
    "b": ("10000", "10110", "11001", "10001", "11110"),
    "o": ("00000", "01110", "10001", "10001", "01110"),
    "t": ("00100", "11110", "00100", "00100", "00011"),
    "i": ("00100", "00000", "01100", "00100", "01110"),
    "n": ("00000", "10110", "11001", "10001", "10001"),
    "g": ("00000", "01111", "10001", "01111", "00001"),
    ".": ("00000", "00000", "00000", "00110", "00110"),
}
background = bytes((13, 18, 20, 255))
foreground = bytes((238, 238, 232, 255))
pixels = bytearray(background * (WIDTH * HEIGHT))

def draw(text, x, baseline, scale):
    cursor = x
    for char in text:
        glyph = glyphs.get(char, ("00000",) * 5)
        for row, pattern in enumerate(glyph):
            for column, value in enumerate(pattern):
                if value != "1":
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        px = cursor + column * scale + sx
                        py = baseline + row * scale + sy
                        start = (py * WIDTH + px) * 4
                        pixels[start:start + 4] = foreground
        cursor += 6 * scale

title = "ZIRARCH"
subtitle = "booting..."
draw(title, (WIDTH - len(title) * 6 * 8) // 2, 544, 8)
draw(subtitle, (WIDTH - len(subtitle) * 6 * 4) // 2, 616, 4)
with open(sys.argv[1], "wb") as output:
    output.write(pixels)

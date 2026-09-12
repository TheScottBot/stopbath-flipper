#!/usr/bin/env python3
"""
Generates icon.png, the 10 by 10 pixel application icon shown beside the name in
the Flipper app menu (`fap_icon` in application.fam). The design is a Wi-Fi
signal: three nested arcs over a dot, the peripheral's whole purpose in the size
the menu allows.

Usage:
    python3 scripts/generate_icon.py            # writes icon.png
    python3 scripts/generate_icon.py icon.png preview.png   # also a zoomed preview

Edit ICON below to change the glyph: '#' is a drawn (black) pixel, '.' is empty.
The Flipper draws black pixels on the yellow screen, so '#' is what shows. Output
is an 8 bit grayscale PNG, which the firmware asset pipeline thresholds to one
bit. No third party library is used, so the standard library is enough.
"""
import struct
import sys
import zlib

# 10 by 10. '#' = drawn (black), '.' = empty.
ICON = [
    "..........",
    "..######..",
    ".#......#.",
    "...####...",
    "..#....#..",
    "....##....",
    "...#..#...",
    "..........",
    "....##....",
    "..........",
]


def to_pixels(rows):
    return [0 if character == "#" else 255 for row in rows for character in row]


def write_png(path, width, height, pixels):
    def chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type: none
        for x in range(width):
            raw.append(pixels[y * width + x])
    signature = b"\x89PNG\r\n\x1a\n"
    header = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)  # 8 bit grayscale
    with open(path, "wb") as handle:
        handle.write(
            signature
            + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b"")
        )


def write_preview(path, rows, scale=30):
    width, height = len(rows[0]), len(rows)
    scaled_width, scaled_height = width * scale, height * scale
    pixels = [255] * (scaled_width * scaled_height)
    for y in range(height):
        for x in range(width):
            value = 0 if rows[y][x] == "#" else 235
            for dy in range(scale):
                for dx in range(scale):
                    grid_line = dx == 0 or dy == 0
                    pixels[(y * scale + dy) * scaled_width + (x * scale + dx)] = (
                        200 if (grid_line and value == 255) else value
                    )
    write_png(path, scaled_width, scaled_height, pixels)


def main():
    icon_path = sys.argv[1] if len(sys.argv) > 1 else "icon.png"
    write_png(icon_path, 10, 10, to_pixels(ICON))
    message = "wrote " + icon_path
    if len(sys.argv) > 2:
        write_preview(sys.argv[2], ICON)
        message += " and " + sys.argv[2]
    print(message)


if __name__ == "__main__":
    main()

"""Draw the Brainwave WiFi icon (WiFi waves on a rounded square) and write it as a 32 and 16 pixel .ico."""

import argparse
import math
import struct

BACKGROUND = (0x22, 0x30, 0x4f)
WAVES = (0x4f, 0xd8, 0xb0)
SUPERSAMPLE = 4


def rounded_square_covers(x, y, size, radius):
    nearest_x = min(max(x, radius), size - radius)
    nearest_y = min(max(y, radius), size - radius)
    return (x - nearest_x) ** 2 + (y - nearest_y) ** 2 <= radius ** 2


def waves_cover(x, y, size):
    centre_x = size / 2
    centre_y = size * 0.78
    dx = x - centre_x
    dy = centre_y - y
    distance = math.hypot(dx, dy)
    dot_radius = size * (0.085 if size > 16 else 0.1)
    if distance <= dot_radius:
        return True
    if dy <= 0 or abs(math.atan2(dx, dy)) > math.radians(46):
        return False
    rings = (0.25, 0.42, 0.59) if size > 16 else (0.31, 0.56)
    thickness = size * (0.085 if size > 16 else 0.12)
    for ring in rings:
        if abs(distance - size * ring) <= thickness / 2:
            return True
    return False


def render(size):
    radius = size * 0.2
    pixels = []
    for row in range(size):
        for column in range(size):
            inside = 0
            wave = 0
            for sample_y in range(SUPERSAMPLE):
                for sample_x in range(SUPERSAMPLE):
                    x = column + (sample_x + 0.5) / SUPERSAMPLE
                    y = row + (sample_y + 0.5) / SUPERSAMPLE
                    if rounded_square_covers(x, y, size, radius):
                        inside += 1
                        if waves_cover(x, y, size):
                            wave += 1
            if inside * 2 < SUPERSAMPLE * SUPERSAMPLE:
                pixels.append(None)
                continue
            mix = wave / inside
            pixels.append(tuple(round(BACKGROUND[index] * (1 - mix) + WAVES[index] * mix) for index in range(3)))
    return pixels


def bitmap_entry(size):
    pixels = render(size)
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 24, 0, 0, 0, 0, 0, 0)
    colour_row = (size * 3 + 3) // 4 * 4
    mask_row = (size + 31) // 32 * 4
    colour = bytearray()
    mask = bytearray()
    for row in reversed(range(size)):
        line = bytearray()
        bits = bytearray(mask_row)
        for column in range(size):
            pixel = pixels[row * size + column]
            if pixel is None:
                line += b'\x00\x00\x00'
                bits[column // 8] |= 0x80 >> (column % 8)
            else:
                line += bytes((pixel[2], pixel[1], pixel[0]))
        colour += line + bytes(colour_row - len(line))
        mask += bits
    return header + colour + mask


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output')
    arguments = parser.parse_args()
    sizes = (32, 16)
    images = [bitmap_entry(size) for size in sizes]
    directory = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, image in zip(sizes, images):
        directory += struct.pack('<BBBBHHII', size, size, 0, 0, 1, 24, len(image), offset)
        offset += len(image)
    with open(arguments.output, 'wb') as handle:
        handle.write(directory + b''.join(images))


if __name__ == '__main__':
    main()

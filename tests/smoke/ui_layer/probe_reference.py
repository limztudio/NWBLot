"""Shared SDR marker encoding and authored UI atlas sampling references."""
from __future__ import annotations

import math


DOT_COLOR = (235, 245, 255)


def linear_rgb_bytes(color):
    def convert(channel):
        srgb = channel * 12.92 if channel <= 0.0031308 else 1.055 * channel ** (1.0 / 2.4) - 0.055
        return int(round(srgb * 255.0))
    return tuple(convert(channel) for channel in color)


def linear_channels(color):
    return tuple(channel / 255.0 / 12.92 if channel <= 10 else
        ((channel / 255.0 + 0.055) / 1.055) ** 2.4 for channel in color)


def encoded_marker(value, intensity=1.0):
    return linear_rgb_bytes(tuple(((value >> shift) & 15) / 15.0 * intensity for shift in (0, 4, 8)))


def compose(source, alpha, tint, backdrop):
    background = linear_channels(backdrop)
    opacity = alpha * tint[3]
    return linear_rgb_bytes(tuple(channel * tint[index] * opacity + background[index] * (1.0 - opacity)
        for index, channel in enumerate(source)))


def authored_texel(kind, x, y, fill, border):
    """Match the authored four-by-four straight-alpha tile before UASTC compression."""
    if not 0 <= x < 24 or not 0 <= y < 24:
        return (0.0, 0.0, 0.0), 0.0
    samples = []
    for sy in range(4):
        for sx in range(4):
            px, py = x + (sx + 0.5) / 4.0, y + (sy + 0.5) / 4.0
            if kind == "dot":
                if math.hypot(px - 12.0, py - 12.0) <= 5.0:
                    samples.append(DOT_COLOR)
            else:
                dx, dy = abs(px - 12.0) - 7.0, abs(py - 12.0) - 7.0
                distance = math.hypot(max(dx, 0.0), max(dy, 0.0)) + min(max(dx, dy), 0.0) - 5.0
                if distance <= 0.0:
                    samples.append(border if distance >= -1.5 else fill)
    if not samples:
        return (0.0, 0.0, 0.0), 0.0
    rgb = tuple(round(sum(sample[channel] for sample in samples) / len(samples)) for channel in range(3))
    return linear_channels(rgb), round(len(samples) * 255.0 / 16.0) / 255.0


def _sample_texel(kind, sx, sy, fill, border):
    x0, y0 = math.floor(sx), math.floor(sy)
    tx, ty = sx - x0, sy - y0
    channels, alpha = [0.0, 0.0, 0.0], 0.0
    for dx, dy, weight in ((0, 0, (1.0 - tx) * (1.0 - ty)), (1, 0, tx * (1.0 - ty)),
            (0, 1, (1.0 - tx) * ty), (1, 1, tx * ty)):
        rgb, coverage = authored_texel(kind, x0 + dx, y0 + dy, fill, border)
        alpha += coverage * weight
        for channel in range(3):
            channels[channel] += rgb[channel] * weight
    return tuple(channels), alpha


def sampled_tile(kind, rectangle, pixel, scale, fill, border, crop=(0.0, 0.0, 24.0, 24.0)):
    """Bilinear raw-tile sampling at the physical pixel center, with optional UV cropping."""
    x, y, width, height = rectangle
    sx = ((pixel[0] + 0.5) / scale[0] - x) / width * crop[2] + crop[0] - 0.5
    sy = ((pixel[1] + 0.5) / scale[1] - y) / height * crop[3] + crop[1] - 0.5
    return _sample_texel(kind, sx, sy, fill, border)


def sampled_region(rectangle, pixel, scale, fill, border):
    """Sample the shipped six-unit nine-slice at the physical pixel center."""
    x, y, width, height = rectangle
    px, py = (pixel[0] + 0.5) / scale[0] - x, (pixel[1] + 0.5) / scale[1] - y
    if not 0.0 <= px < width or not 0.0 <= py < height:
        return (0.0, 0.0, 0.0), 0.0

    def coordinate(position, extent):
        inset = min(6.0, extent / 2.0)
        if position < inset:
            return position / inset * 6.0 - 0.5
        if position >= extent - inset:
            return 18.0 + (position - extent + inset) / inset * 6.0 - 0.5
        return 6.0 + (position - inset) / (extent - 2.0 * inset) * 12.0 - 0.5

    return _sample_texel("panel", coordinate(px, width), coordinate(py, height), fill, border)

"""Pixel acceptance and exact direct/replay comparison for standalone UI raster command IR."""
from __future__ import annotations

import hashlib

from capture_reference import analyze_frame
from texture_image_probe import observe_texture_image, snapshot_from_logs
from window_capture_smoke import SmokeFailure


IMAGE_INITIAL_STATE = {
    "phase": 0,
    "focus_code": 0,
    "before_clicks": 0,
    "after_clicks": 0,
    "parent": 0,
    "child": 0,
    "popup_count": 0,
    "image_targets": 0,
    "declared_source": 1,
    "post_handle_empty": 1,
    "replacement_count": 0,
    "replacement_alternate": 0,
    "eviction_remaining": 0,
    "eviction_completed": 0,
    "builder_source": 1,
    "builder_handle_empty": 1,
}
CHANNEL_TOLERANCE = 1


def observe_scene(name, frame, log_text):
    if name == "startup":
        width, height, rows = frame
        maximum = max(channel for row in rows for pixel in row for channel in pixel)
        if maximum > 2:
            raise SmokeFailure(f"startup UI framebuffer contains nonblack pixels (maximum channel {maximum})")
        return {"passed": True, "extent": [width, height], "maximum_rgb_channel": maximum}
    if name == "paint":
        report = analyze_frame(frame)
        if not report["passed"]:
            raise SmokeFailure("passive UI paint pixel oracle rejected the captured frame")
        return report
    if name == "texture_image":
        snapshot = snapshot_from_logs(log_text)
        if snapshot is None:
            raise SmokeFailure("texture image snapshot was absent from the capture log")
        report = observe_texture_image(frame, snapshot, IMAGE_INITIAL_STATE, skin="alternate")
        if not report["passed"]:
            raise SmokeFailure("engine texture image pixel oracle rejected the captured frame")
        return report
    raise SmokeFailure(f"unknown UI parity scene: {name}")


def _digest_rows(rows):
    digest = hashlib.sha256()
    for row in rows:
        digest.update(bytes(channel for pixel in row for channel in pixel))
    return digest.hexdigest()


def compare_frames(direct, replay):
    direct_width, direct_height, direct_rows = direct
    replay_width, replay_height, replay_rows = replay
    direct_extent = (direct_width, direct_height)
    replay_extent = (replay_width, replay_height)
    if direct_extent != replay_extent:
        return {"passed": False, "direct_extent": direct_extent, "replay_extent": replay_extent,
            "reason": "framebuffer extents differ"}
    different_pixels = 0
    out_of_tolerance_pixels = 0
    maximum_channel_error = 0
    first_difference = None
    first_mismatch = None
    for y, (direct_row, replay_row) in enumerate(zip(direct_rows, replay_rows)):
        for x, (direct_rgb, replay_rgb) in enumerate(zip(direct_row, replay_row)):
            if direct_rgb == replay_rgb:
                continue
            different_pixels += 1
            error = max(abs(a - b) for a, b in zip(direct_rgb, replay_rgb))
            maximum_channel_error = max(maximum_channel_error, error)
            difference = {"position": [x, y], "direct_rgb": direct_rgb,
                "replay_rgb": replay_rgb, "maximum_channel_error": error}
            if first_difference is None:
                first_difference = difference
            if error > CHANNEL_TOLERANCE:
                out_of_tolerance_pixels += 1
                if first_mismatch is None:
                    first_mismatch = difference
    return {
        "passed": out_of_tolerance_pixels == 0,
        "extent": direct_extent,
        "compared_pixels": direct_width * direct_height,
        "channel_tolerance": CHANNEL_TOLERANCE,
        "different_pixels": different_pixels,
        "out_of_tolerance_pixels": out_of_tolerance_pixels,
        "maximum_channel_error": maximum_channel_error,
        "first_difference": first_difference,
        "first_mismatch": first_mismatch,
        "direct_sha256": _digest_rows(direct_rows),
        "replay_sha256": _digest_rows(replay_rows),
    }

#!/usr/bin/env python3
"""Text-area capture, caret and native pixel-sampling regression tests."""

import math
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

SMOKE_DIRECTORY = Path(__file__).resolve().parents[1] / "smoke"
sys.path.insert(0, str(SMOKE_DIRECTORY))
sys.path.insert(0, str(SMOKE_DIRECTORY / "ui_layer"))

import window_capture_smoke
import window_capture_x11
import window_capture_x11_pixels
import edit_probe
import numeric_edit_smoke
import text_area_probe
import text_area_smoke
from probe_reference import encoded_marker

LIT_CARET = 'caret'
LIT_COLLECT_LOG_DELTA = 'collect_log_delta'
LIT_CORNER = 'corner'
LIT_GEOMETRY_MATCHES = 'geometry_matches'
LIT_LOG = '*.log'
LIT_LOG_2 = 'log'
LIT_MEASURE = 'measure'
LIT_MODEL_MATCHES = 'model_matches'
LIT_MONOTONIC = 'monotonic'
LIT_NAME = 'name'
LIT_PASSED = 'passed'
LIT_PHASE_CAPTURE = 'phase_capture'
LIT_PROBES = 'probes'
LIT_READ = 'read'
LIT_RECTANGLES = 'rectangles'
LIT_SLEEP = 'sleep'
LIT_UNUSED = 'unused'
LIT_X_THUMB = 'x_thumb'
LIT_X_TRACK = 'x_track'
LIT_Y_THUMB = 'y_thumb'
LIT_Y_TRACK = 'y_track'


class TextAreaCaretCaptureTests(unittest.TestCase):
    @staticmethod
    def fixture(sequence, caret_on):
        values = dict.fromkeys(text_area_probe.FIELDS, 0)
        values.update(text_area_probe.document_fields("", 0, 0), focus=1, enabled=1, coherent=1)
        rectangles = dict.fromkeys(text_area_probe.RECTANGLES, (0.0, 0.0, 0.0, 0.0))
        rectangles.update(bounds=(32.0, 140.0, 380.0, 160.0), content=(40.0, 148.0, 364.0, 144.0), clip=(40.0, 148.0, 364.0, 144.0), caret=(40.0, 148.0, 1.0, 21.796875))
        lines = ["UiTextAreaSmoke: display logical=640x440 scale=1x1", "UiTextAreaSmoke: skin=default", f"UiTextAreaSmoke: state sequence={sequence} values=" + ",".join(str(values[name]) for name in text_area_probe.FIELDS),
            f"UiTextAreaSmoke: metrics sequence={sequence} scroll=0,0 measure=0,21.796875 line_height=21.796875 selections=0 maximum=0,0"]
        lines.extend(f"UiTextAreaSmoke: geometry sequence={sequence} {name}=" + ",".join(map(str, rectangle)) for name, rectangle in rectangles.items())
        text = "\n".join(lines)
        snapshot = text_area_probe.snapshot_from_logs(text)
        rows = [[(24, 29, 37)] * 640 for _ in range(440)]
        for y in range(140, 300):
            rows[y][32:412] = [(18, 26, 37)] * 380
        marker = 0
        for value, parts in zip((*snapshot["values"], sequence), text_area_probe.MARKER_PARTS):
            for part in range(parts):
                color = encoded_marker(value >> (part * 12))
                x = 11 + marker * 8
                for y in range(426, 429):
                    rows[y][x - 1:x + 2] = [color] * 3
                marker += 1
        if caret_on:
            for y in range(148, 170):
                rows[y][40] = (249, 252, 255)
        return text, snapshot, (640, 440, rows), values

    def checkpoint_with_refresh(self, refreshed_sequence):
        before, snapshot, off_frame, expected = self.fixture(7, False)
        _on_text, _on_snapshot, on_frame, _on_expected = self.fixture(7, True)
        refreshed, _refreshed_snapshot, _frame, _expected = self.fixture(refreshed_sequence, True)
        clock = SimpleNamespace(now=0.0, visible_until=0.0, samples=0, reads=0, logs=0, frame=None)
        events, reports = [], []
        backend = object.__new__(window_capture_smoke.LinuxX11Capture)
        backend.display, backend.root = object(), 1
        backend.x11 = mock.Mock()
        backend.get_attributes = mock.Mock(return_value=SimpleNamespace(width=640, height=440))
        backend._window_root_region = mock.Mock()

        def sleep(delay):
            clock.now += delay

        def read(*_args):
            clock.reads += 1
            on = clock.now < clock.visible_until
            events.append((LIT_READ, clock.reads, on))
            clock.frame = on_frame if on else off_frame
            clock.now += 0.05
            return SimpleNamespace(has_pixel_variation=True, appears_empty_or_white=False)

        def sample(*_args):
            clock.samples += 1
            if clock.samples == 2:
                clock.visible_until = clock.now + 0.125
            color = (249, 252, 255) if clock.now < clock.visible_until else (18, 26, 37)
            return [[color] for _ in range(3)]

        def collect(*_args):
            clock.logs += 1
            events.append((LIT_LOG_2, clock.logs))
            if clock.logs > 1:
                clock.now += 0.25
            return before if clock.logs == 1 else refreshed

        oracle = text_area_smoke.observe_text_area
        def observe(*args, **kwargs):
            report = oracle(*args, **kwargs)
            reports.append(report)
            return report

        backend._capture_drawable_region = mock.Mock(side_effect=read)
        backend.sample_client_pixels = mock.Mock(side_effect=sample)
        run = object.__new__(text_area_smoke.TextAreaRun)
        run.args = SimpleNamespace(output_directory=Path(LIT_UNUSED), skin="default")
        run.backend, run.handle, run.process = backend, 42, SimpleNamespace(poll=lambda: None)
        run.native = SimpleNamespace(windows=False, maintain_pointer=mock.Mock(), observe_window=lambda: {})
        run.log_directory, run.log_baseline, run.log_pattern = Path(LIT_UNUSED), {}, LIT_LOG
        run.deadline, run.snapshot, run.failure_report, run.stages, run.expected = 1.0, None, None, [], expected
        run.caret_phase_waits = 0
        run.write_report = mock.Mock()
        with mock.patch.object(text_area_smoke.time, LIT_MONOTONIC, side_effect=lambda: clock.now), \
             mock.patch.object(text_area_smoke.time, LIT_SLEEP, side_effect=sleep), \
             mock.patch.object(text_area_smoke, LIT_COLLECT_LOG_DELTA, side_effect=collect), \
             mock.patch.object(text_area_smoke, "read_bmp_24_rows", side_effect=lambda _path: clock.frame), \
             mock.patch.object(text_area_smoke, "observe_text_area", side_effect=observe):
            if refreshed_sequence == 7:
                run.checkpoint(LIT_PHASE_CAPTURE, settle=0.0)
            else:
                with self.assertRaisesRegex(window_capture_smoke.SmokeFailure, "displayed-state gate"):
                    run.checkpoint(LIT_PHASE_CAPTURE, settle=0.0)
        return run, events, reports, backend, clock

    def test_phase_capture_freezes_visible_pixels_before_slow_latest_log_refresh(self):
        run, events, reports, backend, clock = self.checkpoint_with_refresh(7)
        self.assertEqual(len(run.stages), 1)
        self.assertEqual(clock.samples, 2)
        self.assertEqual(clock.reads, 2)
        self.assertLess(events.index((LIT_READ, 2, True)), events.index((LIT_LOG_2, 2)))
        self.assertTrue(reports[1][LIT_PASSED])
        self.assertEqual(reports[1]["marker_count"], 57)
        self.assertTrue(next(probe for probe in reports[1][LIT_PROBES] if probe[LIT_NAME] == LIT_CARET)[LIT_PASSED])
        backend.x11.XRaiseWindow.assert_called_once()
        backend._window_root_region.assert_not_called()

    def test_newer_post_capture_snapshot_rejects_old_markers_despite_visible_caret(self):
        run, events, reports, _backend, _clock = self.checkpoint_with_refresh(8)
        self.assertEqual(run.stages, [])
        self.assertLess(events.index((LIT_READ, 2, True)), events.index((LIT_LOG_2, 2)))
        self.assertEqual(reports[1]["snapshot"]["sequence"], 8)
        self.assertTrue(reports[1][LIT_MODEL_MATCHES])
        self.assertTrue(reports[1][LIT_GEOMETRY_MATCHES])
        self.assertTrue(next(probe for probe in reports[1][LIT_PROBES] if probe[LIT_NAME] == LIT_CARET)[LIT_PASSED])
        self.assertEqual([probe[LIT_NAME] for probe in reports[1][LIT_PROBES] if not probe[LIT_PASSED]], ["model_25_0"])
        self.assertFalse(reports[1][LIT_PASSED])


class CaretPixelColumnTests(unittest.TestCase):
    def test_half_pixel_boundaries_choose_the_covered_column(self):
        cases = ((333.0, 333), (333.5, 333), (334.5, 334), (335.5, 335),
            (math.nextafter(333.5, -math.inf), 333), (math.nextafter(333.5, math.inf), 334))
        for left, expected_column in cases:
            with self.subTest(left=left):
                self.assertEqual(edit_probe.caret_pixel_column(left, 1.0), expected_column)

    def test_scale_is_applied_before_the_physical_half_pixel_offset(self):
        for left, scale, expected_column in ((10.4, 2.0, 21), (10.25, 2.0, 20), (667.0, 0.5, 333)):
            with self.subTest(left=left, scale=scale):
                self.assertEqual(edit_probe.caret_pixel_column(left, scale), expected_column)

    def test_edit_and_numeric_phase_samples_use_the_same_one_pixel_column(self):
        caret, content = (333.5, 148.0, 1.0, 21.796875), (40.0, 148.0, 364.0, 144.0)
        self.assertEqual(edit_probe.caret_probe_position(caret, content, 1.0, 1.0, 640, 440)[0], 333)
        run = object.__new__(numeric_edit_smoke.NumericEditRun)
        run.backend, run.handle = SimpleNamespace(client_size=lambda _handle: (640, 440)), 42
        snapshot = {"scale": (1.0, 1.0), "enabled": 1, "readonly": 0, "i_focus": 1, "i_anchor": 32, "i_caret": 32,
            LIT_RECTANGLES: {"integer_caret": caret, "integer_content": content}}
        self.assertEqual(run._caret_probe_sample(snapshot)[0], 333)

    def test_text_area_oracle_requires_the_covered_column_and_rejects_an_adjacent_decoy(self):
        _text, snapshot, frame, expected = TextAreaCaretCaptureTests.fixture(7, False)
        snapshot[LIT_RECTANGLES][LIT_CARET] = (333.5, 148.0, 1.0, 21.796875)
        rows = frame[2]
        for y in range(148, 170):
            rows[y][333] = (249, 252, 255)
        report = text_area_probe.observe_text_area(frame, snapshot, expected)
        self.assertTrue(report[LIT_PASSED])
        self.assertEqual(next(probe for probe in report[LIT_PROBES] if probe[LIT_NAME] == LIT_CARET)["position"][0], 333)
        for y in range(148, 170):
            rows[y][333], rows[y][334] = (18, 26, 37), (249, 252, 255)
        report = text_area_probe.observe_text_area(frame, snapshot, expected)
        self.assertTrue(report[LIT_MODEL_MATCHES])
        self.assertTrue(report[LIT_GEOMETRY_MATCHES])
        self.assertEqual([probe[LIT_NAME] for probe in report[LIT_PROBES] if not probe[LIT_PASSED]], [LIT_CARET])
        self.assertFalse(report[LIT_PASSED])


class TextAreaVisualMeasurementTests(unittest.TestCase):
    def test_visual_extent_matches_scroll_maximum_and_stale_logical_extent_is_rejected(self):
        _text, snapshot, frame, expected = TextAreaCaretCaptureTests.fixture(7, True)
        snapshot[LIT_MEASURE], snapshot["maximum"] = (533.09375, 370.54688), (184.09375, 240.54688)
        rectangles = snapshot[LIT_RECTANGLES]
        rectangles["content"] = rectangles["clip"] = (40.0, 148.0, 350.0, 130.0)
        rectangles[LIT_X_TRACK], rectangles[LIT_Y_TRACK] = (40.0, 278.0, 350.0, 14.0), (390.0, 148.0, 14.0, 130.0)
        rectangles[LIT_X_THUMB] = (40.0, 278.0, 350.0 * 350.0 / 534.09375, 14.0)
        rectangles[LIT_Y_THUMB] = (390.0, 148.0, 14.0, 130.0 * 130.0 / 370.54688)
        rectangles[LIT_CORNER] = (390.0, 278.0, 14.0, 14.0)
        rows = frame[2]
        def fill(rectangle, color):
            x, y, width, height = rectangle
            for row in range(math.ceil(y - 0.5), math.ceil(y + height - 0.5)):
                for column in range(math.ceil(x - 0.5), math.ceil(x + width - 0.5)):
                    rows[row][column] = color
        for name in (LIT_X_TRACK, LIT_Y_TRACK, LIT_CORNER):
            fill(rectangles[name], (24, 29, 37))
        for name in (LIT_X_THUMB, LIT_Y_THUMB):
            fill(rectangles[name], (45, 60, 80))
        report = text_area_probe.observe_text_area(frame, snapshot, expected)
        self.assertTrue(report[LIT_MODEL_MATCHES])
        self.assertTrue(report[LIT_GEOMETRY_MATCHES])
        self.assertTrue(all(probe[LIT_PASSED] for probe in report[LIT_PROBES]))
        self.assertTrue(report[LIT_PASSED])
        snapshot[LIT_MEASURE] = (532.59375, 370.54688)
        report = text_area_probe.observe_text_area(frame, snapshot, expected)
        self.assertTrue(report[LIT_MODEL_MATCHES])
        self.assertTrue(all(probe[LIT_PASSED] for probe in report[LIT_PROBES]))
        self.assertFalse(report[LIT_GEOMETRY_MATCHES])
        self.assertFalse(report[LIT_PASSED])


class PixelSampleBackendTests(unittest.TestCase):
    def test_linux_sample_rejects_outside_or_empty_regions_before_request(self):
        backend = object.__new__(window_capture_smoke.LinuxX11Capture)
        backend._validated_window_size = mock.Mock(return_value=(100, 80))
        backend.x11 = mock.Mock()
        for rectangle in ((-1, 0, 1, 1), (0, -1, 1, 1), (0, 0, 0, 1), (0, 0, 1, 0), (99, 0, 2, 1), (0, 79, 1, 2)):
            with self.subTest(rectangle=rectangle), self.assertRaises(window_capture_smoke.SmokeFailure):
                backend.sample_client_pixels(42, *rectangle)
        backend.x11.XGetImage.assert_not_called()

    def test_linux_sample_releases_image_when_pixel_conversion_fails(self):
        backend = object.__new__(window_capture_smoke.LinuxX11Capture)
        backend._validated_window_size = mock.Mock(return_value=(100, 80))
        backend.display = object()
        backend.x11 = mock.Mock()
        image = SimpleNamespace(contents=SimpleNamespace(bits_per_pixel=32, data=1, byte_order=backend.LSB_FIRST))
        backend.x11.XGetImage.return_value = image
        with mock.patch.object(window_capture_x11_pixels.NativePixelCodec, "ximage_rgb_rows", side_effect=ValueError("invalid image")), \
             self.assertRaises(ValueError):
            backend.sample_client_pixels(42, 10, 20, 1, 3)
        backend.x11.XDestroyImage.assert_called_once_with(image)


if __name__ == "__main__":
    unittest.main()

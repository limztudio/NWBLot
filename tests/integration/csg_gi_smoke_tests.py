#!/usr/bin/env python3
"""Reject GI comparisons without live post-presentation warmup and exact readback source evidence."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from csg_gi_smoke import validate_capture_evidence  # noqa: E402
from window_capture_smoke import SmokeFailure  # noqa: E402


EVIDENCE = """GiTestSmokeProject: CSG GI live warmup start source_frame=361 presentations=360 required_presentations=360 required_seconds=30
GiTestSmokeProject: CSG GI live warmup end source_frame=512 presentations=511 elapsed_seconds=30.000001
FramebufferCapture: capture ready
FramebufferCapture: graphics source frame 512
GiTestSmokeProject: CSG GI capture source_frame=512
"""


class CsgGiCaptureEvidenceTests(unittest.TestCase):
    def test_fixed_frame_only_capture_cannot_qualify_settled_gi(self):
        with self.assertRaisesRegex(SmokeFailure, "start evidence"):
            validate_capture_evidence("FramebufferCapture: graphics source frame 360\n", 360)

    def test_each_missing_receipt_or_interval_is_rejected(self):
        validate_capture_evidence(EVIDENCE, 360)
        lines = EVIDENCE.splitlines(keepends=True)
        for index in (0, 1, 3, 4):
            with self.subTest(index=index), self.assertRaises(SmokeFailure):
                validate_capture_evidence("".join(line for slot, line in enumerate(lines) if slot != index), 360)

    def test_compilation_time_cannot_replace_post_frame_warmup(self):
        for count in (0, 359):
            with self.subTest(count=count), self.assertRaisesRegex(SmokeFailure, "began before"):
                validate_capture_evidence(EVIDENCE.replace("presentations=360 required", f"presentations={count} required"), 360)

    def test_reduced_or_mismatched_warmup_count_is_rejected(self):
        with self.assertRaisesRegex(SmokeFailure, "at least 360"):
            validate_capture_evidence(EVIDENCE, 359)
        with self.assertRaisesRegex(SmokeFailure, "began before"):
            validate_capture_evidence(EVIDENCE, 361)

    def test_short_or_nonfinite_elapsed_interval_is_rejected(self):
        for value in ("29.999999", "0", "-30", "nan", "inf", "-inf"):
            with self.subTest(value=value), self.assertRaisesRegex(SmokeFailure, "completed 30-second"):
                validate_capture_evidence(EVIDENCE.replace("elapsed_seconds=30.000001", f"elapsed_seconds={value}"), 360)

    def test_malformed_numeric_interval_reports_a_smoke_failure(self):
        for field, current in (("required_seconds", "30"), ("elapsed_seconds", "30.000001")):
            for value in ("garbage", "30seconds", "0x1e", "1e", "++30"):
                with self.subTest(field=field, value=value), self.assertRaisesRegex(SmokeFailure, "malformed numeric"):
                    validate_capture_evidence(EVIDENCE.replace(f"{field}={current}", f"{field}={value}"), 360)

    def test_lower_or_nonfinite_declared_interval_is_rejected(self):
        for value in ("0", "29", "nan", "inf"):
            with self.subTest(value=value), self.assertRaisesRegex(SmokeFailure, "require 30"):
                validate_capture_evidence(EVIDENCE.replace("required_seconds=30", f"required_seconds={value}"), 360)

    def test_elapsed_idle_without_live_presentation_progress_is_rejected(self):
        for before, after in (("source_frame=512 presentations=511", "source_frame=361 presentations=511"),
                              ("source_frame=512 presentations=511", "source_frame=512 presentations=360")):
            with self.subTest(after=after), self.assertRaisesRegex(SmokeFailure, "presentation progress"):
                validate_capture_evidence(EVIDENCE.replace(before, after), 360)

    def test_stale_or_missing_source_identity_is_rejected(self):
        for value in ("511", "513", str(2**64 - 1)):
            with self.subTest(value=value), self.assertRaisesRegex(SmokeFailure, "capture source"):
                validate_capture_evidence(EVIDENCE.replace("capture source_frame=512", f"capture source_frame={value}"), 360)

    def test_same_old_readback_and_fixture_source_cannot_precede_live_interval(self):
        altered = EVIDENCE.replace("graphics source frame 512", "graphics source frame 511")
        altered = altered.replace("capture source_frame=512", "capture source_frame=511")
        with self.assertRaisesRegex(SmokeFailure, "capture source"):
            validate_capture_evidence(altered, 360)

    def test_duplicate_evidence_cannot_select_a_favorable_interval(self):
        for line in EVIDENCE.splitlines():
            if "capture ready" in line:
                continue
            with self.subTest(line=line), self.assertRaisesRegex(SmokeFailure, "exactly one"):
                validate_capture_evidence(EVIDENCE + line + "\n", 360)

    def test_out_of_order_source_receipts_are_rejected(self):
        lines = EVIDENCE.splitlines(keepends=True)
        for left, right in ((0, 1), (1, 3), (3, 4)):
            altered = lines.copy()
            altered[left], altered[right] = altered[right], altered[left]
            with self.subTest(left=left, right=right), self.assertRaisesRegex(SmokeFailure, "out of order"):
                validate_capture_evidence("".join(altered), 360)


if __name__ == "__main__":
    unittest.main()

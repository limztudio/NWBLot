from __future__ import annotations

import struct

from baseline_warmup_evidence import BaselineWarmupEvidence


LIT_F = "f"
LIT_NWB_GI_SMOKE_MIN_SETTLE_SECONDS = "NWB_GI_SMOKE_MIN_SETTLE_SECONDS"
LIT_CAPTURE_SOURCE_FRAME = "capture_source_frame"
LIT_SURFEL_GI_COMPLEX = "surfel-gi-complex"
LIT_GRAPHICS_SOURCE_FRAME_902 = "graphics source frame 902"
LIT_GRAPHICS_SOURCE_FRAME_900 = "graphics source frame 900"
LIT_NAN = "nan"
LIT_INF = "inf"


class BaselineWarmupChecks:
    @staticmethod
    def verify(harness):
        complex_profile = harness.get_profile(LIT_SURFEL_GI_COMPLEX)
        complex_environment = dict(complex_profile.frozen_environment)
        complex_evidence = (
            "GiTestSmokeProject: complex GI live warmup start graphics_frame=361 presentations=360 required_presentations=360 required_seconds=30\n"
            "GiTestSmokeProject: complex GI live warmup end graphics_frame=901 presentations=900 elapsed_seconds=30.125\n"
            "FramebufferCapture: capture ready\n"
            "FramebufferCapture: graphics source frame 902\n"
            "GiTestSmokeProject: complex GI capture source_frame=902\n"
        )
        assert BaselineWarmupEvidence.validate(complex_evidence, complex_profile, complex_environment)[LIT_CAPTURE_SOURCE_FRAME] == 902
        for old, replacement in (
            ("start graphics_frame=361 presentations=360", "start graphics_frame=361 presentations=359"),
            ("elapsed_seconds=30.125", "elapsed_seconds=29.999999"),
            ("elapsed_seconds=30.125", "elapsed_seconds=nan"),
            ("elapsed_seconds=30.125", "elapsed_seconds=invalid"),
            ("elapsed_seconds=30.125", "elapsed_seconds=3_0.125"),
            ("required_seconds=30", "required_seconds=10"),
            ("end graphics_frame=901 presentations=900", "end graphics_frame=361 presentations=900"),
            ("end graphics_frame=901 presentations=900", "end graphics_frame=901 presentations=360"),
            ("capture source_frame=902", "capture source_frame=901"),
            (LIT_GRAPHICS_SOURCE_FRAME_902, LIT_GRAPHICS_SOURCE_FRAME_900),
        ):
            try:
                BaselineWarmupEvidence.validate(complex_evidence.replace(old, replacement), complex_profile, complex_environment)
            except harness.SmokeFailure:
                pass
            else:
                raise AssertionError(f"complex GI accepted invalid live warmup evidence: {replacement}")
        for line in complex_evidence.splitlines(keepends=True):
            if "capture ready" in line:
                continue
            for altered in (complex_evidence.replace(line, ""), complex_evidence + line):
                try:
                    BaselineWarmupEvidence.validate(altered, complex_profile, complex_environment)
                except harness.SmokeFailure:
                    pass
                else:
                    raise AssertionError("complex GI accepted missing or repeated live warmup evidence")
        for value in ("10", LIT_NAN, LIT_INF, "invalid", "1e300"):
            try:
                BaselineWarmupEvidence.validate(complex_evidence, complex_profile, {**complex_environment, LIT_NWB_GI_SMOKE_MIN_SETTLE_SECONDS: value})
            except harness.SmokeFailure:
                pass
            else:
                raise AssertionError("complex GI accepted an invalid configured live warmup")
        rounded_requirement = struct.unpack(LIT_F, struct.pack(LIT_F, 30.00002))[0]
        rounded_evidence = complex_evidence.replace("required_seconds=30", f"required_seconds={rounded_requirement!r}").replace("elapsed_seconds=30.125", f"elapsed_seconds={rounded_requirement!r}")
        BaselineWarmupEvidence.validate(rounded_evidence, complex_profile, {**complex_environment, LIT_NWB_GI_SMOKE_MIN_SETTLE_SECONDS: "30.00002"})
        for altered in ("".join(reversed(complex_evidence.splitlines(keepends=True))), complex_evidence.replace(LIT_GRAPHICS_SOURCE_FRAME_902, LIT_GRAPHICS_SOURCE_FRAME_900).replace("capture source_frame=902", "capture source_frame=900")):
            try:
                BaselineWarmupEvidence.validate(altered, complex_profile, complex_environment)
            except harness.SmokeFailure:
                pass
            else:
                raise AssertionError("complex GI accepted reordered or stale readback evidence")
        return complex_profile, complex_environment, complex_evidence

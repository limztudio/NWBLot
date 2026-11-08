"""Compare captured CSG context transition images."""

import hashlib

from reflection_csg_context_evidence import LIT_PHASE, LIT_NAME, LIT_CASE
from reflection_optical_smoke import OpticalCapture
from reflection_optical_image_evidence import ReflectionOpticalImageEvidence
from window_capture_image import read_bmp_24_rows

LIT_CONTEXT_TRANSITION_BMP = "context_transition.bmp"


class ReflectionCsgContextImageEvidence:
    @staticmethod
    def analyze_images(directory, evidence):
        frames, metrics = [], {}
        for item in evidence:
            phase = item[LIT_PHASE]
            path = directory / (LIT_CONTEXT_TRANSITION_BMP if phase == 4 else f"context_transition.bmp.{phase}.bmp")
            frame = read_bmp_24_rows(path)
            frames.append(frame)
            metrics[item[LIT_NAME]] = {"image": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), **ReflectionOpticalImageEvidence.analyze_image(frame, OpticalCapture(item[LIT_NAME], item[LIT_CASE]))}
        for first, second in ((0, 3), (1, 2), (1, 4)):
            metrics[f"phase_{first}_equals_{second}"] = ReflectionOpticalImageEvidence.compare_invariant(frames[first], frames[second])
        return metrics

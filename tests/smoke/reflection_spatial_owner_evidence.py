"""Validate production reflection spatial-owner phase and history evidence."""

import re

from window_capture_smoke import SmokeFailure

LIT_PHASE = "Phase"
LIT_INDEX = "index"
LIT_RADIUS = "radius"
LIT_SEED = "seed"
LIT_GRAPHICS_FRAME = "graphics_frame"
LIT_WARM = "Warm"
LIT_SEQUENCE = "sequence"
LIT_GENERATION = "generation"
LIT_EPOCH = "epoch"
LIT_START_GRAPHICS_FRAME = "start_graphics_frame"
LIT_SAMPLE_INDEX = "sample_index"
LIT_WORK_PUBLISH = "work_publish"
LIT_WORK_FIRST = "work_first"
LIT_WORK_LAST = "work_last"
LIT_WORK_SAMPLES = "work_samples"
LIT_WORK = "Work"
LIT_PUBLISH = "publish"
LIT_FIRST = "first"
LIT_LAST = "last"
LIT_SAMPLES = "samples"
LIT_CAPTURE = "Capture"
LIT_HARDWARE_READY = "hardware_ready"
LIT_COUNT = "count"
LIT_ELIGIBLE = "eligible"
SELECTIONS = { "fresh1": (1,), "fresh2": (2,), "fresh3": (3,), "sequence3": (1, 3), "sequence2": (1, 3, 2), "sequence1": (1, 3, 2, 1), }
FIELDS = { LIT_PHASE: {LIT_INDEX, LIT_RADIUS, LIT_SEED, LIT_GRAPHICS_FRAME}, LIT_WARM: {LIT_INDEX, LIT_SEQUENCE, LIT_GENERATION, LIT_GRAPHICS_FRAME, LIT_EPOCH, LIT_START_GRAPHICS_FRAME,
        LIT_SAMPLE_INDEX, LIT_SEED, LIT_WORK_PUBLISH, LIT_WORK_FIRST, LIT_WORK_LAST, LIT_WORK_SAMPLES}, LIT_WORK: {LIT_PUBLISH, LIT_FIRST, LIT_LAST, LIT_SAMPLES}, LIT_CAPTURE: {LIT_RADIUS, LIT_GRAPHICS_FRAME, LIT_SEED}, }


class ReflectionSpatialOwnerEvidence:
    @staticmethod
    def records(text, kind):
        result = []
        prefix = "ReflectionSpatialOwner" + kind + ":"
        for line in text.splitlines():
            if prefix not in line:
                continue
            item = {}
            for field in line.split(prefix, 1)[1].split():
                match = re.fullmatch(r"([a-z_]+)=([0-9]+)", field)
                if not match or match[1] in item:
                    raise SmokeFailure("malformed or duplicate owner evidence field")
                item[match[1]] = int(match[2])
            if set(item) != FIELDS[kind]:
                raise SmokeFailure("missing or unexpected owner evidence field")
            result.append(item)
        return result


    @staticmethod
    def validate_owner_evidence(text, selection, history, statistics):
        expected = SELECTIONS[selection]
        phases, warmed, windows, captures = (ReflectionSpatialOwnerEvidence.records(text, kind) for kind in (LIT_PHASE, LIT_WARM, LIT_WORK, LIT_CAPTURE))
        if len(phases) != len(expected) or len(warmed) != len(expected) or len(captures) != 1:
            raise SmokeFailure("owner qualification lacks every requested phase, warm proof or final reset")
        announced = re.findall(r"ReflectionSpatialOwner: selection=([a-z0-9]+) initial_radius=([0-9]+) final_radius=([0-9]+) phases=([0-9]+)", text)
        if announced != [(selection, str(expected[0]), str(expected[-1]), str(len(expected)))]:
            raise SmokeFailure("actual owner settings do not match the requested sequence")
        anchors = re.findall(r"FramebufferCapture: graphics source frame ([0-9]+)", text)
        capture = captures[0]
        if anchors != [str(capture[LIT_GRAPHICS_FRAME])] or capture[LIT_RADIUS] != expected[-1] or capture[LIT_SEED] != 0:
            raise SmokeFailure("framebuffer is detached from the final radius/seed source frame")
        by_history = {(item[LIT_SEQUENCE], item[LIT_GENERATION]): item for item in history}
        by_statistics = {(item[LIT_SEQUENCE], item[LIT_GENERATION]): item for item in statistics}
        if len(by_history) != len(history) or len(by_statistics) != len(statistics):
            raise SmokeFailure("duplicate completed sequence identity")
        if any(key not in by_statistics for key in by_history):
            raise SmokeFailure("history has no matching completed queue-token evidence")
        if len({item[LIT_GENERATION] for item in history}) != 1 or len({item["device_generation"] for item in statistics}) != 1:
            raise SmokeFailure("owner evidence crosses resource/device generations")
        by_publish = {}
        last_publish = -1
        for work in windows:
            if work[LIT_PUBLISH] <= last_publish or work[LIT_SAMPLES] <= 0 or work[LIT_FIRST] > work[LIT_LAST] \
                    or work[LIT_SAMPLES] > work[LIT_LAST] - work[LIT_FIRST] + 1:
                raise SmokeFailure("invalid or replayed completed spatial window")
            by_publish[work[LIT_PUBLISH]] = work
            last_publish = work[LIT_PUBLISH]
        if not windows:
            raise SmokeFailure("no actual completed spatial work")
        previous_warm = None
        for index, (phase, warm, radius) in enumerate(zip(phases, warmed, expected)):
            if phase != {LIT_INDEX: index, LIT_RADIUS: radius, LIT_SEED: 101 + index, LIT_GRAPHICS_FRAME: phase[LIT_GRAPHICS_FRAME]} or warm[LIT_INDEX] != index:
                raise SmokeFailure("missing, reordered or wrong-radius owner phase")
            if previous_warm is not None and (phase[LIT_GRAPHICS_FRAME] <= previous_warm[LIT_GRAPHICS_FRAME] or warm[LIT_EPOCH] <= previous_warm[LIT_EPOCH]):
                raise SmokeFailure("radius changed before the preceding phase completed")
            key = (warm[LIT_SEQUENCE], warm[LIT_GENERATION])
            sample, counters = by_history.get(key), by_statistics.get(key)
            if sample is None or counters is None or not counters[LIT_HARDWARE_READY]:
                raise SmokeFailure("warm phase lacks completed hardware/history evidence")
            for field in (LIT_GRAPHICS_FRAME, LIT_EPOCH, LIT_START_GRAPHICS_FRAME, LIT_SAMPLE_INDEX, LIT_SEED):
                if sample[field] != warm[field]:
                    raise SmokeFailure("warm phase is detached from its completed history record")
            if sample[LIT_START_GRAPHICS_FRAME] != phase[LIT_GRAPHICS_FRAME] or sample[LIT_SEED] != phase[LIT_SEED] \
                    or sample[LIT_SAMPLE_INDEX] < 2 or sample[LIT_COUNT] != 1 or sample[LIT_ELIGIBLE] != 1:
                raise SmokeFailure("phase did not complete three accepted samples from its exact reset boundary")
            work = by_publish.get(warm[LIT_WORK_PUBLISH])
            expected_work = {LIT_PUBLISH: warm[LIT_WORK_PUBLISH], LIT_FIRST: warm[LIT_WORK_FIRST], LIT_LAST: warm[LIT_WORK_LAST], LIT_SAMPLES: warm[LIT_WORK_SAMPLES]}
            if work != expected_work or work[LIT_FIRST] < phase[LIT_GRAPHICS_FRAME] or work[LIT_LAST] > sample[LIT_GRAPHICS_FRAME]:
                raise SmokeFailure("warm phase spatial work is missing, stale or not completed")
            previous_warm = warm
        source = capture[LIT_GRAPHICS_FRAME]
        if source <= previous_warm[LIT_GRAPHICS_FRAME]:
            raise SmokeFailure("capture reset precedes completion of the last prepared radius")
        coverage = [work for work in windows if work[LIT_FIRST] <= source <= work[LIT_LAST] and work[LIT_SAMPLES] == work[LIT_LAST] - work[LIT_FIRST] + 1]
        if not coverage:
            raise SmokeFailure("captured source has no dense completed spatial-range coverage")
        covering = [sample for sample in history if sample[LIT_START_GRAPHICS_FRAME] == source and sample[LIT_GRAPHICS_FRAME] >= max(source, coverage[0][LIT_LAST]) and sample[LIT_SEED] == 0
            and sample[LIT_ELIGIBLE] == 1 and sample[LIT_COUNT] == 1 and sample[LIT_EPOCH] > previous_warm[LIT_EPOCH] and by_statistics[(sample[LIT_SEQUENCE], sample[LIT_GENERATION])][LIT_HARDWARE_READY]]
        if not covering:
            raise SmokeFailure("captured image is not the first frame of its completed seed-zero epoch")
        return {"radii": list(expected), "phases": phases, "warm": warmed, "captured_graphics_frame": source, "capture_work": coverage[0], "covering_history": covering[0], "completed_spatial_windows": windows}


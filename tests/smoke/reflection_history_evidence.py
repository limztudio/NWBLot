"""Parse and validate completed reflection history evidence."""

import re

from window_capture_smoke import SmokeFailure


LIT_SEQUENCE = "sequence"
LIT_GENERATION = "generation"
LIT_GRAPHICS_FRAME = "graphics_frame"
LIT_EPOCH = "epoch"
LIT_START_GRAPHICS_FRAME = "start_graphics_frame"
LIT_COUNT = "count"
LIT_SAMPLE_INDEX = "sample_index"
LIT_SEED = "seed"
LIT_ELIGIBLE = "eligible"
LIT_REUSED = "reused"
LIT_RESET = "reset"
LIT_REASON = "reason"
LIT_TEMPORAL = "temporal_"
LIT_ROUGH_DEFORM = "rough_deform"
LIT_REFLECTIONSMOKEHISTORY = "ReflectionSmokeHistory:"
HISTORY_FIELDS = (LIT_SEQUENCE, LIT_GENERATION, LIT_GRAPHICS_FRAME, LIT_EPOCH, LIT_START_GRAPHICS_FRAME, LIT_COUNT, LIT_SAMPLE_INDEX, LIT_SEED, LIT_ELIGIBLE, LIT_REUSED, LIT_RESET, LIT_REASON)


class ReflectionHistoryEvidence:
    @staticmethod
    def parse_history(log_text):
        history = []
        for line in log_text.splitlines():
            if LIT_REFLECTIONSMOKEHISTORY not in line:
                continue
            fields = line.split(LIT_REFLECTIONSMOKEHISTORY, 1)[1].strip().split()
            sample = {}
            for field in fields:
                match = re.fullmatch(r"([a-z_]+)=([0-9]+)", field)
                if not match or match[1] in sample:
                    raise SmokeFailure("malformed completed reflection history field")
                sample[match[1]] = int(match[2])
            if set(sample) != set(HISTORY_FIELDS):
                raise SmokeFailure("completed history metadata is missing fields")
            history.append(sample)
        if not history:
            raise SmokeFailure("no completed reflection history was published")
        return history

    @staticmethod
    def validate_history(log_text, statistics, spec):
        history = ReflectionHistoryEvidence.parse_history(log_text)
        anchors = re.findall(r"FramebufferCapture: graphics source frame ([0-9]+)", log_text)
        if len(anchors) != 1:
            raise SmokeFailure("capture lacks one exact graphics source-frame anchor")
        source = int(anchors[0])
        stats_by_identity = {(sample[LIT_SEQUENCE], sample[LIT_GENERATION]): sample for sample in statistics}
        previous = None
        for sample in history:
            if (sample[LIT_SEQUENCE], sample[LIT_GENERATION]) not in stats_by_identity:
                raise SmokeFailure("completed history is detached from its accepted statistics token")
            if any(sample[name] not in (0, 1) for name in (LIT_ELIGIBLE, LIT_REUSED, LIT_RESET)) \
                or sample[LIT_REASON] > 8 or sample[LIT_COUNT] > spec.samples \
                or sample[LIT_START_GRAPHICS_FRAME] > sample[LIT_GRAPHICS_FRAME]:
                raise SmokeFailure("completed history state exceeds its typed contract")
            wide = (LIT_SEQUENCE, LIT_GENERATION, LIT_GRAPHICS_FRAME, LIT_EPOCH, LIT_START_GRAPHICS_FRAME)
            if any(value > (0xffffffffffffffff if name in wide else 0xffffffff) for name, value in sample.items()):
                raise SmokeFailure("completed history metadata exceeds its integer field width")
            if previous and (sample[LIT_SEQUENCE] <= previous[LIT_SEQUENCE] or sample[LIT_GRAPHICS_FRAME] <= previous[LIT_GRAPHICS_FRAME] or sample[LIT_EPOCH] < previous[LIT_EPOCH]):
                raise SmokeFailure("completed history source frames or epochs moved backwards")
            if sample[LIT_REUSED] and (not sample[LIT_ELIGIBLE] or sample[LIT_RESET] or sample[LIT_COUNT] < 2):
                raise SmokeFailure("history reported reuse without an eligible prior sample")
            if previous and sample[LIT_EPOCH] == previous[LIT_EPOCH]:
                if sample[LIT_START_GRAPHICS_FRAME] != previous[LIT_START_GRAPHICS_FRAME] \
                    or sample[LIT_SAMPLE_INDEX] <= previous[LIT_SAMPLE_INDEX] or sample[LIT_COUNT] < previous[LIT_COUNT]:
                    raise SmokeFailure("history epoch did not retain its start, progressive index, and count")
            previous = sample
        covered = [sample for sample in history if sample[LIT_GRAPHICS_FRAME] >= source]
        if not covered:
            raise SmokeFailure("readback quit before completed reflection metadata covered its source frame")
        captured = covered[0]
        if captured[LIT_SEED] != spec.seed:
            raise SmokeFailure("captured reflection used the wrong sampling seed")
        raw = not spec.temporal or spec.case in ("temporal_deform", LIT_ROUGH_DEFORM)
        if raw:
            if any(sample[LIT_ELIGIBLE] or sample[LIT_REUSED] or sample[LIT_COUNT] for sample in history):
                raise SmokeFailure("raw or runtime-deformed geometry reused reflection history")
        elif not captured[LIT_ELIGIBLE]:
            raise SmokeFailure("captured static reflection was ineligible")
        if spec.case.startswith(LIT_TEMPORAL):
            mutations = re.findall(r"ReflectionSmokeMutation: graphics_frame=([0-9]+) fresh_final=([01]) seed=([0-9]+)", log_text)
            if len(mutations) != 1 or int(mutations[0][1]) != int(spec.final_state) or int(mutations[0][2]) != spec.seed:
                raise SmokeFailure("temporal capture lacks the requested single scene mutation")
            mutation = int(mutations[0][0])
            before = [sample for sample in history if sample[LIT_GRAPHICS_FRAME] < mutation]
            if not before or (not raw and max(sample[LIT_COUNT] for sample in before) < 32):
                raise SmokeFailure("temporal mutation occurred before static history was populated")
            if spec.post_reset_samples == 1 and source != mutation:
                raise SmokeFailure("temporal image missed the first post-mutation graphics frame")
            if not raw:
                if captured[LIT_START_GRAPHICS_FRAME] != mutation or captured[LIT_EPOCH] <= before[-1][LIT_EPOCH]:
                    raise SmokeFailure("the captured frame retained stale history across the scene mutation")
                if spec.post_reset_samples > 1 and captured[LIT_COUNT] != spec.post_reset_samples:
                    raise SmokeFailure("post-reset capture did not reach its accepted history sample count")
        elif not raw:
            if captured[LIT_COUNT] != spec.samples or captured[LIT_START_GRAPHICS_FRAME] > source:
                raise SmokeFailure("static image was captured before its accepted sample-count plateau")
            if not any(sample[LIT_COUNT] == spec.samples and sample[LIT_GRAPHICS_FRAME] < source for sample in history):
                raise SmokeFailure("capture requested convergence without prior completed cap evidence")
        return {"captured_graphics_frame": source, "covering_completed_history": captured, "completed_samples": len(history), "history": history}


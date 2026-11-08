"""Validate completed reflection frame statistics."""


import re

from window_capture_smoke import SmokeFailure


LIT_OFFSCREEN = "offscreen"
LIT_OPAQUE_GLASS = "opaque_glass"
LIT_FLOOR = "floor"
LIT_DISABLED = "disabled"
LIT_SCREEN = "screen"
LIT_HARDWARE = "hardware"
LIT_HYBRID = "hybrid"
LIT_SEQUENCE = "sequence"
LIT_GENERATION = "generation"
LIT_FRAME = "frame"
LIT_MODE = "mode"
LIT_WIDTH = "width"
LIT_HEIGHT = "height"
LIT_REQUESTED_BUDGET = "requested_budget"
LIT_EFFECTIVE_BUDGET = "effective_budget"
LIT_QUEUE_CAPACITY = "queue_capacity"
LIT_HARDWARE_REQUESTED = "hardware_requested"
LIT_HARDWARE_AVAILABLE = "hardware_available"
LIT_HARDWARE_READY = "hardware_ready"
LIT_TOKEN_QUEUE = "token_queue"
LIT_TOKEN_VALUE = "token_value"
LIT_PHYSICAL_QUEUE = "physical_queue"
LIT_DEVICE_GENERATION = "device_generation"
LIT_CANDIDATES = "candidates"
LIT_HARDWARE_RAYS = "hardware_rays"
LIT_HARDWARE_HITS = "hardware_hits"
LIT_OPAQUE_PIXELS = "opaque_pixels"
LIT_GLASS_PIXELS = "glass_pixels"
LIT_FALLBACK_PIXELS = "fallback_pixels"
LIT_SCREEN_ATTEMPTS = "screen_attempts"
LIT_SCREEN_HITS = "screen_hits"
LIT_REFLECTIONSMOKESTATISTICS = "ReflectionSmokeStatistics:"
MODES = (LIT_DISABLED, LIT_SCREEN, LIT_HARDWARE, LIT_HYBRID)
DEFAULT_RAY_BUDGET = 2 * 960 * 720
STATISTICS_FIELDS = (LIT_SEQUENCE, LIT_GENERATION, LIT_FRAME, LIT_MODE, LIT_WIDTH, LIT_HEIGHT, LIT_REQUESTED_BUDGET, LIT_EFFECTIVE_BUDGET, LIT_QUEUE_CAPACITY, LIT_HARDWARE_REQUESTED, LIT_HARDWARE_AVAILABLE, LIT_HARDWARE_READY,
    LIT_TOKEN_QUEUE, LIT_TOKEN_VALUE, LIT_PHYSICAL_QUEUE, LIT_DEVICE_GENERATION, LIT_CANDIDATES, LIT_HARDWARE_RAYS, LIT_HARDWARE_HITS, LIT_OPAQUE_PIXELS, LIT_GLASS_PIXELS, LIT_FALLBACK_PIXELS, LIT_SCREEN_ATTEMPTS, LIT_SCREEN_HITS)
COUNTER_FIELDS = STATISTICS_FIELDS[-8:]


class ReflectionStatistics:
    @staticmethod
    def parse_statistics(log_text):
        samples = []
        prefix = LIT_REFLECTIONSMOKESTATISTICS
        for line in log_text.splitlines():
            if prefix not in line:
                continue
            fields = line.split(prefix, 1)[1].split()
            if len(fields) != len(STATISTICS_FIELDS):
                raise SmokeFailure("completed reflection statistics have missing or duplicate fields")
            sample = {}
            for field in fields:
                match = re.fullmatch(r"([a-z_]+)=([0-9]+)", field)
                if not match or match[1] not in STATISTICS_FIELDS or match[1] in sample:
                    raise SmokeFailure("completed reflection statistics contain a malformed field")
                sample[match[1]] = int(match[2])
            samples.append(sample)
        if not samples:
            raise SmokeFailure("no completed reflection statistics were published")
        return samples

    @staticmethod
    def validate_statistics(samples, case, mode, budget=DEFAULT_RAY_BUDGET, allow_zero_samples=False, extent=(960, 720)):
        width, height = extent
        stable = []
        previous = None
        for sample in samples:
            if set(sample) != set(STATISTICS_FIELDS) or any(type(value) is not int or value < 0 for value in sample.values()):
                raise SmokeFailure("completed reflection statistics are malformed")
            wide_fields = (LIT_SEQUENCE, LIT_GENERATION, LIT_TOKEN_VALUE)
            if any(value > (0xffffffffffffffff if name in wide_fields else 0xffffffff) for name, value in sample.items()):
                raise SmokeFailure("completed reflection statistics exceed their integer field widths")
            if sample[LIT_MODE] != MODES.index(mode) or (sample[LIT_WIDTH], sample[LIT_HEIGHT]) != (width, height):
                raise SmokeFailure("completed reflection statistics do not match the requested mode and dimensions")
            capacity = sample[LIT_QUEUE_CAPACITY]
            if capacity <= 0 or capacity > 2 * width * height or sample[LIT_REQUESTED_BUDGET] != budget \
                or sample[LIT_EFFECTIVE_BUDGET] != min(budget, capacity):
                raise SmokeFailure("completed reflection statistics do not match the requested budget and capacity")
            if not sample[LIT_SEQUENCE] or not sample[LIT_GENERATION] or not sample[LIT_TOKEN_VALUE] \
                or not 0 < sample[LIT_DEVICE_GENERATION] <= 0xffff or not 0 <= sample[LIT_PHYSICAL_QUEUE] < 0xffff \
                or sample[LIT_TOKEN_QUEUE] > 2:
                raise SmokeFailure("reflection statistics lack an accepted token and device identity")
            if any(sample[name] not in (0, 1) for name in (LIT_HARDWARE_REQUESTED, LIT_HARDWARE_AVAILABLE, LIT_HARDWARE_READY)) \
                or sample[LIT_HARDWARE_REQUESTED] != int(mode in (LIT_HARDWARE, LIT_HYBRID)):
                raise SmokeFailure("reflection statistics have inconsistent hardware route metadata")
            if previous:
                identity = (LIT_GENERATION, LIT_DEVICE_GENERATION, LIT_PHYSICAL_QUEUE, LIT_TOKEN_QUEUE)
                if any(sample[name] != previous[name] for name in identity):
                    raise SmokeFailure("static reflection capture mixed statistics generations or physical queues")
                if sample[LIT_SEQUENCE] <= previous[LIT_SEQUENCE] or sample[LIT_FRAME] <= previous[LIT_FRAME] \
                    or sample[LIT_TOKEN_VALUE] <= previous[LIT_TOKEN_VALUE]:
                    raise SmokeFailure("completed reflection statistics are stale or out of order")
            previous = sample
            eligible = sample[LIT_OPAQUE_PIXELS] + sample[LIT_GLASS_PIXELS]
            if sample[LIT_OPAQUE_PIXELS] > width * height or sample[LIT_GLASS_PIXELS] > width * height \
                or sample[LIT_CANDIDATES] > eligible or sample[LIT_SCREEN_ATTEMPTS] > eligible \
                or sample[LIT_SCREEN_HITS] > sample[LIT_SCREEN_ATTEMPTS]:
                raise SmokeFailure("reflection counters exceed their eligible pixel population")
            if sample[LIT_HARDWARE_RAYS] > min(sample[LIT_EFFECTIVE_BUDGET], sample[LIT_CANDIDATES]) \
                or sample[LIT_HARDWARE_HITS] > sample[LIT_HARDWARE_RAYS]:
                raise SmokeFailure("actual hardware rays or hits exceeded the completed frame budget")
            zero_samples = eligible - sample[LIT_SCREEN_HITS] - sample[LIT_HARDWARE_HITS] - sample[LIT_FALLBACK_PIXELS]
            # GGX samples below the geometric horizon are valid zero radiance, with no trace or fallback.
            if not (0 <= zero_samples <= sample[LIT_OPAQUE_PIXELS] if allow_zero_samples else zero_samples == 0):
                raise SmokeFailure("reflection outcome counters do not partition eligible pixels")
            if mode in (LIT_DISABLED, LIT_SCREEN) and (sample[LIT_HARDWARE_RAYS] or sample[LIT_HARDWARE_HITS] or sample[LIT_CANDIDATES]):
                raise SmokeFailure("a non-hardware reflection route issued hardware work")
            if mode == LIT_DISABLED and any(sample[name] for name in COUNTER_FIELDS):
                raise SmokeFailure("disabled reflection produced nonzero counters")
            if mode == LIT_HARDWARE and (sample[LIT_SCREEN_ATTEMPTS] or sample[LIT_SCREEN_HITS]):
                raise SmokeFailure("hardware-only reflection attempted screen tracing")
            if not sample[LIT_HARDWARE_READY] and (sample[LIT_HARDWARE_RAYS] or sample[LIT_HARDWARE_HITS]):
                raise SmokeFailure("reflection issued hardware rays without a ready hardware route")
            if sample[LIT_FRAME] < 3:
                continue
            if mode != LIT_DISABLED and not sample[LIT_OPAQUE_PIXELS]:
                raise SmokeFailure("stable reflection statistics are missing the opaque fixture")
            if mode in (LIT_SCREEN, LIT_HYBRID) and not sample[LIT_SCREEN_ATTEMPTS]:
                raise SmokeFailure("stable screen route did not attempt screen tracing")
            if mode in (LIT_HARDWARE, LIT_HYBRID):
                bounded_queue = min(sample[LIT_CANDIDATES], sample[LIT_EFFECTIVE_BUDGET])
                if sample[LIT_EFFECTIVE_BUDGET] > 0 and (not sample[LIT_HARDWARE_AVAILABLE] or not sample[LIT_HARDWARE_READY]):
                    raise SmokeFailure("stable hardware capture has no ready hardware route")
                if sample[LIT_HARDWARE_RAYS] > bounded_queue:
                    raise SmokeFailure("ready hardware route exceeded its bounded candidate queue")
                if bounded_queue > 0 and sample[LIT_HARDWARE_RAYS] == 0:
                    raise SmokeFailure("ready hardware route executed no rays from its bounded candidate queue")
            if case == LIT_OPAQUE_GLASS and mode != LIT_DISABLED and not sample[LIT_GLASS_PIXELS]:
                raise SmokeFailure("stable reflection statistics are missing primary glass")
            if case == LIT_OFFSCREEN and mode in (LIT_HARDWARE, LIT_HYBRID) and not sample[LIT_HARDWARE_HITS]:
                raise SmokeFailure("offscreen reflection has no completed hardware hits")
            if case == LIT_FLOOR and mode in (LIT_SCREEN, LIT_HYBRID) and not sample[LIT_SCREEN_HITS]:
                raise SmokeFailure("floor reflection has no accepted screen hits")
            if budget < DEFAULT_RAY_BUDGET and ((budget > 0 and sample[LIT_CANDIDATES] <= sample[LIT_EFFECTIVE_BUDGET]) or not sample[LIT_FALLBACK_PIXELS]):
                raise SmokeFailure("limited-budget reflection did not exercise excess candidates and fallback")
            stable.append(sample)
        if not stable:
            raise SmokeFailure("no completed reflection statistics reached stable frame 3")
        return stable

    @staticmethod
    def compare_hybrid_statistics(hardware, hybrid):
        populations = [sample[LIT_OPAQUE_PIXELS] + sample[LIT_GLASS_PIXELS] for sample in hardware + hybrid]
        if not populations or min(populations) <= 0 or max(populations) - min(populations) > max(16, min(populations) * 0.01):
            raise SmokeFailure("floor hardware and hybrid statistics do not represent matched eligible populations")
        hardware_rays = [sample[LIT_HARDWARE_RAYS] for sample in hardware]
        hybrid_rays = [sample[LIT_HARDWARE_RAYS] for sample in hybrid]
        if not hardware_rays or not hybrid_rays or min(sample[LIT_SCREEN_HITS] for sample in hybrid) <= 0 \
            or max(hybrid_rays) >= min(hardware_rays):
            raise SmokeFailure("floor hybrid did not reduce completed hardware rays while accepting screen hits")
        return {"hardware_ray_range": [min(hardware_rays), max(hardware_rays)], "hybrid_hardware_ray_range": [min(hybrid_rays), max(hybrid_rays)], "eligible_pixel_range": [min(populations), max(populations)],
            "hybrid_accepted_screen_hit_range": [min(sample[LIT_SCREEN_HITS] for sample in hybrid), max(sample[LIT_SCREEN_HITS] for sample in hybrid)]}
